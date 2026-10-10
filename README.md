# Racing My Car 1.85 Gauge firmware

Firmware for the Racing My Car round gauge (ESP32-S3, 1.85" 360×360 touch screen). It reads the car over an
ELM327-compatible Bluetooth OBD adapter and shows Racing My Car themes, games and menus. Themes, boot animations,
games and firmware updates come from the Racing My Car platform: <https://racingmycar.com>.

- Build: ESP-IDF v5.5.3, `idf.py build` (GitHub Actions builds every push, `.github/workflows/build.yml`).
- Release number: `version.txt` (shown on the gauge as `vN`, must match the `image-frames-vN` release).
- Bluetooth and Wi-Fi name: `RMC - 1.85 Gauge XXXX`.

## Licence

This firmware is free software under the GNU General Public License v3 (see `LICENSE`). It is a modified version of
an open-source gauge firmware; see `NOTICE` for where it comes from and what changed.

---

# Racing My Car 1.85 仪表固件

Racing My Car 圆形仪表（ESP32-S3，1.85 英寸 360×360 触摸屏）的固件：通过蓝牙 OBD 适配器读取车辆数据，显示
Racing My Car 的仪表主题、游戏与菜单。主题、开机动画、游戏与固件更新由 Racing My Car 平台提供：<https://racingmycar.com>。

本固件以 GNU GPL v3 许可证发布（见 `LICENSE`），来源与修改说明见 `NOTICE`。
