# T-Bao S31 VoCat 喵伴

基于 **T-Bao S31 / ESP32-S31** 的 AI 萌宠固件。项目将小智语音对话、圆形触摸屏表情、摄像头视觉、音乐播放和运动控制整合到一套 ESP-IDF 应用中。

> 当前状态：T-Bao S31 板级配置、摄像头/音频/显示驱动和应用功能已接入工程，代码可用于构建固件；摄像头 PID、PMIC、屏幕、触摸、音频和 SD 卡仍需要在真实硬件上完成 smoke test。

[English README](README.md)

## 功能

- **小智语音对话**：离线唤醒词（“你好喵伴”“你好小智”）+ WebSocket 云端 ASR、LLM 和 TTS，支持表情状态和实时字幕。
- **圆形触摸 UI**：LVGL 9 + esp_emote_expression，提供表情主页、启动器、对话、摄像头、音乐、视频、游戏、番茄钟、动作、设置和关于页面。
- **摄像头与视觉**：OV2640/OV3660 兼容的 PARLIO JPEG 摄像头；支持屏幕预览、HTTP MJPEG `/stream`、JPEG 抓拍 `/snapshot`、录像、轻量人脸检测/人脸跟随和番茄钟守护模式。
- **音乐与视频**：microSD 本地音频/视频播放；在线音乐搜索会从多个公共 API 解析可播放地址并交给播放器。
- **运动控制**：双电机和舵机控制，支持触摸、Web、语音/MCP 等控制源，并带有速度限制、序列动作和故障保护。
- **联网与控制**：Wi-Fi 配网、音量/亮度控制、设备设置、MCP 工具调用和 HTTP 预览服务。

## 硬件与板级配置

板型名称为 `tbao_s31_board`，配置位于 [`boards/tbao_s31_board`](boards/tbao_s31_board)。

| 模块 | 当前配置 |
| --- | --- |
| 主控 | ESP32-S31，目标名 `esp32s31`，16 MB Flash + PSRAM |
| LCD | ST7796S，约 1.54 英寸，320×320，8-bit PARLIO/I80，总线 GPIO36/37/38/39/40/42/43/44 |
| 触摸 | FT6336/FT6336U，I2C 地址 `0x38`，触摸复位/中断由 XL9555 提供 |
| 音频 | ES8389 播放 + ES7210 双麦采集，I2S GPIO6/7/8/9/10 |
| 摄像头 | OV2640/OV3660，PARLIO JPEG；SCCB GPIO3/4，RESET GPIO45，XCLK GPIO55，数据 GPIO46..53，PCLK GPIO54，HREF GPIO57 |
| 存储 | microSD，SDMMC 4-bit：D0..D3 = GPIO20..23，CLK = GPIO24，CMD = GPIO25，挂载点 `/sdcard` |
| 扩展与执行器 | XL9555 GPIO 扩展、双电机驱动、舵机输出、LCD 背光 PWM |

更完整的引脚、电源和连接器说明见 [`pinmap_cn.md`](pinmap_cn.md)。

## 环境要求

- ESP-IDF **6.1 或更高版本**，目标芯片为 `esp32s31`。
- Python、CMake、Ninja 和 `riscv32-esp-elf` 工具链由 ESP-IDF 安装器提供。
- Windows 用户可在 ESP-IDF PowerShell 环境中执行下面的命令；Linux/macOS 只需将 PowerShell 脚本步骤替换为对应 shell 命令。

检查环境：

```bash
idf.py --version
idf.py --list-targets | findstr esp32s31
```

## 构建与烧录

在仓库根目录执行：

```bash
# 首次配置目标芯片
idf.py set-target esp32s31

# 解析仍需从注册表获取的依赖
idf.py reconfigure

# T-Bao S31 的 Board Manager 生成代码已提交到 components/gen_bmgr_codes
# 全新克隆后无需重新生成

# 编译
idf.py build

# 烧录并查看串口日志（把 COM12 换成实际端口）
idf.py flash
idf.py -p COM12 monitor
```

ESP-DL、人脸检测、项目修改过的运行时组件以及 T-Bao S31 的 Board Manager 生成组件已纳入 `components/`，不再依赖被修改的 `managed_components/`。板级 YAML（`boards/tbao_s31_board`）发生变化后，请重新执行 `idf.py gen-bmgr-config -b tbao_s31_board -c ./boards`，再执行 `idf.py reconfigure` 和 `idf.py build`。

仓库提供了 [`build_only.ps1`](build_only.ps1) 和 [`esptool_flash.ps1`](esptool_flash.ps1) 作为本机自动化脚本。脚本包含本地 ESP-IDF 路径、Python 环境和 `COM12` 等默认值，使用前请检查并按实际环境修改；跨机器构建建议使用上面的 `idf.py` 命令。

## SD 卡目录

应用将 SD 卡挂载到 `/sdcard`：

| 内容 | 位置 |
| --- | --- |
| 音乐 | `/sdcard` 根目录下的可播放音频文件 |
| 视频 | `/sdcard` 根目录下的 MP4/AVI/MJPEG 文件 |
| 录像 | `/sdcard/record` |

## HTTP 预览

设备联网后，访问设备 IP 的以下路径：

- `/`：预览和设备控制页面
- `/snapshot`：获取当前 JPEG 帧
- `/stream`：MJPEG 流（单个长连接查看器）

具体端口和网络地址以串口日志为准。

## 项目结构

```text
main/                         应用、页面、摄像头、音频、运动和 HTTP 服务
boards/tbao_s31_board/       T-Bao S31 Board Manager 配置
components/                   本地组件（esp-dl、人脸检测、板级适配等）
managed_components/           ESP-IDF Component Manager 依赖
patches/                      对 managed_components 的可重复应用补丁
assets/                       启动画面和 UI 资源
docs/                         设计记录与移植文档
tests/                        Python 合约测试
```

## 测试与验证

运行仓库内的静态合约测试：

```bash
python -m unittest discover -s tests -p "test_*.py"
```

构建成功只代表固件和资源可以生成。首次上板还应依次验证：摄像头传感器识别、摄像头供电电压、LCD/触摸方向、ES8389 播放、ES7210 录音、Wi-Fi 配网、SD 卡读写和电机/舵机安全停止。

## 许可证

除非文件另有说明，应用代码采用 Apache-2.0。组件、模型和资源包沿用上游项目许可证，详情请查看对应组件目录。
