# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目与操作约定

这是 T-Bao S31 的 ESP-IDF C/C++ 固件，整合小智语音、触摸表情 UI、摄像头、人脸跟随、SD 媒体播放与双电机/舵机控制。目标是 **`esp32s31`**（不是 `esp32s3`），板型为 `tbao_s31_board`，使用 16 MB Flash、PSRAM 和 320×320 LCD。

**用户自行编译、烧录。默认只修改代码并报告，不运行固件构建或烧录，除非用户明确要求。** 下列命令是开发参考，不代表需要自动执行。

## 构建与配置

要求 ESP-IDF **≥6.1**，以及其配套 Python、CMake、Ninja 和 `riscv32-esp-elf` 工具链。在已激活的 ESP-IDF 环境、仓库根目录运行：

```sh
idf.py --version
idf.py --list-targets

# 仅首次配置目标；不要在每次改代码后重复执行
idf.py set-target esp32s31

idf.py reconfigure
idf.py menuconfig
idf.py build

# 仅在用户明确要求时烧录或启动串口监视；替换实际串口
idf.py -p COM12 flash
idf.py -p COM12 monitor
```

- 根 `CMakeLists.txt` 定义工程名 `xiaozhi_chat`，固件输出为 `build/xiaozhi_chat.bin`；其中强制 Ninja response files，解决 Windows 编译参数过长的问题。
- `sdkconfig` 是当前生效配置；`sdkconfig.defaults` 是项目默认配置，`components/gen_bmgr_codes/board_manager.defaults` 是生成的板级默认配置。修改 defaults 不保证覆盖 `sdkconfig` 的已有值，需同步检查。
- 如需显式指定两组默认配置，Bash 使用 `export SDKCONFIG_DEFAULTS="sdkconfig.defaults;components/gen_bmgr_codes/board_manager.defaults"`；PowerShell 使用 `$env:SDKCONFIG_DEFAULTS = 'sdkconfig.defaults;components/gen_bmgr_codes/board_manager.defaults'`，随后运行相应的 IDF 配置命令。
- 板级 YAML 修改后，重新生成并检查 `components/gen_bmgr_codes/` 的变更：

```sh
idf.py gen-bmgr-config -b tbao_s31_board -c ./boards
idf.py reconfigure
# 如用户要求构建，再运行 idf.py build
```

`build_only.ps1` 是 Windows 环境辅助脚本：它配置工具链、向若干 `managed_components/` 路径重放补丁，再构建并写入 `build_only.log`。脚本仍有硬编码安装路径，且当前多个组件已改为本地覆盖，不能假定其中所有补丁都作用于实际参与构建的组件。`flash_only.ps1`、`reconfigure_only.ps1` 还引用旧的 `H:\espwork\...` 工程路径，不宜直接用于当前仓库。

## 测试入口

没有统一的项目级 lint 或完整测试 runner。测试主要是读取源码的 Python 合约检查，以及少量主机 C 测试；它们不等同于固件构建或上板验证。

### `tests/`：标准库 unittest

```sh
# 运行此目录全部测试，不构建固件
python -m unittest discover -s tests -p 'test_*.py' -v

# 单个测试文件
python -m unittest discover -s tests -p test_motion_servo_control.py -v

# 单个测试方法
python -m unittest discover -s tests -p test_motion_servo_control.py -k test_touch_servo_submission_is_part_of_motion_behavior_api -v
```

### `tools/`：独立脚本

很多检查位于 `tools/` 而非 `tests/`。例如：

```sh
python tools/test_motion_contract.py
python tools/test_camera_lifecycle_contract.py
python tools/test_display_performance.py
python tools/test_partition_budget.py
```

这些脚本有的通过 `main()` 执行，有的定义 `test_*` 函数并在脚本入口逐个调用。不要把 `unittest discover` 或一次 `pytest tools/` 当作覆盖全部脚本。安装了 pytest 时，可单独运行函数式测试，例如：

```sh
python -m pytest tools/test_display_performance.py::test_flush_completion_is_tagged_by_the_renderer_owner -q
```

`tools/test_boot_splash.py` 等图像资源检查需要 Pillow。以下脚本会调用 PATH 中的 **主机 `clang`**，编译并运行临时 C 测试程序，运行前应明确它们不是纯静态检查：

```sh
python tools/test_motion_math.py
python tools/test_face_follow.py
python tools/test_mp4_demux.py
```

其中 motion math 和 MP4 脚本遇到 `unknown-elf` clang 时只做 C 语法检查，不能据此宣称已执行 C 测试程序。

注意：`.gitignore` 忽略整个 `tools/`、`docs/`，但部分文件已跟踪，另一些仅在当前工作区存在。新增文件时检查是否被忽略；不要假定本地测试脚本在其他 checkout 中必然存在。

## 架构与资源所有权

### 启动与小智运行时

- `main/app_main.c` 先安装 PSRAM 优先的全局 cJSON allocator，再初始化 NVS、网络接口和事件循环，最后进入 `esp_xiaozhi_chat_app()`。这里不再调用 `example_connect()`，避免重复初始化 Wi-Fi。
- `main/esp_xiaozhi_chat_app.c` 是应用编排层：装配 Board Manager adapter、显示、设置、电池监控、Wi-Fi、SD、页面、运动及音频/聊天回调。`components/esp_xiaozhi/` 则实现聊天协议、WebSocket/MQTT transport、MCP、激活及视觉接口。
- 小智是独立可启停的后台运行时，通过 `esp_xiaozhi_chat_app_set_enabled()` 请求，由 lifecycle worker 执行 `DISABLED / STARTING / ENABLED / STOPPING / ERROR` 转换。关闭 AI 不应销毁显示、页面、Wi-Fi 或板级上下文；音乐可单独准备/释放共享扬声器路径。
- `app_settings.c` 在 NVS 保存 AI 开关、音效、音量与亮度，新设备默认关闭 AI，并含一次性的旧设置迁移。`wifi_provisioning.c` 独占配网/STA 生命周期：启动读取 NVS 凭据，没有凭据或连接失败仍进入离线 UI；设置页显式请求配网后重启进入 SoftAP，而不是开机强制配网。
- 音频由本地 `components/jason-mao__av_processor/`、GMF/ESP-SR 和 codec 组件提供；`app_audio_dsp.c` 处理录音/播放 DSP。唤醒、录音上传、TTS FIFO 与重连在 app 编排层协作，修改其状态或生命周期时需要同时考虑这些异步路径。

### UI 与 LCD 交接

- `esp_xiaozhi_chat_display.c` 管理物理 LCD 及渲染所有权：表情主页使用 `esp_emote_expression`，功能页面使用 LVGL 9，视频播放使用 RGB565 直绘。**同一时间只能有一个 renderer 驱动面板**；使用现有 face/video 切换 API，保留 DMA flush 完成与 owner 标记逻辑。
- `ui_page_manager.c` 负责页面注册、`on_enter/on_exit/on_tick`、状态/字幕转发及导航；页面表在 `esp_xiaozhi_chat_app.c`，页面接口和 ID 在 `ui_page_manager.h`。主页与 launcher 互通，功能页返回 launcher。
- `ui_touch.c` 提供手势，page manager 使用 `lv_async_call` 将普通 UI 操作交给 LVGL 上下文；direct-video 手势有独立路径。控制中心覆盖层同样需要保存并恢复 renderer。跨任务修改 LVGL 对象必须遵守现有异步交接/port lock 方式。
- `ui_page_style.c` 提供共享页面样式，`app_language.c` 提供中英文文本选择。不要只改某一个页面而绕过公共样式或语言入口。

### 摄像头、视觉与运动

- `app_camera.c` 包装 T-Bao PARLIO JPEG 摄像头，提供共享最新 JPEG、复制/抓拍和帧回调。`app_activity.c` 是摄像头生命周期仲裁器：页面、Web 租约、录制、守护、MCP 抓拍、人脸跟随声明需求，由低优先级 worker 启停摄像头。新增消费者应配对使用 `app_activity_camera_acquire/release()`，而不是各自直接启停硬件。
- `http_preview.c` 提供 `/stream`、`/snapshot` 等接口；本地预览、录制、视觉和人脸跟随复用 JPEG 来源。`app_face_follow.cpp` 把检测、`face_target_tracker.c` 和纯控制器 `face_follow_controller.c` 串起来，再向 motion manager 提交带 TTL 的请求。
- 人脸跟随支持离页后台运行，页面退出不总意味着停止。page manager 对小智/人脸跟随及摄像头页面有资源互斥保护，不要在新增导航路径中绕过。
- 运动路径为 UI/Web/MCP/语音/表情 → `motion_behavior.c` 或相应适配层 → `motion_manager.c` → `actuator_control.cpp`。manager 统一处理来源优先级、TTL、动作序列、加减速、反向死区、舵机限速和 failsafe；不要从页面或 MCP 直接写 PWM。`motion_math.c` 与跟随 tracker/controller 可在主机独立测试。

### 板级配置、媒体与组件覆盖

- `boards/tbao_s31_board/` 的 YAML 与 `setup_device.c` 描述板级外设、设备及电源时序，生成代码位于 `components/gen_bmgr_codes/`。`tbao_s31_board.c` 暴露共享 I2C、XL9555 和电源控制，执行器等模块需复用 Board Manager 的 bus/expander handle，不能再创建同一条 I2C 总线。引脚详情查 `pinmap_cn.md`。
- `app_sdcard.c` 挂载 `/sdcard`。音乐和视频从根目录扫描；`app_video_player.c` 结合 MP4/AVI demux 与视频 overlay 播放。README 给出的支持路径是先将 MP4 转为 320×320、10 fps 的 MJPEG 视频，不是任意 H.264 MP4 都能直接播放。
- `main/idf_component.yml` 使用大量 `path` / `override_path` 指向 `components/` 的本地修改版，包括 Board Manager、codec、emote、av_processor、WebSocket、LCD 和摄像头组件。修改依赖前先确认实际解析位置，不要仅凭目录名去改 `managed_components/`。
- `patches/` 保存部分依赖补丁/替换文件。若改动需要在依赖刷新后保留，检查对应本地组件、补丁副本和重放脚本的关系。
- `main/CMakeLists.txt` 显式列出源文件，文件存在不代表参与编译。这里也将 `assets/welcome_boot.gif` 烧入 `welcome` 分区，并用 **`360_360`** emote 资源生成 `assets.bin`（虽然实际屏幕为 320×320）。`partitions.csv` 在 16 MB 内划分 `model`、单个 `factory` 应用、`assets` 和 `welcome`；资源变大时必须一起检查预算。

## S31 内存与初始化约束

内部 SRAM 是瓶颈，PSRAM 富余不代表内部分配能成功。AI、AFE、Wi-Fi/TLS、emote 并行时，应检查 **internal free 和 largest block**，而非只看总 heap；页面切换已有 `UI_PAGE_MGR` 日志。

- 大型 JSON、图像、媒体缓冲和非实时静态数组尽量沿用 PSRAM 分配/`EXT_RAM_BSS_ATTR`。但会进入 cache-disabled 驱动路径的任务栈必须保留在内部 RAM，尤其本地 WebSocket wrapper 和小智 lifecycle worker；不能为了省 SRAM 一律移到 PSRAM。
- `sdkconfig.defaults` 与当前 `sdkconfig` 保留了关键保护：`CONFIG_ESP_PHY_DISABLE_PLL_TRACK=y` 避免 PHY 每次唤醒创建 timer 时因内部内存碎片而 abort；静态 Wi-Fi TX buffers 为 8；TLS 使用外部内存并禁用若干硬件加密路径以避免隐式 internal DMA 分配。
- S31 WakeNet/qacc 无法访问 RTC fast RAM，保持 `CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP=n`。启动时先初始化音频/AFE，再启用 emote，使 WakeNet 优先获得所需内部 SRAM；同时保持 boot splash → emote 的 LCD 交接与 display-ready 唤醒门控。

README.md / README_cn.md 描述当前功能与使用方式。`docs/PROJECT_STATUS.md` 的早期章节保留旧板型、旧目录和历史未实现状态，不应覆盖当前板级 YAML、配置与代码事实。
