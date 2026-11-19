# T-Bao S31

基于 **T-Bao S31 / ESP32-S31** 的 AI 萌宠固件。项目将小智语音对话、圆形触摸屏表情、摄像头视觉、音乐播放和运动控制整合到一套 ESP-IDF 应用中。

> 当前状态：T-Bao S31 板级配置、摄像头/音频/显示驱动和应用功能已接入工程，代码可用于构建固件。

[English README](README.md)

## 功能

- **小智语音对话**：离线唤醒词（“你好小智”）+ WebSocket 云端 ASR、LLM 和 TTS，支持表情状态和实时字幕。
- **触摸 UI**：LVGL 9 + `esp_emote_expression`，提供表情主页、启动器、对话、摄像头、音乐、视频、游戏、番茄钟、动作、设置和关于页面。
- **摄像头与视觉**：OV2640/OV3660 兼容的 PARLIO JPEG 摄像头；支持屏幕预览、HTTP MJPEG `/stream`、JPEG 抓拍 `/snapshot`、录像、轻量人脸检测/人脸跟随和番茄钟守护模式。
- **音乐与视频**：microSD 本地音频/视频播放；在线音乐搜索会从多个公共 API 解析可播放地址并交给播放器。
- **运动控制**：双电机和舵机控制，支持触摸、Web、语音/MCP 等控制源，并带有速度限制、序列动作和故障保护。
- **联网与控制**：Wi-Fi 配网、音量/亮度控制、设备设置、MCP 工具调用和 HTTP 预览服务。
- **芽伴同行 v3（待上板验证）**：游戏卡片内以 ESP-NOW 连接 Pet Ring，点击开始取当前相对零点、双向校验/找回伙伴、开始/停止全量程体感驾驶；奶油色圆角双宠舞台加入原创带眼睛的宝仔与来访芽伴，支持六种互动和16组本地神秘对白。未新增独立 App，本轮未编译或烧录。

### Pet Ring 联动

小车进入「游戏 → 芽伴同行」，指环进入「照顾 → 玩耍 → 联动」。首次自动发现，核对六位码后双方各点一次确认；**点击开始时取当前佩戴姿态为相对零点**，无需回旧水平或等待一秒校准。单次输入异常先发零目标，短窗口内恢复继续同一次驾驶；持续180ms无可靠姿态、控制超时、故障或STOP/退出仍撤销授权。真正失联自动重连后不自行开车，需新的开始。

按要求已取消直行优先、方向模式/额外死区、差速上限与35%/25%缩放，前后/左右输入独立使用完整±1.0，PWM可达±100%，不是固定满功率。仅联动倒车/转向按输入平滑将最低起转值由18%补偿到40%，其它来源不变；零输入仍停车。底层不再每次清零工作PWM，修复到零之后的80ms换向等待。**力度/最高速度显著增加，先悬空轮子测试。** 保留STOP/失联/输入/低电/故障与授权保护，不更换引脚或清轮向NVS。

协议仍v3，请同步更新两端。`RING_DRIVE raw/sent`、`motor_diag`、`motor_pwm_diag`分别显示意图、轮目标/输出和四路PWM寄存器；寄存器读回不是实测引脚波形或实际转速。若仍单轮不动，先用已有动作页四方向对照IMU，依据目标/寄存器/实物三层结果排查反向通道、驱动/接线或负载，不能以代码检查代替硬件测量。

退出重进会恢复无线服务并做Discover/Welcome、加密Ready/Ack及业务心跳双向校验，发现广播不再维持假连接。每连接使用新的临时LMK，旧绑定不清。车端两行对白按实际20px字体行高静态显示，超长句固定位置换段，不再纵向或横向滚动；STOP始终可达。

两端「… → 解除绑定 → 确认解除」均为短点操作，在线尽力同步双方；对方离线时清本端并明确提示未确认，存储失败可重试，不删除养成/网络设置。**v3 需两端同步更新，保留原绑定密钥和宠物存档。** 同行期间已联网 STA 不受影响，若 AP 掉线则暂缓后台扫描以保持 ESP-NOW 信道，退出恢复 STA 重连。

上下文、队列优先 PSRAM，256×112 RGB565 单舞台缓冲57344 bytes严格用 PSRAM，退出先解除 LVGL 引用/缓存再释放；失败仍能连接/聊天。调用 NVS/Flash 的 worker 栈保留内部 RAM（6144 bytes），不新增320全屏缓冲。启动日志记录 internal free/largest block；需实机观察内存余量。

宝仔方屏双眼、黄色侧环和轮足参考产品外形；芽伴外观只读来自指环。配对跳入、招手、一起跑、抱抱、悄悄话、击掌和摸摸均有屏幕演出；一起跑不驱动车轮，驾驶时互动会先停车。只有招手在仲裁允许时可轻微实体点头，不引入云端、TTS或新的音频资源。

完整协议、同步和上板清单见 [`components/petlink/README.md`](components/petlink/README.md)。`python -I tests/test_petlink_contract.py` 是不编译的源码合约，指环另有参考状态模拟、字库/副本检查；这些不能证明无线稳定性或 SDK 编译已通过。C++ 核心与图像哨兵用例已更新但本轮未编译执行。用户测试请先悬空车轮；不具备防跌落、避障或精确定位能力。

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

# 可选：重新解析或拉取依赖
idf.py reconfigure

# 编译
idf.py build

# 烧录（如需指定串口，可追加 `-p COM12`）
idf.py flash
```

### 首次配网与大模型配置

固件烧录成功后设备默认关闭小智 AI，并可直接进入离线界面，不要求立即配网。需要联网时进入“设置 → WLAN → 重新配置 WiFi”，按屏幕提示完成配网。配网完成后，打开 [xiaozhi.me](https://xiaozhi.me/)，进入控制台并在右上角选择“添加设备”，输入屏幕或串口日志中显示的 6 位验证码，即可完成大模型配置。

### 语音控制说明

固件启动后，第一次呼叫“你好小智”时设备会应答，但此时只是进入唤醒状态，暂时无法进行对话。需要第二次呼叫“你好小智”，设备可能不会再次应答，但这时就可以直接说“讲个笑话”等内容开始对话。后续如果出现对话无反应，可以再次呼叫“你好小智”重新唤醒。

### 运动校准

进入“移动”，点击右上角“校准”。校准期间暂停其他运动来源，取消或离开页面会恢复原设置；点击保存后写入 NVS，重启仍有效，并统一应用于触摸、Web、小智和人脸跟随。

- **车轮方向**：支持分别反转电机 A/B、交换 A/B。针对“左变前、右变后、后变右、前变左”的情况，可选择“修正当前错向”，然后架空车轮，按住前/后/左/右验证后保存。单次试转最长 1.5 秒，松开或急停立即停止。
- **舵机限位**：选择最低点/最高点、拖动滑条或 ±10 μs 微调时，会按当前实际脉宽平滑试位，最后一次调节后最多 3 秒自动释放 PWM；可随时点击“停止试位”。“按住试位”按钮仍为松开即停。端点可先独立试位，保存时才检查安全范围（1100～1900 μs）及至少 100 μs 的间距；允许最高点脉宽小于最低点，以适配反向安装。不要让舵机持续顶住机械限位；释放 PWM 后不再提供保持力。

### 人脸跟随调参

人脸跟随效果与静摩擦力有关，也会受到小车重量和桌面材质影响。如果跟随过程出现异常，可微调 [`main/face_follow_controller.c`](main/face_follow_controller.c) 第 26 行的默认参数：

```c
#define FACE_FOLLOW_DEFAULT_YAW_DRIVE 0.14f
```

建议围绕 `0.14f` 小幅调整；不确定参数含义时，可以先保留默认值或咨询 AI。

板级 YAML（`boards/tbao_s31_board`）发生变化后，请重新执行：

```bash
idf.py gen-bmgr-config -b tbao_s31_board -c ./boards
idf.py reconfigure
idf.py build
```


## 内存诊断

当前播放器只接受 MJPEG，配置已关闭闲置的 H.264 软件解码注册和 H.264 IRAM 放置；MP4/AVI 仍需按下方说明转成 MJPEG。拍照识别的 4 KiB 文本响应缓冲明确分配到 PSRAM。

串口中筛选 `APP_MEMORY` 可查看内存诊断：

- `boot` / `boot_ready`：应用初始化前后。`boot_ready` 时主任务栈尚未回收，后续稳态 free 可能因此增加。
- `runtime_before` / `runtime_after`：AI 开关或语言重启的生命周期处理前后。
- 页面名称：完成页面切换后的堆状态。
- `periodic`：复用现有 lifecycle worker 的周期采样，默认每 60 秒一次，不创建额外任务。

日志格式如下，`<数值>` 均为实际采样的字节数，而非 KiB：

```text
APP_MEMORY: periodic internal: free=<数值> min=<数值> largest=<数值> B
APP_MEMORY: periodic internal_dma: free=<数值> min=<数值> largest=<数值> B
APP_MEMORY: periodic psram: free=<数值> min=<数值> largest=<数值> B
APP_MEMORY: periodic task=xiaozhi_runtime stack=internal stack_free_min=<数值> B
```

`internal_dma` 是内部堆的子集，不能与 `internal` 相加。`min` 是匹配 heap 各自历史低水位的汇总，不是严格的全系统同一时刻最低值，也不会在退出页面时恢复；判断释放是否正常应比较相同场景的 `free` 和 `largest`。堆的周期日志无法捕获每次瞬态最大连续块的最低值，需要结合低水位和故障现场日志。

任务栈由各任务自采样，不暂停全系统，也不保存可能失效的其他任务 handle。当前覆盖主任务启动结束、页面导航调用任务、lifecycle worker、电池采样、触摸派发，以及有访问时的运动 Web 状态响应和摄像头 snapshot/stream；不包含所有系统任务、WebSocket 或电池保护任务。周期限制作用于各已接入任务的采样点，并非保证所有任务每 60 秒都有日志。`stack_free_min` 是该任务自创建以来的最小剩余栈，已是字节数，不再乘以 `sizeof(StackType_t)`；任务重建后重新统计。不要只凭空闲时的数值缩栈，应先覆盖最坏负载并留出安全余量。

可在 `menuconfig → Xiaozhi Chat Application` 调整 `CONFIG_APP_MEMORY_DIAGNOSTICS_INTERVAL_SECONDS`，设为 `0` 关闭周期采样，仍保留启动、页面和 AI 生命周期检查点。源码默认配置位于 `sdkconfig.defaults`，已有工程以当前 `sdkconfig` 为准。

建议上板验证：多轮 AI 开关和唤醒对话、TLS 重连、拍照识别、音乐与视频播放，以及摄像头/人脸跟随页面反复进出，观察同一稳态的内部 `free/largest` 是否稳定。Wi-Fi EXTRA/SLP IRAM 优化和已有任务栈大小目前保持不变，待测量后再调整。

## SD 卡目录

应用将 SD 卡挂载到 `/sdcard`：

| 内容 | 位置 |
| --- | --- |
| 音乐 | `/sdcard` 根目录下的可播放音频文件 |
| 视频 | `/sdcard` 根目录下的 MP4/AVI/MJPEG 文件 |

> 注意：下载的 MP4 文件需要先通过 FFmpeg 转码后才能播放。如果本地没有 FFmpeg，可以使用 [FFmpeg 在线工具](https://ffmpeg.gcc.ac.cn/advanced_zh.html)。
>
> 参数 1 填写：`-y`
>
> 参数 2 填写：
>
> ```text
> -map 0:v:0 -vf scale=320:320 -c:v mjpeg -q:v 5 -pix_fmt yuvj420p -an -r 10 -movflags +faststart -f mp4
> ```

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

## 许可证

除非文件另有说明，应用代码采用 Apache-2.0。组件、模型和资源包沿用上游项目许可证，详情请查看对应组件目录。
