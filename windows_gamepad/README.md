# Windows 虚拟 Xbox 360 手柄

桌面程序读取 Arduino 当前固件输出的 `joystick:` FireWater 行，将摇杆和 A/B/X/Y 输入转成 Windows 虚拟 Xbox 360 手柄。串口使用 115200 波特率，当前固件约每 50 ms 输出一帧。

## 使用已发布安装包

从 GitHub [Releases](https://github.com/kuranshi/PSX-Controller/releases) 下载 x64 安装程序，运行并按提示完成安装。安装程序会在未检测到 ViGEmBus 时启动官方签名驱动安装器；如果提示重启，重启后再打开“PSX Controller”。连接 Arduino，刷新或选择对应 COM 口，点击“连接手柄”。关闭 VOFA、串口监视器等占用 COM 口的软件。

检查是否生效：保持桥接程序运行，按 `Win+R` 输入 `joy.cpl`，打开 Xbox 控制器的“属性 → 测试”，移动摇杆并按下 A/B/X/Y。

## 从源码运行

需要 Windows、Python 3.10 或更高版本及 ViGEmBus 驱动。安装驱动后，在 PowerShell 执行：

```powershell
cd windows_gamepad
python -m venv .venv-gamepad
$env:VGAMEPAD_SKIP_VIGEMBUS_INSTALL = 'true'
.\.venv-gamepad\Scripts\python.exe -m pip install -r requirements.txt
.\.venv-gamepad\Scripts\python.exe .\controller_app.py --port COM3
```

可不传 `--port`，在窗口中选择串口。若 Y 轴方向反了，可加 `--invert-left-y` 或 `--invert-right-y`。开发者构建单文件安装程序请看 `build.ps1`；需要 Python、Inno Setup 7 和网络连接以下载驱动安装器。

ViGEmBus 1.22.0 已停止维护。随包驱动仅在用户执行安装程序时安装，安装过程可能需要管理员权限和重启。依赖的许可和来源列于 `THIRD-PARTY-NOTICES.md`。
