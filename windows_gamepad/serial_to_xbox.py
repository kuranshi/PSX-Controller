"""Read Arduino joystick and face-button states into a virtual Xbox 360 pad."""

from __future__ import annotations

import argparse
import time

import serial
import vgamepad as vg


BAUD_RATE = 115200
FRAME_TIMEOUT_SECONDS = 0.5
AXIS_LIMIT = 1000
BUTTONS = (
    vg.XUSB_BUTTON.XUSB_GAMEPAD_A,
    vg.XUSB_BUTTON.XUSB_GAMEPAD_B,
    vg.XUSB_BUTTON.XUSB_GAMEPAD_X,
    vg.XUSB_BUTTON.XUSB_GAMEPAD_Y,
)


def parse_frame(
    line: bytes,
) -> tuple[tuple[int, int, int, int], tuple[bool, bool, bool, bool]] | None:
    """Return J1 X/Y, J2 X/Y, and A/B/X/Y from one firmware frame."""
    try:
        text = line.decode("ascii").strip()
        if text.startswith("joystick:"):
            text = text[len("joystick:") :]
        values = [int(value) for value in text.split(",")]
        if len(values) < 14:
            return None
        axes = tuple(max(-AXIS_LIMIT, min(AXIS_LIMIT, value)) for value in values[:4])
        buttons = tuple(value == 1 for value in values[10:14])
        return axes, buttons
    except (UnicodeDecodeError, ValueError):
        return None


def normalized(value: int, invert: bool) -> float:
    result = value / AXIS_LIMIT
    return -result if invert else result


def main() -> None:
    parser = argparse.ArgumentParser(description="Map the Arduino's two joysticks to a virtual Xbox 360 gamepad.")
    parser.add_argument("port", help="Arduino serial port, for example COM5")
    parser.add_argument("--invert-left-y", action="store_true", help="Invert the left stick Y axis")
    parser.add_argument("--invert-right-y", action="store_true", help="Invert the right stick Y axis")
    args = parser.parse_args()

    gamepad = vg.VX360Gamepad()
    gamepad.reset()
    gamepad.update()
    print(f"Virtual Xbox 360 controller created. Reading {args.port} at {BAUD_RATE} baud; press Ctrl+C to stop.")

    last_valid_frame = time.monotonic()
    centered_after_timeout = False
    try:
        with serial.Serial(args.port, BAUD_RATE, timeout=0.1) as port:
            while True:
                frame = parse_frame(port.readline())
                now = time.monotonic()
                if frame is not None:
                    axes, button_states = frame
                    j1x, j1y, j2x, j2y = axes
                    gamepad.left_joystick_float(
                        x_value_float=normalized(j1x, False),
                        y_value_float=normalized(j1y, args.invert_left_y),
                    )
                    gamepad.right_joystick_float(
                        x_value_float=normalized(j2x, False),
                        y_value_float=normalized(j2y, args.invert_right_y),
                    )
                    for button, pressed in zip(BUTTONS, button_states):
                        if pressed:
                            gamepad.press_button(button=button)
                        else:
                            gamepad.release_button(button=button)
                    gamepad.update()
                    last_valid_frame = now
                    centered_after_timeout = False
                elif now - last_valid_frame > FRAME_TIMEOUT_SECONDS and not centered_after_timeout:
                    gamepad.left_joystick_float(x_value_float=0.0, y_value_float=0.0)
                    gamepad.right_joystick_float(x_value_float=0.0, y_value_float=0.0)
                    for button in BUTTONS:
                        gamepad.release_button(button=button)
                    gamepad.update()
                    centered_after_timeout = True
    except KeyboardInterrupt:
        print("Stopping; releasing face buttons and centering virtual sticks.")
    except serial.SerialException as error:
        print(f"Serial port error on {args.port}: {error}")
    finally:
        gamepad.reset()
        gamepad.update()


if __name__ == "__main__":
    main()

# by codex
