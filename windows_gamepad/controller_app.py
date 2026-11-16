"""Small Windows GUI for connecting the Arduino to a virtual Xbox 360 pad."""

from __future__ import annotations

import argparse
import queue
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk

import serial
from serial.tools import list_ports

from gamepad_core import apply_frame, parse_frame, reset_gamepad


BAUD_RATE = 115200
FRAME_TIMEOUT_SECONDS = 0.5
ARDUINO_VID = 0x2341
ARDUINO_UNO_R4_WIFI_PID = 0x1002


class ControllerApp:
    def __init__(self, root: tk.Tk, startup_port: str | None = None, invert_left_y=False, invert_right_y=False):
        self.root = root
        self.root.title("PSX Controller · 虚拟 Xbox 手柄")
        self.root.geometry("490x300")
        self.root.resizable(False, False)

        self.events: queue.Queue = queue.Queue()
        self.stop_event: threading.Event | None = None
        self.reader_thread: threading.Thread | None = None
        self.vgamepad = None
        self.gamepad = None
        self.port_labels: dict[str, str] = {}
        self.pending_error: str | None = None
        self.current_port: str | None = None

        self.port_var = tk.StringVar()
        self.status_var = tk.StringVar(value="未连接")
        self.axes_var = tk.StringVar(value="左摇杆  X: 0   Y: 0       右摇杆  X: 0   Y: 0")
        self.buttons_var = tk.StringVar(value="A：松开    B：松开    X：松开    Y：松开")
        self.invert_left_y_var = tk.BooleanVar(value=invert_left_y)
        self.invert_right_y_var = tk.BooleanVar(value=invert_right_y)
        self._build_ui()
        self.refresh_ports(preferred=startup_port)
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(20, self._poll_events)

    def _build_ui(self) -> None:
        frame = ttk.Frame(self.root, padding=18)
        frame.pack(fill="both", expand=True)

        ttk.Label(frame, text="Arduino → 虚拟 Xbox 360 手柄", font=("Microsoft YaHei UI", 14, "bold")).pack(anchor="w")
        ttk.Label(frame, text="连接后映射两个摇杆与 A/B/X/Y；串口为 115200 波特率。", wraplength=440).pack(anchor="w", pady=(4, 14))

        port_row = ttk.Frame(frame)
        port_row.pack(fill="x")
        ttk.Label(port_row, text="串口").pack(side="left")
        self.port_combo = ttk.Combobox(port_row, textvariable=self.port_var, state="readonly", width=30)
        self.port_combo.pack(side="left", padx=(8, 8))
        self.refresh_button = ttk.Button(port_row, text="刷新", command=self.refresh_ports)
        self.refresh_button.pack(side="left")

        action_row = ttk.Frame(frame)
        action_row.pack(fill="x", pady=(14, 12))
        self.connect_button = ttk.Button(action_row, text="连接手柄", command=self.connect)
        self.connect_button.pack(side="left")
        self.disconnect_button = ttk.Button(action_row, text="断开", command=self.disconnect, state="disabled")
        self.disconnect_button.pack(side="left", padx=(8, 0))
        ttk.Label(action_row, textvariable=self.status_var).pack(side="left", padx=(14, 0))

        ttk.Separator(frame).pack(fill="x", pady=4)
        ttk.Label(frame, textvariable=self.axes_var, font=("Consolas", 10)).pack(anchor="w", pady=(10, 4))
        ttk.Label(frame, textvariable=self.buttons_var).pack(anchor="w", pady=(2, 8))

        invert_row = ttk.Frame(frame)
        invert_row.pack(fill="x")
        ttk.Checkbutton(invert_row, text="反转左摇杆 Y", variable=self.invert_left_y_var).pack(side="left")
        ttk.Checkbutton(invert_row, text="反转右摇杆 Y", variable=self.invert_right_y_var).pack(side="left", padx=(12, 0))
        ttk.Label(frame, text="关闭 VOFA 和串口监视器后再连接，避免 COM 口被占用。", foreground="#555555").pack(anchor="w", pady=(14, 0))

    def refresh_ports(self, preferred: str | None = None) -> None:
        ports = list(list_ports.comports())
        self.port_labels = {}
        for port in ports:
            description = port.description or "串口设备"
            self.port_labels[f"{port.device} — {description}"] = port.device
        labels = list(self.port_labels)
        self.port_combo["values"] = labels

        chosen = None
        if preferred:
            chosen = next((label for label, device in self.port_labels.items() if device.upper() == preferred.upper()), None)
        if chosen is None:
            arduino_ports = [
                label for label, port in zip(labels, ports)
                if port.vid == ARDUINO_VID and (port.pid == ARDUINO_UNO_R4_WIFI_PID or "Arduino" in (port.description or ""))
            ]
            if len(arduino_ports) == 1:
                chosen = arduino_ports[0]
            elif len(labels) == 1:
                chosen = labels[0]
        if chosen is None and labels:
            chosen = labels[0]
        self.port_var.set(chosen or "")
        self.connect_button.configure(state="normal" if labels else "disabled")
        if not labels and self.status_var.get() == "未连接":
            self.status_var.set("未发现串口")

    def connect(self) -> None:
        label = self.port_var.get()
        port_name = self.port_labels.get(label)
        if not port_name:
            messagebox.showwarning("未选择串口", "先连接 Arduino，并选择它对应的 COM 口。", parent=self.root)
            return
        try:
            import vgamepad as vg

            gamepad = vg.VX360Gamepad()
            reset_gamepad(gamepad)
        except Exception as error:
            messagebox.showerror(
                "虚拟手柄驱动不可用",
                "Windows 没有连接到 ViGEmBus 虚拟手柄驱动。请先安装随程序提供的 ViGEmBus；如果刚安装过，请按提示重启电脑。\n\n"
                f"详细信息：{error}",
                parent=self.root,
            )
            return

        self.vgamepad = vg
        self.gamepad = gamepad
        self.current_port = port_name
        self.stop_event = threading.Event()
        self.pending_error = None
        self.status_var.set(f"正在连接 {port_name}…")
        self.connect_button.configure(state="disabled")
        self.refresh_button.configure(state="disabled")
        self.disconnect_button.configure(state="normal")
        self.reader_thread = threading.Thread(
            target=self._read_serial,
            args=(port_name, self.stop_event),
            name="ArduinoSerialReader",
            daemon=True,
        )
        self.reader_thread.start()

    def _read_serial(self, port_name: str, stop_event: threading.Event) -> None:
        last_valid_frame = time.monotonic()
        timeout_reported = False
        try:
            with serial.Serial(port_name, BAUD_RATE, timeout=0.1) as port:
                self.events.put(("connected", port_name))
                while not stop_event.is_set():
                    line = port.readline()
                    frame = parse_frame(line) if line else None
                    now = time.monotonic()
                    if frame is not None:
                        self.events.put(("frame", frame))
                        last_valid_frame = now
                        timeout_reported = False
                    elif now - last_valid_frame > FRAME_TIMEOUT_SECONDS and not timeout_reported:
                        self.events.put(("timeout", None))
                        timeout_reported = True
        except (serial.SerialException, OSError) as error:
            self.events.put(("error", str(error)))
        finally:
            self.events.put(("closed", None))

    def disconnect(self) -> None:
        if self.stop_event:
            self.stop_event.set()
        self._release_gamepad()
        self.status_var.set("正在断开串口…")
        self.disconnect_button.configure(state="disabled")
        self.connect_button.configure(state="disabled")

    def _release_gamepad(self) -> None:
        self._center_gamepad()
        self.gamepad = None
        self.vgamepad = None

    def _center_gamepad(self) -> None:
        if self.gamepad is not None:
            try:
                reset_gamepad(self.gamepad)
            except Exception:
                pass
        self.axes_var.set("左摇杆  X: 0   Y: 0       右摇杆  X: 0   Y: 0")
        self.buttons_var.set("A：松开    B：松开    X：松开    Y：松开")

    def _poll_events(self) -> None:
        try:
            while True:
                event, value = self.events.get_nowait()
                if event == "connected":
                    self.status_var.set(f"已连接 {value}")
                elif event == "frame" and self.gamepad is not None:
                    try:
                        apply_frame(
                            self.gamepad,
                            self.vgamepad,
                            value,
                            self.invert_left_y_var.get(),
                            self.invert_right_y_var.get(),
                        )
                        j1x, j1y, j2x, j2y = value.axes
                        self.axes_var.set(f"左摇杆  X: {j1x:>5}   Y: {j1y:>5}       右摇杆  X: {j2x:>5}   Y: {j2y:>5}")
                        labels = ["按下" if pressed else "松开" for pressed in value.buttons]
                        self.buttons_var.set(f"A：{labels[0]}    B：{labels[1]}    X：{labels[2]}    Y：{labels[3]}")
                        if self.status_var.get().startswith("串口数据超时"):
                            self.status_var.set(f"已连接 {self.current_port}")
                    except Exception as error:
                        self.pending_error = f"更新虚拟手柄失败：{error}"
                        self.disconnect()
                elif event == "timeout":
                    self._center_gamepad()
                    if self.stop_event and not self.stop_event.is_set():
                        self.status_var.set("串口数据超时，手柄已回中")
                elif event == "error":
                    self.pending_error = self._friendly_serial_error(value)
                    self.disconnect()
                elif event == "closed":
                    self._release_gamepad()
                    self.stop_event = None
                    self.reader_thread = None
                    self.connect_button.configure(state="normal" if self.port_labels else "disabled")
                    self.refresh_button.configure(state="normal")
                    self.disconnect_button.configure(state="disabled")
                    if self.pending_error:
                        messagebox.showerror("连接失败", self.pending_error, parent=self.root)
                        self.status_var.set("连接失败")
                        self.pending_error = None
                    elif self.status_var.get().startswith("正在断开"):
                        self.status_var.set("未连接")
        except queue.Empty:
            pass
        self.root.after(20, self._poll_events)

    @staticmethod
    def _friendly_serial_error(error: str) -> str:
        if "PermissionError" in error or "Access is denied" in error or "拒绝访问" in error:
            return "串口无法打开，可能正被 VOFA、Arduino 串口监视器或另一个桥接程序占用。关闭它们后再试。\n\n" + error
        return "无法读取 Arduino 串口。请检查 USB 连接、COM 口和 115200 波特率。\n\n" + error

    def close(self) -> None:
        if self.stop_event:
            self.stop_event.set()
        self._release_gamepad()
        self.root.destroy()


def main() -> None:
    parser = argparse.ArgumentParser(description="Connect Arduino inputs to a virtual Xbox 360 gamepad.")
    parser.add_argument("--port", help="Optionally preselect an Arduino COM port, e.g. COM3")
    parser.add_argument("--invert-left-y", action="store_true", help="Start with the left Y axis inverted")
    parser.add_argument("--invert-right-y", action="store_true", help="Start with the right Y axis inverted")
    args = parser.parse_args()

    root = tk.Tk()
    ControllerApp(root, args.port, args.invert_left_y, args.invert_right_y)
    root.mainloop()


if __name__ == "__main__":
    main()

# by codex
