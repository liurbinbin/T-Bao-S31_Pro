# PetLink v3 · 芽伴同行

指环：照顾 → 玩耍 → 联动。小车：游戏 → 芽伴同行。**两端需同步更新 v3**，不清养成存档和原绑定；绑定 blob 仍为 version=1，养成 V2 CRC不变。通信版本与养成版本不是同一个概念。

本轮不运行固件或主机 C/C++ 编译、烧录、串口。Python 源码合约/参考模型不是实际 Endpoint 执行，不是射频稳定性或机械停止测试。

## 开始、取零与停止

- 首次进入未绑定页面自动寻找伙伴，核对六位暗号，两端各点一次「成为伙伴」。配对窗口60秒；超时可再次邀请。
- **点击开始时，以当前佩戴姿态取本次相对零点。** 不再进入页面就用桌面上的姿态自动校准，也不要求返回旧±5°中立位置。
- IMU由指环专用worker唯一读取；取零使用最近100ms内至少两个可靠且方向相近的样本。正常已采样时可快速取零，不需要静止40样本/1秒；如果缺数据或剧烈晃动，固定提示等待，最多约1.8秒后取消本次开始。
- 取零后自动等解锁/驾驶授权，先零目标起步，然后按相对角度控制。回到本次零点停止运动意图；「停止」撤销授权。每次重新点开始都重新取零，行驶中绝不自动更改基准。
- 单次IMU离群或80ms以上的短读取缺口先发送**零目标暂停帧**，不重放旧非零指令。两个连续可靠样本在短窗口内恢复时，继续同一次显式驾驶，不需要再点击。
- 没有可恢复的可靠姿态连续达到180ms、车端300ms无有效控制、指环700ms无控制ACK进展、真正失联、故障/低电、STOP/退出均撤销令牌。持续“坏/好交替但一直无法恢复”也会到期，不会无限保留驾驶意图。
- **撤销后的授权不自动复活。** 真正失联可以自动重连，但重连后仍需新的开始；和同一次驾驶内的短暂输入暂停不同。

## 全量程驾驶与PWM输出检查

按用户要求已取消上一轮新增的直行优先、方向模式滞回、弱横向抑制、微量输入整体清零和0.7差速上限。两轴直接独立发送，线/角映射统一为0.001，±1000对应完整±1.0，驱动允许达到±100% PWM，不是固定满功率。斜向输入按原差速自然混合，可出现单轮支点转弯，不再强制两轮同向。原Tilt中立死区、取零、暂停、STOP、输入/失联/低电/故障保护仍保留。

仅联动的倒车/转向增加平滑起转补偿：effort=max(|angular|,max(0,-linear))，0～0.08间将minimum_percent由18%提升至40%，之后仍随输入增长到100%；纯前进保留原最低值，其它来源保持原规则。零mix始终输出零。例如归一化转向0.1得到46%目标而非旧缩放后的约20%，这不是实测扭矩，实际仍受供电和负载影响。

底层PWM更新不再反复清零工作输入：先确保非工作桥臂低，再更新工作输入；零时两路停止，idle仍暂停timer/GPIO hold。换向先制动到零，再完整等待既有80ms死区后反向加速，修复原等待分支不可达的问题。不改引脚、PWM频率、轮向NVS、共享时钟或来源仲裁。

诊断分三层：
- `RING_DRIVE raw=(linear,angular) sent=(linear,angular)`：确认IMU输入与实际发送量一致，最多约2Hz记录变化。
- `motor_diag`：requested/mix/target/output，分辨目标本来为零、正在制动/换向等待，还是稳定非零。
- `motor_pwm_diag`：A/B请求、四路引脚、软件timer运行命令、期望工作路duty与`ledc_get_duty`寄存器读回。`expected_active`与`duty_reg`按AIN1/AIN2/BIN1/BIN2排列；停用通道可保留旧寄存器值，不能当成仍在输出。

寄存器读回**不是引脚波形、电流或电机实际转速**。若工作路期望值与寄存器不同，检查PWM配置/通道；若寄存器一致而实物仍只单向转，继续查反向输入/H桥/接线/单轮起转或机械负载，不能靠软件日志宣称硬件已正常。源码已核对背光0、摄像头6、舵机5与电机1..4未发现复用，不擅自猜测换引脚。

协议仍v3，建议双端同步更新，不清绑定。先悬空车轮，因为本轮力度/最高速度明显大于之前；用已有动作页前/后/左/右与IMU四方向对照，不进行开机自动试转。`python -I tools/test_companion_drive.py`只执行参考计算和源码合约；真实C++驱动意图测试、车端`tests/motion_drive_tests.c`已更新但本轮不编译运行，不能据此声称实机单轮故障已排除。

## 页面重连与双向校验

旧问题是车端把 Discover 当作“已连接”，重复发现还持续刷新保活；单播不通也能永久显示在线。v3将引导与业务接收分开：

1. 指环广播根密钥HMAC认证 Discover，携带本次随机link ID。
2. 小车生成随机挑战boot，广播根认证 Welcome。引导不依赖旧加密peer，不刷新业务存活时间。
3. 双方以绑定根密钥、link ID、boot派生本次业务HMAC key与临时LMK，并安装加密peer。同一挑战重试不反复安装key。
4. 指环加密发送 LinkReady，小车加密返回 LinkAck；指环收到ACK才显示已连接并立即发送业务心跳。
5. 小车收到该握手之后的有效业务心跳才显示已连接。握手期间锁定候选信道；1800ms没有完成即退出候选并重试。重复Discover/Ready不能无限延长握手。

业务接收年龄只由当前连接的有效业务帧更新；广播单向可达不等于连接建立。已绑定对端的新link ID撤销旧驾驶并进入新握手，不被旧的“假在线”长期阻挡。短的已退役ID缓存拒绝近期旧引导复活。

指环重进页面显式恢复 ESP-NOW、回调和广播peer，不仅调用 Wi-Fi start 后假定旧驱动仍可用。退出立即撤销驾驶/互动，认证 Goodbye 最多重试约100ms，之后停止无线电；UI不阻塞。丢掉Goodbye时仍由业务超时/新握手恢复，不依赖通知必达。

小车沿用唯一 `wifi_provisioning` 所有者：已联网STA不改信道；同行期间若AP掉线则暂缓后台扫描，退出恢复。指环搜索合法信道，优先上次信道700ms，再250～600ms轮询；连接/握手候选期间不扫信道。两端仅在同行期间关闭省电，初始化失败约3秒重试。

## 对白与互动

车端使用固定对白区，两行按实际字体行高布置（当前20px/行），不启用任何循环滚动。超长中英句子按真实字宽在固定位置换句段，不移动文字坐标；状态长句静态省略。底部STOP不被弹层遮挡。

本地宝仔是方屏双眼、摄像头、黄色侧环、方底座与轮足的像素机器人，芽伴外观只读来自指环，不建立第二份养成。共享 `CompanionScene`/`PixelRenderer` 提供相遇跳入、招手、跑跑、抱抱、悄悄话、击掌、摸摸及16组本地双角色对白，不使用云端、TTS或新音频通道。跑跑仅屏幕演出，不自动开车；招手可在运动仲裁允许时小幅实体点头。

互动若正在驾驶，先停车再发送；STOP取消排队互动。事件带ID/来源/种子/年龄/停车epoch，重复不重播，同时触发指环优先。停车前的旧事件不能在新代次复活。

同行陪玩结算仍为同一连接累计至少30秒车端确认的非零输入，含前进/转向，手动停止后从指环招手。每次进入最多结算一次现有Play，分数0，不改RUN最佳成绩，遵守睡眠/托管/每日上限。

## 解绑

两端均「… → 解绑 → 确认解除」，短点，不依赖长按。先停车，后提交NVS，反馈双方解除/本机解除对方未确认/保存失败。在线认证请求带操作ID，幂等重试；离线不能保证双端删除，需在对方菜单再解除。取消不改数据，失败可重试。不删除养成或Wi-Fi，不因陌生广播清绑定。

## 线上帧与密码边界

所有整数little-endian；20字节头：`PR[2] version[1]=3 type[1] payload_len[2] flags[2] link[8] seq[4]`。flags=0仅公钥Offer；其他帧追加HMAC-SHA256前16字节。最大帧144字节。业务加密单播，Discover/Welcome为根认证广播。

| type | 值 | payload |
|---|---:|---|
| Offer | 1 | 公钥65 + 随机16 + role1 |
| Confirm | 2 | 空；头session为配对transcript ID |
| Discover / Welcome | 3 / 4 | 空 / boot8 |
| Heartbeat | 5 | role1 + stage1 + form1 + nature1 |
| Arm | 6 | boot8 + 申请ID8 + sceneEpoch4（20） |
| Grant | 7 | boot8 + 申请ID8 + 令牌8 + 已接受Arm序号4 + sceneEpoch4（32） |
| Control | 8 | token8 + linear2 + angular2 + enabled1 + inputPaused1 + reserved2（16） |
| Stop / Release | 9 / 10 | 空 / sceneEpoch4 |
| Status | 11 | boot8 + token8 + 控制ACK4 + linear2 + angular2 + battery1 + flags1 + reason1 + unpair1 + generation4 + txFailed4 + rxDropped4 + sceneEpoch4（44） |
| Pet | 12 | role1 + stage1 + form1 + nature1 |
| Event | 13 | ID4 + kind1 + origin1 + seed4 + signed-age2 + sceneEpoch4（16） |
| EventAck | 14 | ID4 + origin1 + accepted1（6） |
| Unbind / UnbindAck | 15 / 16 | 操作ID8 / 操作ID8 + success1 |
| LinkReady / LinkAck | 17 / 18 | 当前boot8（业务加密） |
| Goodbye | 19 | 空；当前业务会话认证 |

Control的enabled=1，inputPaused只能0/1；暂停时linear/angular必须同时为0，reserved为0。意图范围±1000，不是PWM或实测速度。Status flags bit0故障/不可用，bit1锁止，bit2输入暂停；battery=255未知。

配对仍用P-256 ECDH、SHA256 transcript和HKDF-SHA256，绑定双方角色/MAC/公钥/随机数。保留既有根密钥与LMK blob，仅业务临时key改变：

- `HMAC(root, "PR3-AUTH" + linkLE8 + bootLE8)` 为本次业务认证key。
- `HMAC(root, "PR3-LMK!" + linkLE8 + bootLE8)` 前16字节为本次ESP-NOW LMK。

标签域分离，临时key不写NVS。IDF5公开Mbed TLS3、IDF6公开PSA兼容继续保留，最多缓存两个volatile HMAC句柄，换钥/失败/退出释放；不引入OpenThread私有头、不释放全局PSA。

控制/授权、状态、心跳、事件、管理独立序号流。Status旧ACK不能撤销同代次新申请，但新停车epoch立即生效；重复Grant不重置ACK进展或反复插零帧。Release/Arm/Grant/Event校验epoch，旧令牌不能重新起步。看门狗参数仍为控制300ms、输入180ms、ACK700ms、链路2500ms，控制25Hz、状态10Hz、心跳2Hz；不以几秒维持旧方向掩盖丢包。

## 诊断与内存

停止原因：0无、1用户、2控制超时、3真实失联、4输入持续失效、5运动故障、6对端停止、7重连、8解绑、9控制ACK无进展。握手阶段：0关闭、1搜索、2车端等Ready、3指环等Ack、4车端等业务心跳、5业务已建立。

指环日志含`stop/hs/paused/input/good_age/raw_age/ack/bus_age/gap/tx_err/tx_nomem/auth_bad/reconnects`；输入原因区分缺样本、加速度离群、角速度、翻转、恢复等待。车端「连接详情」与阶段变化日志显示业务年龄、ACK、发送错误、队列丢帧、认证拒绝、worker最大间隔。`txFailed/rxDropped`不等于独立测得的空中丢包率。

- 车端Endpoint/队列/密码上下文优先PSRAM；队列控制块/自旋锁保留内部RAM。
- 舞台256×112 RGB565 = 57344字节，只分配PSRAM，退出先解除LVGL引用/缓存再释放，无320全屏副本；失败仍可连接/聊天。
- 两端worker栈各6144字节内部RAM，固定周期运行；C6沿用原128×128缓冲，无额外整屏。IMU唯一读者，与触摸/PMU共享Wire互斥，不创建第二总线。
- 实体运动只经motion_manager，两轴全量程映射±1.0，经原校准/混合/换向保护；PWM最高可达±100%。不具备防跌落、避障、精确里程或自动返航。

## 同步与验证

权威共享源码位于指环`libraries/PetLink/src`和`examples/Lilygo_Pet_Ring`；车端components为完整副本。同步脚本不覆盖`main/`适配器，manifest保护本地变更，必要时`--backup-changed`先保留旧副本。

```sh
python -I examples/Lilygo_Pet_Ring/tools/sync_companion.py --car D:/Code2/T-Bao-S31_Pro
python -I examples/Lilygo_Pet_Ring/tools/sync_companion.py --car D:/Code2/T-Bao-S31_Pro --check
python -I tools/test_companion_contract.py
python -I tools/test_companion_simulation.py
python -I D:/Code2/T-Bao-S31_Pro/tests/test_petlink_contract.py
```

Python覆盖旧Discover假在线、双方分别退出重进、双端相反顺序重进、丢Welcome/Ready/Ack/首个心跳、广播通但加密单向不通、临时key换代、输入暂停/持续异常和停止优先；它是参考模型，不执行真实Endpoint。实际Tilt/帧/令牌C++回归已写入`tests/core_tests.cpp`，图像哨兵仍在`pet_tests companion`，本轮均不编译运行。

用户上板请先悬空轮子：当前佩戴姿态开始/停止 → 正常转腕/大角度 → 连续低速驾驶 → 指环单独退出重进 → 小车单独退出重进 → 双端重进 → 断电/失联不自行行驶 → 在线/离线解绑。若仍中断，保留具体停止原因、input/握手阶段及年龄/错误计数；源码修复不代表所有实机射频问题已被验证消除。
