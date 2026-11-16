# Third-party software notices

The Windows application bundles or installs the following third-party software. The corresponding license texts are provided in this folder where applicable.

| Component | Version | License | Source |
| --- | --- | --- | --- |
| vgamepad | 0.2.0 | MIT | https://github.com/yannbouteiller/vgamepad |
| pyserial | 3.5 | BSD 3-Clause | https://github.com/pyserial/pyserial |
| PyInstaller | 6.22.3 (build tool; not shipped as a separate app) | GPL-2.0-or-later with bootloader exception | https://github.com/pyinstaller/pyinstaller |
| ViGEmBus | 1.22.0 (optional driver installer) | BSD 3-Clause | https://github.com/nefarius/ViGEmBus |
| Inno Setup | 7 (installer build tool) | Inno Setup License | https://jrsoftware.org/isinfo.php |

The ViGEmBus installer is distributed unchanged from the official project release. Its SHA-256 and Authenticode signature are checked by `build.ps1` before packaging. ViGEmBus is retired and is no longer maintained by its authors. The driver is installed only when the user runs the setup program and consents to its installer prompts.

PyInstaller's license text and bootloader exception are distributed as `licenses\pyinstaller-COPYING.txt` in the installed application. The MIT and BSD license texts are distributed alongside this notice in the installed `licenses` folder.
