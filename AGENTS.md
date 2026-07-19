# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

---

**34. Montserrat 字体切为本地文件**

编译时 Google Fonts 在线下载 Montserrat 字体超时，导致编译失败。

**修复**: 将 8 个 Montserrat 字型（8/12/16/21/24/27/30/36）从 `type: web` + `url:` 改为 `file: "fonts/Montserrat-Regular.ttf"`。

改动文件: `智能前级蓝牙2.0.yaml` 中 8 处 font 定义。


**35. ESPHome 2026.7.0 兼容: platformio_options → build_flags**

platformio_options.build_flags 在 ESPHome 2026.12.0 会移除支持。
将 build_flags 从 platformio_options 下移到 esphome: 直接子级。

改动文件: `智能前级蓝牙2.0.yaml` (esphome: 段)

**36. NV3007 黑屏修复: 显式 spi_mode: 0**

ESPHome 2026.6.4+ 在无 CS 引脚时默认 SPI mode 改为 MODE3,
NV3007 需要 MODE0, 导致初始化失败 → 有背光无显示。

修复: mipi_spi display 配置加 spi_mode: 0。

改动文件: `智能前级蓝牙2.0.yaml` (display: 段)

**37. 防爆音路径必须先软降再硬切**

开启静音、进入待机、输入切换、变压器/直通输出模式切换都不能直接硬切。
必须先让 `send_volume_to_pga` 把 PGA2311 渐变到 -96dB, 等 `current_db <= -95.5f`
后, 再执行 `pga2311::set_volume(0,0)` 或继电器动作。v2.1.49 起, 日常路径不再操作 `mute_switch`。

已修复入口:
- `soft_mute_switch.turn_on_action`: 先软降到 0, 再保持 PGA=0 软件静音
- `enter_standby`: `power_transitioning` 期间允许音量渐变, 软降完成后再关屏/待机, 不再拉 GPA6
- `switch_output_mode`: 输出模式切换使用软降 -> PGA=0 -> 切 GPA4 -> 保持软件静音窗口 -> 恢复音量
- HA `set_mute` 服务必须调用 `soft_mute_switch`, 不要直接切 `mute_switch`

改动文件: `智能前级蓝牙2.0.yaml` (switch/script/api 段)

**38. 防爆音软降统一脚本: anti_pop_fade_to_silence**

v2.1.23 初版虽然已编译通过, 但 `wait_until` 超时后仍会继续硬切, 并且输出切换/待机并发时可能覆盖 `soft_mute` 状态。

**修复**:
- 新增 `anti_pop_fade_to_silence` 统一脚本, 负责停止旧音量脚本、设置 `target_db=-96.0f`、执行 `send_volume_to_pga` 并等待 `current_db <= -95.5f`。
- 软降等待窗口为 5 秒; 超时记录 `current_db/target_db`, 再兜底 PGA=0。正常路径必须先软降完成再保持软件静音或切继电器。
- `soft_mute_switch.turn_on_action` 在 `switching_input/power_transitioning` 忙碌期间只记录 `soft_mute=true`, 不再中途直接硬静音。
- `enter_standby` 遇到输入/输出切换中则跳过本次进入待机, 避免两个音频路径状态机互相抢 `switching_input/saved_soft_mute`。
- `switch_output_mode` 恢复阶段要保留切换期间用户发出的静音请求, 不得简单恢复切换前 `saved_soft_mute`。

改动文件: `智能前级蓝牙2.0.yaml` (script/switch 段)

**39. ESPHome 2026.7.0 本地网页控制台 REST 路径**

`www/index.html` 是本地浏览器控制台, 通过设备 `web_server` 的 `/events` SSE 获取实体状态, 再发 REST 请求控制设备。

**注意**:
- ESPHome 2026.7.0 当前 REST 控制路径使用实体名称, 例如 `/number/主音量 Volume/set?value=49`, 不要把 SSE 里的短 `id` 拼成 REST object_id, 否则会 404。
- 网页 POST 请求必须带非空 body, 让浏览器发送 `Content-Length`; 空 body/无 body 在 ESPHome 2026.7 `web_server_idf` 下会触发 `411 Length Required`。优先使用 1 字节二进制 body, 避免浏览器自动添加 `Content-Type` 后触发 ESPHome 不完整支持的 `OPTIONS` 预检。
- 默认设备 IP 当前为 `192.168.31.99`; 只允许自动迁移旧默认 `192.168.31.86`, 不要覆盖用户手动保存的其他 IP。
- 网页音量 dB 显示必须和固件公式保持一致: PGA 寄存器值按 `(reg - 192) * 0.5` 换算, 不要用 `(reg - 255) * 0.5`。
- `待机模式` 这个 ESPHome/HA 开关语义为 `ON=待机, OFF=运行`; 网页按钮 active 状态应表示真正待机, 文案显示"运行/待机", 避免误解。
- 外部 `select/输入源选择 Input` 请求必须能触发 `switch_input`; 但内部代码已经设置 `current_input` 后再 `publish_state`, 因此 `set_action` 中目标等于 `current_input` 时要跳过, 防止恢复 v2.1.8 前的重复继电器吸合。
- 本条只涉及本地网页和外部输入选择入口, 不修改 BLE HID 遥控器路径。

改动文件: `www/index.html`, `智能前级蓝牙2.0.yaml` (input_select set_action)

**40. 本地网页控制台跨源/PNA 放行**

ESPHome 2026.7.0 下, 命令行不带 `Origin` 的 REST 请求可以成功, 但浏览器从本地 `www/index.html`
访问设备 `http://<ip>/...` 时会带跨源 Origin 和 Private Network Access 预检。

**修复**:
- `web_server` 必须配置 `allowed_origins: ["*"]` 和 `enable_private_network_access: true`, 否则本地网页按钮会被设备 web_server 拦截。
- 本地 `file://` 打开网页时浏览器 Origin 为 `null`; ESPHome 的 `allowed_origins` 不能单独写 `null`, 因此这里使用 `"*"`。
- `www/index.html` 保留固定实体名称兜底, 不依赖 SSE 短 id 才能发送控制请求。
- 网页显示版本必须随网页控制逻辑变化同步提升, 当前为 `WEB v2.1.51`, 便于确认浏览器没有加载旧缓存。
- 静音按钮不得点击后本地假切换, 必须等设备 SSE 状态回传后再显示 active。
- 本地控制台不再注册 Service Worker; 若浏览器里有旧注册, 打开页面时清理, 避免旧缓存导致"代码已改但页面没变"。

改动文件: `智能前级蓝牙2.0.yaml` (web_server), `www/index.html`

**41. 防爆音和 MSGEQ7 信号判定**

防爆音路径必须统一走 `anti_pop_fade_to_silence`, 不要再在局部入口写固定 1200ms/1500ms 等待后硬切。

**修复要点**:
- 输入切换也必须复用 `anti_pop_fade_to_silence`, 等 `current_db <= -95.5f` 或统一脚本结束后再 `pga2311::set_volume(0,0)`、硬静音、切输入继电器。
- `anti_pop_fade_to_silence` 保留 DEBUG 级起止日志, 验证时可临时把 logger 调到 DEBUG; 最终固件保持 INFO。
- MSGEQ7 启动零漂 offset 必须接受实机 3.3V 下约 `3200 raw` 的直流偏置, 当前上限为 `3600`; 不要改回过低阈值, 否则 offset 会被丢弃, 频谱常满格/信号判断失真。
- MSGEQ7 弱信号必须先在 raw 域扣除 `RAW_NOISE_FLOOR=6`, 再做频段增益, 最后按 `SIGNAL_FULL_SCALE_RAW=128` 映射到 0-255; 当前 `NOISE_GATE=2`。不要改回 4095 全量程缩放或“缩放后先门限再增益”, 否则十几个 raw count 的有效变化会被整数截断成 0。
- 频谱自动跳转的 `has_signal` 必须先要求当前输入 MCP 检测有效, 再用 `s_signal_avg >= 2` 判定; `s_signal_avg` 由七段平均值和单频段峰值联合得出, 并带 5 秒弱信号保持窗口。不要只看七段平均值, 否则只有少数频段跳动时 15 秒稳定计时会被反复清零。
- MSGEQ7 的 `s_signal_avg` 必须来自未显示消隐前的实时频谱平均值, 并且静音时也继续更新; 频谱视觉可以冻结/消隐, 信号判定不能冻结在旧值。
- LVGL 频谱柱/VU/主页 mini spectrum 使用显示专用视觉均衡, 当前输入无信号时先由 MCP gate 消隐, 有信号时再按低门槛 `visual_floor/visual_gain_q8/visual_knee` 软膝压缩显示; 这个增益只用于绘制, 不得反馈到自动输入或自动跳转判定。输入卡片 LED 单独使用 MCP23017 `audio_*` 状态。
- `PGA/msgeq7.h` 的 `DebugFrame` 只读快照用于临时 DEBUG 日志区分 raw、offset、frame 三层数据, 不主动产生日志。
- 网页/手机 BLE 的 `媒体控制 Media` 必须复用 `ble_hid_event_text` 推送 `esphome.hid_events`: `播放=PLAY`, `暂停=PAUSE`, `下一首=NEXT_TRACK`, `上一首=PREV_TRACK`。不要只 publish select 状态后复位, 否则 HA 播放控制不会执行。

改动文件: `智能前级蓝牙2.0.yaml`, `PGA/msgeq7.h`, `www/index.html`

**42. 本地网页按钮控制兜底**

实机 REST 直测成功但浏览器按钮无效时, 优先检查浏览器跨源/PNA 和前端实体路径, 不要先改音频核心。

**规则**:
- `www/index.html` 的控制按钮必须优先复用 `/events` 发现到的实体 `domain/name`, 并保留固定实体名称兜底。
- `switch/待机模式` 语义为 `ON=待机, OFF=运行`; 网页开机/关机/待机按钮应直接调用该 switch 的 `turn_off/turn_on`, 不要绕 `select/媒体控制 Media`。
- 媒体按钮可以调用 `select/媒体控制 Media`, 但必须通过统一发送函数, 以便复用 CORS/no-cors 兜底和路径检查。
- 浏览器 CORS/PNA 失败而 curl 可控时, 网页应优先用隐藏 iframe + form POST 发送命令; 普通 `mode: "cors"` / `mode: "no-cors"` 仅作为后备诊断路径。
- ESPHome POST 控制请求必须带非空 body/Content-Length, 否则可能返回 `411 Length Required`; 网页侧使用简单非空 body, 不要引入自定义 header 或会触发预检的 `Content-Type`。
- `www/index.html` 不要依赖 fetch 成功判断作为本地控制台首选路径; 直接双击本地 HTML 时 form POST 更稳定, fetch 只用于同源或诊断后备。
- 网页版本变化时要重置浏览器保存的旧设备 IP 到当前默认值, 并支持 `?ip=192.168.x.x` 覆盖; 多次 OTA 测试会在 `.66/.97/.99` 间切换, stale localStorage 是网页控制失败的常见原因。

改动文件: `www/index.html`, `智能前级蓝牙2.0.yaml` (firmware_version)

**43. ESPHome 2026.7 网页控制 POST 必须非空 body**

`web_server_idf` 会拒绝没有 `Content-Length` 的 POST。浏览器 `fetch(..., body: "")`
在部分环境下仍可能变成无长度 POST, 设备日志会报 `Content length is required for post`,
按钮看似发送但实体不会执行。

**修复**:
- `www/index.html` 的 `sendRequest()` 使用非空 1 字节 body, 当前 fetch 后备路径为 `Uint8Array([49])`。
- CORS 正常路径和 `no-cors` 兜底路径都必须使用同一非空 body。
- 避免自定义 header 或非简单 `Content-Type` 触发浏览器 `OPTIONS` 预检; 实机验证 `web_server_idf` 对预检可能空回复。
- curl/PowerShell 验证 REST 时也要用 `--data-raw "x"` 或等效方式, 不要只写 `-X POST`。

改动文件: `www/index.html`

**44. HID/媒体事件必须带脉冲序号**

`BLE HID 按键事件` 是 `update_interval: never` 的 text_sensor, 网页/手机 BLE/实体遥控都会复用它推送
`homeassistant.event: esphome.hid_events`。同一个动作如果连续发布固定状态（如一直是 `PLAY`）,
浏览器 SSE/HA 状态监听可能只看到第一次。

**修复**:
- 所有 HID/媒体事件状态使用 `ACTION#seq`, 例如 `PLAY#12`、`NEXT_TRACK#13`。
- `homeassistant.event` 的 `usage` 必须拆回 `#` 前的原始动作名, 并额外发送 `seq`。
- 新增或修改媒体控制入口时, 不要直接 `id(ble_hid_event_text).publish_state("PLAY")`; 必须递增 `hid_event_seq` 后发布带序号状态。

改动文件: `智能前级蓝牙2.0.yaml` (globals/text_sensor/media/HID 事件发布)

**45. 网页控制 POST 不要触发 OPTIONS 预检**

命令行 curl 带 body 的 POST 可以 200, 但浏览器若因为 `Content-Type` 或自定义 header 发起
CORS/Private Network Access 的 `OPTIONS` 预检, ESPHome 2026.7 `web_server_idf`
可能返回空回复, 表现为"curl 能控, 本地网页按钮不能控"。

**修复**:
- `sendRequest()` 的 body 使用 `Uint8Array([49])`, 既有 `Content-Length`, 又不主动设置 `Content-Type`。
- 不要给网页控制请求添加自定义 header; 需要调试时用设备 `/events` 和浏览器控制台判断是否走了预检。
- 如果再次改网页请求层, 必须同时验证: 空 POST 返回 411、非空 POST 返回 200、OPTIONS 不能作为成功路径依赖。

改动文件: `www/index.html`

**46. 网页媒体控制必须使用瞬时 button 入口**

`select/媒体控制 Media` 可以保留为状态显示和旧接口兼容, 但它不是可靠的瞬时按钮。
连续设置同一个 option 时, ESPHome/前端状态去重可能让后续动作不稳定。

**修复**:
- 固件提供 `button/媒体播放 Play`、`button/媒体暂停 Pause`、`button/媒体下一首 Next`、`button/媒体上一首 Prev`。
- 4 个 button 共用 `emit_media_action`, 每次 press 都发布 `ACTION#seq` 到 `BLE HID 按键事件`。
- `www/index.html` 媒体按钮只有在 `/events` 已发现对应 button 实体 id 后才调用 `/button/<name>/press`; button 不存在或未广播时必须直接回退到 `select/媒体控制 Media`。
- button 探测/兼容入口失败不要先弹 `IP/跨源权限` 错误, 避免旧固件未 OTA 时误导排查。
- `/events` 后续精简状态包可能没有 `name/domain`; `mapEntity()` 必须合并实体信息, 不允许用空 `name` 覆盖已知实体, 否则会出现 `未找到实体: media`。

改动文件: `智能前级蓝牙2.0.yaml` (script/button/select), `www/index.html`

**47. 本地 HTML 控制页必须保留 form POST 兜底**

直接双击打开 `www/index.html` 时, 浏览器可能拦截 `file:// -> http://192.168.x.x`
的 `fetch`/Private Network Access 请求。命令行 curl 返回 200 不代表本地 HTML 一定能发出同样请求。

**规则**:
- `www/index.html` 的控制请求必须默认使用隐藏 iframe + form POST, 不能先依赖 `fetch`/`no-cors` 成功判断。
- `file://` 和本地 HTTP 打开时都优先用 form POST, 因为控制命令只需要发送, 不需要读取跨源响应。
- 网页 SSE 未连接时只能提示连接状态, 不能用全屏遮罩或 `pointer-events: none` 阻断控制按钮; curl 已能控制时, 用户仍应能从网页直接发送命令。
- 普通 CORS/no-cors 代码只作为未来同源场景备用, 不能作为本地控制台的首选发送通道。
- 这类问题优先改网页发送层, 不要误判为固件 REST 实体失效。

改动文件: `www/index.html`

**48. 防爆音渐变期间必须锁定 target_db**

`anti_pop_fade_to_silence` 运行期间, 其他入口可能继续触发 `send_volume_to_pga`
或 `soft_mute_switch.turn_off_action`。如果这时按普通音量重算 `target_db`, 软降目标会从
`-96dB` 被抢回当前音量, 最后表现为 5 秒超时后才 PGA=0/硬切, 防爆音等于失效。

**规则**:
- `send_volume_to_pga` 在 `anti_pop_fade_to_silence->is_running()` 时必须强制保持 `target_db=-96.0f`。
- 防爆音软降未完成时, 取消静音只能保持 `soft_mute=true` 并同步开关状态, 不能提前打开硬件静音或恢复音量。
- 实机日志若出现 `fade timeout before hard mute` 且 `target_db` 不是 `-96.0f`, 优先检查是否有入口抢写了 `target_db`。

改动文件: `智能前级蓝牙2.0.yaml` (soft_mute_switch/send_volume_to_pga)

**49. MSGEQ7 弱信号显示均衡与自动跳转抗抖**

v2.1.36 已确认 DAC 输入下 MSGEQ7 不再全 0, 但 `frame_peak=9~17` 直接画到 78/88px 高的 LVGL bar 时只有几像素, 实机仍像没有频谱。
**规则**:
- 驱动侧继续保持 raw 域底噪扣除与 `SIGNAL_FULL_SCALE_RAW=128`; 不要为了“看得见”再粗暴调大驱动全局缩放, 否则会影响信号判定和 LED。
- UI 绘制前才做显示专用视觉均衡: 无信号由 MCP gate + 静默重校准归零, 有信号时 6.25k/16k 也必须按弱信号低门槛显示, 避免最后两段完全不跳。
- 自动频谱页跳转使用未放大前的 `s_signal_avg`, 当前阈值为 `>=2`, 但 `s_signal_avg` 必须同时考虑七段平均值和单频段峰值, 并保留 5 秒弱信号保持窗口, 防止弱信号瞬间掉到底后重新计时。
- `msgeq7_bands` 七段明细日志只能保留 DEBUG 级; 验证时可临时调 INFO, 最终固件不能刷屏。
- 如果以后实机又说“有声音但没频谱”, 先看 `/events` 中静音/待机/背光/输入状态, 再临时把 `msgeq7_diag` 调到 INFO 验证 raw/frame, 不要先删缓存。
改动文件: `智能前级蓝牙2.0.yaml` (频谱 interval / 自动跳转)

**50. 频谱视觉、蓝表头和输入信号 LED 职责分离**

v2.1.39 起频谱页面的“好看”和自动判定继续分离: 自动跳转/信号判定仍看未放大的 raw frame,
屏幕绘制才做音量联动和视觉增益。

**规则**:
- 频谱柱显示可以跟随 `volume_val/max_volume` 做视觉缩放, 但不得把音量缩放后的值写回 `s_signal_avg` 或自动输入/自动跳转逻辑。
- 自动跳转不得只看七段平均值; 实机弱信号可能表现为 `raw_avg=0/1` 但 `raw_peak=4~6`, 这时仍应通过峰值联合判定维持稳定计时。
- 频谱显示层必须有独立时间常数平滑和峰值衰减; 不要把每帧整数值直接硬写到 LVGL bar, 否则实机跳动会不丝滑。
- 无信号底噪由 MCP `audio_*` gate 和静默重校准处理, 不要再靠把 `visual_floor` 压到很高来消隐。当前输入检测无信号时频谱显示必须归零; 所有输入无信号稳定后可自动 `msgeq7::recalibrate()` 更新静默基线。
- 16kHz/6.25kHz 的有信号显示门槛必须按实机弱信号幅度调, 不能高到 `raw_peak=1~15` 时完全不动; 若高频柱长期偏高, 优先看 `SPECTRUM_DIAG_RAW diff/offset` 和 MCP gate, 再调显示侧 `visual_floor/visual_gain_q8/visual_knee`。
- 频谱显示映射必须保留软膝压缩, 不要回到纯线性大倍率放大; 线性放大会让弱信号或残留值轻易顶满, 看起来不像自然频谱。
- 火花/流光频谱样式不能再加人工 `flicker` 抖动; 柱高必须来自真实七段频谱插值, 否则会看起来不像按频率跳动。
- 蓝表头 VU 指针不要只用七段平均值, 应使用平均值 + 峰值并保持快起慢落; 但电平权重必须低于频谱柱显示层, 当前约为 `avg*60%+peak*40%`, 避免指针长期顶在高位。
- 蓝表头指针使用每声道 40 个 2x1 密集细 `obj` 段 + 6px 轴心, 按 225°~315° 直接计算 x/y 坐标, 模拟麦景图表头的一根细长黑针; 不依赖 `LV_USE_LINE`、`LV_USE_CANVAS` 或 `transform_angle`。旧旋转指针对象保留但应隐藏, 不要再回到 -42°~+42° 水平线摆法。
- v2.1.45 的频谱视觉是中等软膝压缩: 不能再把 6.25k/16k 的 floor/gain 压到 v2.1.43 那种几乎不跳的程度。
- 频谱样式必须通过 `select/频谱样式 Spectrum` 暴露到 Web/HA, 选项值要和设置页 Row 12 一致; 远程确认蓝表指针时先看该 select 是否为“蓝表VU”。
- 频谱底噪/门限调试必须先打开 `switch/频谱诊断日志 SpectrumDiag`, 对比 `mcp_active/gate/idle_cal`、`rawL/rawR`、`targetL/targetR`、`drawL/drawR` 以及 `SPECTRUM_DIAG_RAW diff/offset`; 不要只看屏幕跳动就继续调 `visual_floor`。
- 主页面输入卡片上的 `led_src_0~3` 是 MCP23017 输入信号状态灯: `audio_cd/dac/pc/aux` 有信号就常亮, 无信号就灭。不要再用 MSGEQ7 `s_signal_avg` 做呼吸闪动。
- `msgeq7_bands` 仍只用于 DEBUG 诊断, 不要为了观察视觉效果把正式固件调成 INFO 刷屏。

改动文件: `智能前级蓝牙2.0.yaml` (频谱 interval / VU / 输入卡片 LED)

**51. 频谱响应优先快攻, 顺滑交给 gravity 回落**

v2.1.45 的双层平滑让频谱看起来顺, 但实机反馈低音出来后柱条慢半拍。
诊断时如果 `SPECTRUM_DIAG targetL/targetR` 已经变化, 但 `drawL/drawR` 还长时间停在旧高度,
说明问题在显示层运动模型, 不要继续改 MSGEQ7 底噪或自动跳转阈值。

**规则**:
- MSGEQ7 驱动层允许较快攻击, 让真实瞬态尽快进入 `frame.left/right`; 当前 `SMOOTH_UP=0.72`, `SMOOTH_DOWN=0.22`。
- UI 显示层主柱使用 fast attack + per-band gravity fall: 上升要跟拍, 下降用速度累积保持顺滑。
- `s_signal_avg`、自动跳转和输入卡片 LED 仍只能使用未视觉放大的 raw/框架值或 MCP 状态, 不得使用 gravity 后的 `draw` 值。
- `LED点阵` 样式参考 audioMotion LED bars 的视觉, 使用 24 条窄列、量化高度和绿/黄/橙/红分区; 为了实机稳定, 不新增几百个 LVGL cell 对象。
- audioMotion-analyzer 是 AGPL-3.0-or-later, 本项目只借鉴视觉/算法思想, 不直接复制其源码。
- 调高柱条可调显示侧 `visual_gain_q8/visual_knee` 和主页小频谱高度, 不要改 `RAW_NOISE_FLOOR`、`NOISE_GATE` 或 `SIGNAL_FULL_SCALE_RAW`。

改动文件: `智能前级蓝牙2.0.yaml` (频谱 interval / LED点阵 / 主页小频谱), `PGA/msgeq7.h` (MSGEQ7 平滑系数)

**52. 频谱“跟不上音乐”先区分驱动层和显示层**

v2.1.46 实机反馈 LED点阵好看很多, 但仍不像按音乐速度跳动。
`/events` 播放音乐时 `target/draw` 已接近, 说明显示层 gravity 不是主要滞后点,
应优先检查 MSGEQ7 驱动平滑和单列视图的数据源。

**规则**:
- 如果 `target` 与 `draw` 已经接近, 不要继续只加 LVGL attack; 先看 `PGA/msgeq7.h` 的驱动层 `SMOOTH_UP/SMOOTH_DOWN`。
- 当前 MSGEQ7 显示用平滑为 `SMOOTH_UP=1.00`, `SMOOTH_DOWN=0.42`; 自动跳转仍不得使用显示层放大或 gravity 后的数据。
- `LED点阵`、主页小频谱这类单列/单声道视觉应使用 `max(frame.left, frame.right)` 后插值, 不要用左右平均 `frame.combined`, 否则单边瞬态会被打平。
- 这条只改变频谱视觉响应, 不改 `RAW_NOISE_FLOOR`、`NOISE_GATE`、`SIGNAL_FULL_SCALE_RAW`、输入 MCP gate、防爆音和网页控制。

改动文件: `智能前级蓝牙2.0.yaml` (频谱 interval / LED点阵 / 主页小频谱), `PGA/msgeq7.h` (MSGEQ7 平滑系数)

**53. 输出模式切换比输入切换需要更长静音包络**

直通/变压器 `relay_out` 直接改变后级输出路径, 比输入源继电器更容易产生触点弹跳、
变压器磁化或直流瞬态。即使已经走 `anti_pop_fade_to_silence`, 也不能照搬输入切换的
30ms/100ms 窗口。

**规则**:
- `switch_output_mode` 必须保持顺序: anti_pop 软降 -> `pga2311::set_volume(0,0)` -> 软件静音预稳定 -> 切 `relay_out` -> 长等待 -> 音量渐变恢复。
- 当前输出模式切换参数: PGA=0 后预稳定 150ms, `relay_out` 切换后软件静音保持 450ms, 再等待 120ms 执行 `send_volume_to_pga`。
- 切 `relay_out` 后要再次固定 `current_db/target_db=-96.0f` 并写 PGA=0, 不要只依赖前一次软降的状态。
- 如果实机仍有输出模式爆音, 优先继续调 `switch_output_mode` 的软件静音保持窗口, 不要先改输入切换、MSGEQ7、网页控制或 BLE HID。

改动文件: `智能前级蓝牙2.0.yaml` (switch_output_mode)

**54. 硬件静音只属于开机保护时序**

用户确认: 静音只有在开机时序使用硬件静音, 其他场景只用 PGA2311 软件静音。

**规则**:
- `mute_switch` / MCP23017 GPA6 只允许在开机保护时序中拉低保护, 启动时序结束必须释放为开声状态。
- 日常 `soft_mute_switch`、待机/唤醒、输入切换、直通/变压器输出切换、温度保护恢复都不得再打开硬件静音。
- 运行时静音统一使用 `anti_pop_fade_to_silence`、`soft_mute=true`、`current_db/target_db=-96.0f` 和 `pga2311::set_volume(0,0)`。
- 如果未来又出现爆音, 优先调 PGA 软件静音包络和继电器保持窗口, 不要把 `mute_switch` 重新加回运行路径。

改动文件: `智能前级蓝牙2.0.yaml` (boot, soft_mute_switch, enter_standby, exit_standby, switch_input, switch_output_mode, temp_protect)

**55. 待机呼吸灯必须保留历史修复波形**

IO16 面板 LED 由 50ms interval 直接控制, 不经过 light entity。

**规则**:
- 待机 1 小时内使用 4s 周期 `sin²` 呼吸波形, 逻辑亮度 `2% -> 65% -> 2%`, 两端斜率归零避免最低点突变。
- 待机 1 小时后使用 5 分钟一次、2 秒宽的 `sin²` 平滑脉冲, 逻辑亮度 `0% -> 30% -> 0%`。
- LED 输出必须每 50ms 直接写入, 不要用 `last_lvl` / `fabs(lvl-last_lvl)` 阈值滤波; 阈值会让波形低点和慢变区不连续。
- 写入 PWM 前保留 `lvl * lvl` gamma 近似校正, 对应历史修复 `ff0ec2c breathing: fix float precision, >1h pulse truncation, gamma correction`。
- 这条只管 IO16 面板 LED, 不影响 TFT 背光、输入卡片 LVGL LED、频谱 LED 点阵或音频状态机。

改动文件: `智能前级蓝牙2.0.yaml` (LED interval)

**56. v2.1.51 外部控制入口和编译警告清理规则**

本条来自代码审核 2/3/4/5/6/7 项修复, 只处理外部控制、网页入口、频谱信号判定和可修编译警告。

**规则**:
- HA/API `set_input` 和 Web `select/输入源选择 Input` 一样, 目标输入等于 `current_input` 时必须跳过 `switch_input`, 只同步 select/UI, 防止同一路输入重复吸合继电器。
- `www/index.html` 是唯一正式本地控制台; `网页控制/index.html` 只能作为跳转到 `../www/index.html` 的兼容入口, 不得再维护第二套控制逻辑。
- 本地网页控制请求默认使用隐藏 iframe + form POST, `fetch`/`no-cors` 只作为后备路径; fetch body 必须保持 `Uint8Array([49])`, 不要改回空 body、字符串空值或自定义 header。
- 修改网页控制逻辑、默认 IP 或固件控制入口时, 必须同步提升 `WEB_VERSION` 和页面显示版本, 当前为 `WEB v2.1.51`, 便于确认浏览器没有加载旧缓存。
- 静音或视觉消隐期间, MSGEQ7 的 `s_signal_avg` 仍要从实时 frame 更新, 并用七段平均值 + 单频段峰值 + 当前输入 MCP gate 联合判定; 不要退回 `sum/7` 平均值单判定。
- 清理 ESPHome 2026.7 编译警告时优先处理无风险项: 避免 `uint8_t > 255` 这类永假比较、中文 `snprintf` 小缓冲区、枚举 `switch` 缺少 `default`。未使用函数警告可保留, 因为部分是调试/未来入口。
- 本条不修改 BLE HID 驱动、不修改输出模式防爆音时序、不删除 `.esphome`/PlatformIO/ESPHome 缓存。

改动文件: `智能前级蓝牙2.0.yaml`, `www/index.html`, `网页控制/index.html`

**57. v2.1.52 保留 API 的 unused warning 处理方式**

`PGA/*.h` 中部分函数是测试、诊断或后续维护入口, 当前固件主路径暂时不用, 但不要为了清理 warning 直接删除。

**规则**:
- 对确认需要保留的头文件级 `static` 辅助 API, 使用标准 C++ `[[maybe_unused]]` 标记, 让编译器知道这是有意保留。
- 不要删除 `pga2311` 的测试/诊断接口、`msgeq7` 的单频段/ready 查询接口、`ble_hid_host` 的配对/学习/raw 调试接口来压 warning。
- 如果未来把这些接口接入 YAML、调试页或单元测试, 可以保留 `[[maybe_unused]]`; 该属性不改变调用行为。
- 这条只处理项目头文件 warning, 不改 ESPHome 生成目录或 `.esphome`/PlatformIO/ESPHome 缓存。若 warning 来自 ESPHome managed component 自身, 优先记录来源, 不直接改生成缓存。

改动文件: `PGA/pga2311.h`, `PGA/msgeq7.h`, `PGA/ble_hid_host.h`

**58. v2.1.53 待机必须释放所有音频继电器**

历史功能: 待机时音频继电器不能继续吸合。v2.1.9 曾在 `enter_standby` 释放输入继电器, 后续重构丢失; 当前硬件还包括 GPA4 输出继电器。

**规则**:
- `enter_standby` 必须先完成 anti-pop 软降并写 `pga2311::set_volume(0,0)`, 再释放 `relay_in1~relay_in4` 和 `relay_out`。
- 如果设备开机恢复在 `standby=true`, boot 初始化也必须强制释放 GPA0~GPA4, 不得因为 `output_mode=true` 吸合 GPA4。
- 待机期间修改输出模式只能保存 `output_mode` 目标状态, `switch_output_mode` 不能在 standby 中吸合 `relay_out`。
- `exit_standby` 必须在输入继电器接通和音量恢复前, 按 `output_mode` 恢复 `relay_out`; 输入继电器仍由 `switch_input` 恢复当前输入。
- 这条只管音频继电器释放/恢复顺序, 不改变 PGA2311 渐变、硬件静音 boot-only 策略、BLE HID、网页控制或 MSGEQ7。

改动文件: `智能前级蓝牙2.0.yaml` (boot relay init, enter_standby, exit_standby, switch_output_mode)

**59. v2.1.54 恢复出厂必须从待机起按并先进入音频安全态**

恢复出厂是破坏性操作, 只能从待机状态开始长按编码器 10 秒触发。工作状态开始长按只能进入待机, 不得在继续按住后恢复出厂, 也不得显示会误导用户的 5 秒恢复出厂提示。

**规则**:
- 10 秒触发必须使用按下瞬间记录的 `started_from_standby`, 不要改成读取当前 `standby`; 1.5 秒长按可能先唤醒屏幕。
- 5~10 秒屏幕提示和 LED 加速闪烁也必须只在待机起按路径出现。
- `factory_reset` 脚本开始时必须停止音量/输入/输出切换脚本, 设置 `current_db/target_db=-96.0f`, 写 `pga2311::set_volume(0,0)`, 并释放 `relay_in1~relay_in4` 与 `relay_out`。
- v2.1.49 起运行期仍不得操作 `mute_switch`; 恢复出厂前音频安全态只用 PGA2311 软件静音和继电器释放。

改动文件: `智能前级蓝牙2.0.yaml` (firmware_version, factory_reset, 恢复出厂 10 秒检测, LED interval)


## 强制性规则

**每次修改代码后，必须同步更新以下项目文档：**
  1. `CHANGELOG.txt` — 添加新版本/修改条目，写明改动内容、原因、涉及文件
  2. `AGENTS.md` — 如果涉及架构变更、新增功能、新硬件配置，必须更新对应章节
  3. 其他文档 — 如果 README.md 或 CLAUDE.md 有相关内容，一并更新

> 任何代码改动（包括修复 bug、新增功能、重构、UI 调整）都必须有对应的文档更新。这是强制性要求，不可跳过。

---

# 智能前级2.0 — 项目概览

## 项目简介

基于 ESP32-S3 + ESPHome 的 Hi-Fi 音频前级放大器。具备 4 路输入切换 (CD/DAC/PC/AUX)、PGA2311 音量控制、MSGEQ7 七段频谱分析、2.79 寸 TFT 彩屏显示 (LVGL)、MCP23017 I2C GPIO 扩展、温度保护等功能。

**固件版本:** v2.1.54
**MCU:** ESP32-S3 @ 240MHz
**框架:** ESPHome 2026.7.0 + LVGL v9.x managed component
**仓库:** https://github.com/oupengopu/zhitong-preamp-2

---

## 硬件引脚分配

### PGA2311 音量芯片 (SPI)
| 信号 | GPIO | 说明 |
|------|------|------|
| CS | IO42 | 片选 (手动控制) |
| CLK | IO41 | SPI 时钟 (4MHz) |
| SDI (MOSI) | IO2 | 数据输入 |
| SDO | N/C | 不接 |

协议: CS 拉低 → 发 16bit (高8位=右声道, 低8位=左声道) → CS 拉高。值范围 0~255 (0=静音, 192≈0dB, 每步 0.5dB)。

### MSGEQ7 频谱分析器
| 信号 | GPIO | ADC通道 |
|------|------|---------|
| RESET | IO1 | — |
| STROBE | IO39 | — |
| R_OUT (右声道) | IO3 | ADC1_CH2 |
| L_OUT (左声道) | IO9 | ADC1_CH8 |

频段: 63Hz / 160Hz / 400Hz / 1kHz / 2.5kHz / 6.25kHz / 16kHz

### NTC 温度传感器
| 信号 | GPIO | ADC通道 |
|------|------|---------|
| NTC | IO10 | ADC1_CH9 (6dB衰减) |

电路: 3.3V → 10kΩ(上拉) → ADC → NTC(9.4kΩ@25°C) → GND  
NTC 参数: B=3950, 参考电阻 9.4kΩ@25°C

### TFT 显示屏 (2.79", NV3007 驱动)
| 信号 | GPIO |
|------|------|
| SPI MOSI | IO4 |
| SPI SCLK | IO5 |
| DC | IO6 |
| RESX | IO15 |
| BLK (背光) | IO7 (PWM) |
| CS | GND (常选) |

分辨率: 428×142 (实际像素), 渲染 2x 缩放 → 856×284

### 编码器
| 信号 | GPIO |
|------|------|
| A | IO17 |
| B | IO18 |
| D (按键) | IO8 (20ms 消抖) |

型号: EC11E18244A5，36 定位 / 18 脉冲。ESPHome `rotary_encoder` 使用 `resolution: 2`，使每个机械定位点对应 1 个软件步进。

### LED 指示灯
| 信号 | GPIO |
|------|------|
| LED | IO16 (PWM, LEDC 1kHz) |

电路: GPIO16 → 1K → NPN基极, 基极10K下拉到GND, 集电极100R→LED阴极, LED阳极→+5V  
三极管: MMBT3904 (SOT-23), 开关速度 ns 级, 1kHz PWM 绰绰有余

### I2C 总线 (MCP23017)
| 信号 | GPIO |
|------|------|
| SDA | IO38 |
| SCL | IO40 |
| INTA | IO11 |
| INTB | IO12 |

### MCP23017 扩展 (地址 0x27)
| 引脚 | 功能 | 备注 |
|------|------|------|
| GPA0 | 继电器1 (CD输入) | GPA0=继电器1, GPA1=继电器2, GPA2=继电器3, GPA3=继电器4 |
| GPA1 | 继电器2 (DAC输入) | |
| GPA2 | 继电器3 (PC输入) | |
| GPA3 | 继电器4 (AUX输入) | |
| GPA6 | 硬件静音 | **高电平=开声, 低电平=静音**; v2.1.49+ 仅开机保护使用 |
| GPB0 | 音频检测 CD | GPB0=CD, GPB1=DAC, GPB2=PC, GPB3=AUX |
| GPB1 | 音频检测 DAC | |
| GPB2 | 音频检测 PC | |
| GPB3 | 音频检测 AUX | |
| INTA | 中断输出 Port A | 接 ESP32-S3 IO11 (继电器, 不产生中断) |
| INTB | 中断输出 Port B | **接 ESP32-S3 IO12 (音频检测输入中断, YAML interrupt_pin)** |

### 未使用的 GPIO
| GPIO | 说明 |
|------|------|
| IO45 | MSGEQ7 RESET旧引脚 (1K下拉) |
| IO46 | GND 启动模式 (不接) |

---

## 项目文件结构

```
智能前级2.0/
├── 智能前级蓝牙2.0.yaml    # 主配置文件 (~5220行, ESPHome + LVGL UI)
├── PGA/                   # C++ 驱动头文件目录
│   ├── pga2311.h          # PGA2311 SPI 驱动 (C++ namespace)
│   ├── msgeq7.h           # MSGEQ7 频谱 + NTC 温度驱动 (C++ namespace)
│   ├── ble_hid_host.h     # BLE HID Host 蓝牙遥控器驱动 (C++ namespace)
│   ├── lvgl_compat.h      # LVGL C API 声明补齐 (opaque lv_obj_t 兼容)
│   ├── settings_ui.h      # 设置页 UI 控件指针数组管理
│   └── remote_keys.h      # BLE 遥控器按键映射动作名查询
├── secrets.yaml           # WiFi/API/OTA 密钥
├── AGENTS.md              # 本文件
├── CHANGELOG.txt          # 变更历史
├── fonts/                 # 字体文件
│   ├── Montserrat-Regular.ttf
│   ├── NotoSansSC-Regular.ttf
│   └── Roboto-Regular.ttf
├── icons/                 # SVG 图标 (18 个)
├── preview/               # Web 预览模拟器
│   ├── index.html         # 2x 缩放 TFT 模拟器 (HTML/CSS/JS)
│   └── .Codex/launch.json
├── .esphome/              # ESPHome 编译缓存
└── 资料/                  # 硬件文档 (PDF 数据手册等)
```

---

## 软件架构

### 核心模块

1. **PGA2311** (`PGA/pga2311.h`) — SPI 音量芯片驱动
   - ESP-IDF SPI3_HOST, 4MHz, mode 0
   - 去重写入 (仅值变化时发送)
   - CS 手动控制, 16-bit 传输 (R 高8位, L 低8位)

2. **MSGEQ7** (`PGA/msgeq7.h`) — 频谱分析 + NTC 温度
   - ADC1 oneshot API (ESP-IDF 5.x), 12-bit
   - 双速率平滑 (上升 0.35, 下降 0.12)
   - 帧率无关峰值衰减 (powf 解耦)
   - 零漂校准, 噪声门限, 频段增益补偿
   - 线程安全: 临界区保护共享数据, `get_frame()` 快照读取
   - SpectrumFrame: combined[7], left[7], right[7], peak[7], peak_l[7], peak_r[7], temperature

3. **LVGL 兼容层** (`PGA/lvgl_compat.h`)
   - ESPHome managed component 使用 opaque lv_obj_t (PIMPL)
   - extern "C" 声明所需 LVGL C API (label/bar/led 函数)
   - 非复合 widget 直接是 lv_obj_t* 全局变量, 无需 `->obj_`

4. **ESPHome YAML** — 主配置 (~5700 行)
   - 4 个核心 LVGL 页面: splash_page, main_page, spectrum_page, settings_page
   - 3 个子页面: ble_remote_page(蓝牙遥控), remote_keys_page(遥控器按键映射), debug_page(诊断信息)
   - 页面切换: 主页面/频谱/设置/蓝牙遥控/遥控器按键/诊断信息
   - 音频检测 (MCP23017 GPB0~GPB3, active-low + inverted, 100ms 确认 / 500ms 断连)
   - 输入自动切换 (CD > DAC > PC > AUX 优先级, 支持手动覆盖)
   - 编码器: 旋转→音量, 单击→页面循环, 长按→待机
   - 显示超时自动调低亮度 (可配置 1~60 分钟, 不熄屏)

### 关键全局变量
| 变量 | 类型 | 说明 |
|------|------|------|
| `volume_val` | int | 当前音量 (0~255) |
| `current_input` | int | 当前输入 (0=CD, 1=DAC, 2=PC, 3=AUX) |
| `max_volume` | int | 最大音量限制 (10~255) |
| `power_on_limit` | int | 开机音量上限 (10~255) |
| `balance` | int | 当前输入的平衡 (-20~20) |
| `input_bal_0~3` | int | 四路输入独立平衡槽 (CD/DAC/PC/AUX, -20~20) |
| `soft_mute` | bool | 软静音标志 |
| `standby` | bool | 待机状态 |
| `current_db` | float | 当前实际 dB |
| `target_db` | float | 目标 dB (渐变更新的目标) |
| `theme` | int | 颜色主题索引 (0~7) |
| `spectrum_style_select` | select | Web/HA 频谱样式选择, 与设置页 Row 12 同步 |
| `spectrum_diag_log` | bool | 频谱诊断日志开关状态, 默认关闭, 开启后每秒输出 mcp/gate/raw/target/draw/diff/offset |
| `display_timeout_min` | int | 显示超时分钟数 |
| `display_brightness` | int | 显示亮度 (0~100) |
| `last_manual_input_ms` | uint32_t | 手动选择输入的时间戳 (0=自动模式) |
| `system_ready` | bool | 系统就绪标志 |
| `switching_input` | bool | 正在切换输入 |
| `audio_cd/dac/pc/aux` | binary_sensor | 各输入音频检测状态 |
| `settings_focus_idx` | int | 设置页焦点索引 (0~11) |
| `settings_active_idx` | int | 设置页调节模式索引 (-1=浏览模式) |
| `keymap_focus_idx` | int | 遥控器按键页焦点索引 (0-7=动作映射, 8=学习按键, 9=重置学习, 10=返回) |
| `keymap_active_idx` | int | 遥控器按键页调节模式索引 (-1=浏览, ≥0=调节中) |
| `key_learn_select_idx` | int | 学习按键行当前选择的源按键动作 (1~8) |
| `key_learn_pick_mode` | bool | 学习按键行的选择模式标志 |
| `key_map_1~key_map_8` | int | 8 个标准按键的动作映射 (NVS 持久化, 默认恒等) |
| `key_map_9~key_map_14` | int | 6 个扩展键(SWIPE/CAMERA)的动作映射 (NVS, 默认恒等) |
| `learned_key_1~8_handle/len/hash` | int/uint32_t | BLE 遥控器 8 个核心源按键的 raw 指纹学习表 (NVS) |
| `key_learn_waiting` | bool | 遥控器按键页学习模式等待状态 |
| `key_learn_target` | int | 当前正在学习的源按键动作 (1~8) |
| `ble_remote_scanning` | bool | BLE 遥控扫描中 |
| `ble_remote_connected` | bool | BLE 遥控已连接 |
| `ble_remote_device_count` | int | 发现的 BLE 设备数 |
| `ble_remote_focus_idx` | int | BLE 遥控页焦点索引 |
| `ble_remote_peer_bda` | std::string | 已配对 BLE 遥控 BDA (NVS 持久化) |
| `ble_remote_battery` | int | BLE 遥控电池电量 (-1=未知) |
| `ble_remote_scan_status` | std::string | BLE 遥控页状态文字 |

### 关键脚本
| 脚本 | 说明 |
|------|------|
| `switch_input` | 切换输入: 软静音→切继电器→取消静音 |
| `apply_theme` | 应用主题色到 LVGL 控件（LED/分隔线/音量条/频谱条/设置页 bar/主题色点） |
| `auto_switch_input` | 自动输入选择 (8s 等待, 优先级 CD>DAC>PC>AUX, 无信号则保持) |
| `display_idle_timer` | 显示超时管理 (可配置分钟数, 调低亮度而非关屏) |
| `exit_standby` | 退出待机: 渐变亮度 (800ms) → 应用主题 → 恢复音量 → 自动切换 |
| `factory_reset` | 恢复出厂: 重置全局变量 → 显示提示 → 清除 WiFi → 重启 (编码器按住 10s 触发) |

### 颜色主题 (8 种，受世界名机启发)
| 索引 | 名称 | 颜色 | 灵感来源 |
|------|------|------|---------|
| 0 | 翠绿 | 0x10B981 | 高保真经典绿 |
| 1 | 赤红 | 0xEF4444 | 贵丰 (Gryphon) 雄狮红 |
| 2 | 冰蓝 | 0x7DD3FC | 冷光冰蓝 |
| 3 | 紫色 | 0x8B5CF6 | 马克莱文森 (Mark Levinson) 高贵紫 |
| 4 | 麦景图蓝 | 0x0071E3 | 麦景图 (McIntosh) 表头蓝 |
| 5 | 金嗓子金 | 0xCD9B4A | 金嗓子 (Accuphase) 香槟金 |
| 6 | 孔雀青 | 0x0F766E | 深孔雀蓝绿，高级冷调且与冰蓝明显区分 |
| 7 | 南瓜橙 | 0xF97316 | 南瓜 (Nagra) 仪表橙 |

---

## 核心功能逻辑

### 频谱分析 (50ms interval)
- 调用 `msgeq7::read()` 读取 MSGEQ7 七段频谱
- 调用 `msgeq7::read_ntc()` 读取 NTC 温度 (带中值滤波 + IIR)
- 使用 `msgeq7::get_frame()` 获取 SpectrumFrame 快照
- 更新 LVGL 频谱条 (L/R 独立, 含 peak 保持线; 显示层按音量联动放大, 时间常数平滑, 不影响信号判定)
- 更新 VU 电平条/蓝表头指针 (平均值 60% + 峰值 40%, 40 段 2x1 密集细长指针, 225°~315° 表盘角度, 快起慢落)
- 更新输入卡片 LED 信号灯 (MCP23017 `audio_cd/dac/pc/aux` 有信号常亮, 无信号熄灭)
- 约 20Hz 刷新率

### 频谱页自动切换 (基于 MSGEQ7 信号)
- 取代旧版 MCP23017 引脚电平检测，使用 MSGEQ7 真实频谱信号
- **主页面 → 频谱页**: MSGEQ7 信号存在且稳定 ≥15 秒后自动跳转
- **频谱页 → 主页面**: 信号消失 ≥10 秒后自动返回
- 静音/待机/系统未就绪时不自动跳转
- 设置页/子页面不受影响
- 编码器单击手动切换不受影响（频谱→主页、主页→设置）

### 音量渐变 (50ms interval)
- 当前 dB 向目标 dB 逼近 (每步 ~0.78dB, 约 4 步/秒)
- 写入 PGA2311 芯片 (R/L 独立, 平衡偏移)
- 更新 LVGL 音量条/数值

### 温度保护 (50ms interval)
- 触发温度: ≥72°C → 强制待机保护
- 恢复温度: <55°C 才能退出保护
- 每 10 秒读取一次 NTC (间隔计数)

### 输入自动切换
- 优先级: CD(0) > DAC(1) > PC(2) > AUX(3)
- 手动选择后: 保持当前输入直到信号丢失, 然后恢复自动切换
- 待机恢复/重启后: 自动启动

### LVGL 页面结构
| 页面 | 内容 |
|------|------|
| splash_page | 开机闪屏: "Zhitong Audio" 品牌标识 (montserrat_36, 翠绿) + "智能前级2.0" 副标题 + 版本号, 文字 500ms 渐显 |
| main_page | 状态栏(输入/自动切换/温度/音量/图标) + 大音量 + 输入选择 + 音量条 + 平衡/静音指示 + BLE遥控电池条 |
| spectrum_page | 左右声道 7 段频谱条 + 峰值保持线 + VU 电平条 + 频率标签 + 输入/温度 |
| ble_remote_page | 蓝牙遥控管理: 扫描/设备列表/连接/断开/电池电量/HID调试 |
| remote_keys_page | 遥控器按键映射: 8 个标准源按键 + 学习/重置/返回，浏览/调节双模式，单击循环切换映射目标(0-15) |
| settings_page | 单列可滚动列表，15 项：最大音量/开机上限/.../频谱跳转/频谱样式/固件版本(只读)/IP地址(只读) |
| debug_page | 诊断信息: CPU占用率、内部堆(320KB)/PSRAM(8MB)实时监控、低水位标记、运行时间 |

---

## 操作说明

### 编码器旋转
| 当前页面 | 效果 |
|---------|------|
| 主页 | 调节音量（屏幕休眠时自动恢复亮度，不再弹出大字音量浮层） |
| 频谱页 | 调音量 + 自动返回主页 |
| 设置页（浏览模式） | 移动焦点切换设置项（无限循环） |
| 设置页（调节模式） | 调整当前设置项的值（bar/数值实时更新） |
| BLE 遥控页 | 移动焦点：扫描按钮/设备槽(动态)/返回按钮（无限循环，跳过隐藏行） |
| 遥控器按键页（浏览） | 移动焦点（11 行循环：0-7 动作映射, 8 学习按键, 9 重置学习, 10 返回） |
| 遥控器按键页（调节） | 0-7 行旋转循环切换映射目标；第 8 行旋转选择要学习的源按键 |

### 编码器按键（单击）
| 当前状态 | 效果 |
|---------|------|
| 屏幕休眠 | 恢复亮度 |
| 主页 | → 进入设置页 |
| 频谱页 | → 返回主页 |
| 设置页（浏览模式） | → 进入调节模式（bar 高度 6→12px，背景更亮 0x25374F）；只读项(固件版本/IP地址)无反应；蓝牙遥控行→跳转 BLE 遥控页；遥控器按键行→跳转 remote_keys_page |
| 设置页（浏览模式, 固件版本行） | 快速单击 3 次 → 跳转 debug_page（500ms 窗口） |
| 设置页（调节模式） | → 退回浏览模式（bar 恢复 base 高度，确认修改） |
| BLE 遥控页 | → 激活焦点项：扫描/停止扫描，连接设备，断开（留在蓝牙页），返回设置 |
| 遥控器按键页（浏览, 0-7 行） | → 进入动作映射调节模式（当前行背景变亮 0x25374F） |
| 遥控器按键页（浏览, 学习按键行） | → 进入学习目标选择模式；旋转选择音量+/音量-/静音/播放暂停/下一曲/上一曲/电源/切换输入，再单击开始等待实体遥控器按键 |
| 遥控器按键页（浏览, 重置学习行） | → 清空 8 个实体遥控 raw 学习指纹，不改动作映射 |
| 遥控器按键页（调节） | → 退出调节模式（确认修改，背景恢复） |
| 遥控器按键页（返回行） | → 返回设置页 |

### 编码器按键（双击）
| 当前页面 | 效果 |
|---------|------|
| 主页 / 频谱页 | 切换静音 |
| 设置页 / 蓝牙遥控页 / 遥控器按键页 / 诊断页 | 无动作 |

### 编码器按键（长按 1.5s）
| 状态 | 效果 |
|------|------|
| 设置页 | 返回主页面 |
| 蓝牙遥控页 | 返回主页面 |
| 遥控器按键页 | 返回主页面 |
| 诊断页 | 返回主页面 |
| 主页面/频谱页（工作状态）| 进入待机 |
| 待机状态 | 唤醒（800ms 渐变亮度）|

### 编码器按键（超长按 10s — 恢复出厂设置）
| 条件 | 效果 |
|------|------|
| 待机状态开始按住 5 秒 | 设备可能先唤醒屏幕，然后显示橙色提示"继续按住恢复出厂设置..."，LED 加速闪烁 |
| 待机状态开始按住 10 秒 | 先 PGA2311 写 0 并释放全部音频继电器，再执行恢复出厂：重置所有设置到默认值 → 显示"已重置，请连接热点" → 清除 WiFi 记忆 → 重启进入 AP 配网模式 |
| 工作状态开始按住 | 不触发恢复出厂；1.5 秒长按仍按正常逻辑进入待机 |
| 中途松手 | 取消操作，隐藏提示 |

> 恢复出厂会重置以下设置到默认值：音量(160)、输入源(CD)、各输入独立平衡(0)、最大音量(255 PGA全量程)、开机上限(192 PGA 0dB)、息屏超时(5分钟)、亮度(80%)、主题(翠绿)、自动输入切换(开)、各输入独立音量(160)、按键映射(恒等: 1-9→1-9)。

### LED 指示灯状态 (IO16, PWM)
| 状态 | LED 行为 | 亮度 | 说明 |
|------|----------|------|------|
| 正常开机 (WiFi已连) | 常亮 | 70% | 正常工作指示 |
| 静音中 | 常亮 | 35% | 半亮区分静音状态 |
| 未连 WiFi | 闪烁, 1s 周期 | 70% ↔ 0% | 500ms 亮 / 500ms 灭 |
| 开机闪屏 | 渐亮 | 0% → 70%, 1秒渐变 | 配合开机仪式感 |
| 待机 <1小时 | 呼吸灯, 4s 周期 | 2% ↔ 65% (gamma校正前) | sin² 柔呼吸, 两端斜率归零 |
| 待机 >1小时 | 5分钟平滑脉冲, 2s 宽 | 0% → 30% → 0% (gamma校正前) | sin² 深夜省电脉冲 |
| 温度保护 (≥72°C) | 快闪, 200ms 周期 | 80% ↔ 0% | 警示异常 |
| 工厂重置按住 5~10s | 加速闪烁 | 80% ↔ 0%, 500ms→100ms | 按住越久闪越快 |

优先级: 温度保护 > 工厂重置(5s+) > 待机 > 闪屏渐亮 > 未连WiFi > 静音 > 正常

### 显示超时
- 无操作后（默认 5 分钟，设置页可调 1~60 分钟）背光调至 50% 微光
- **不熄屏** — 保持显示内容可见，维持高级音响质感
- 旋转编码器或单击恢复用户设定亮度
- 设置项名称为"息屏超时"，实际为调低亮度而非关屏
- 代码位置: `display_idle_timer` 中 `call.set_brightness(0.50f)`。不要改回 10%/3%，实机机箱内太暗看不清

### 音量调节
- 主页旋转编码器直接调节音量，不再弹出大字音量浮层
- 频谱页旋转编码器调节音量后自动返回主页
- 四路输入使用独立音量槽 `input_vol_0~3`。调音量时保存当前输入；切换输入时先保存 `active_input`，再加载目标输入音量
- 四路输入使用独立平衡槽 `input_bal_0~3`。调声道平衡时保存当前输入；切换输入时跟随 `active_input` 保存旧输入平衡，再加载目标输入平衡
- `send_volume_to_pga` 内的安全保存必须避开 `switching_input`，否则切输入淡出阶段会把旧输入音量写入新输入槽，表现为"每个输入独立音量失效"

### 自动输入切换
- 优先级：**CD > DAC > PC > AUX**
- 当前输入信号丢失后，等待 **8 秒**看是否恢复（曲目间隙不误切）
- 8 秒内信号恢复 → 保持当前输入
- 8 秒后仍未恢复 → 按优先级切换到有信号的输入
- **所有输入均无信号 → 保持当前不动**
- 手动选择输入后保持该通道，信号丢失后恢复自动切换
- 待机恢复/重启后自动启动

### 温度保护
- ≥72°C 强制待机，屏幕显示红色 **"保护!"**
- <55°C 退出保护，恢复正常显示

### 待机
- 彻底关屏 + 静音 + 暂停 LVGL，WiFi 保持连接
- 唤醒时 800ms 渐变亮度恢复 → 应用主题 → 恢复音量 → 自动切换输入

### 开机闪屏 (Splash Screen)
- 上电后立即显示 splash_page: 中央 "Zhitong Audio" 品牌标识 (翠绿 0x10B981)
- **文字渐显动画**: 首帧微光 15/255 → 500ms 内平滑升至全亮
- **500ms 二次 show_splash_boot 是真机白屏兜底**: NV3007/背光初始化偶发白屏时需要重新显示 splash_page，并重置 fade_frame；不要再按"避免闪一下"删除
- 最短展示 **3 秒**，WiFi 连接就绪后以 **FADE_IN 300ms** 过渡到主页面
- 若 WiFi 10 秒内未连接，自动超时切到主页面（不卡死）
- splash_page 仅开机显示一次，过渡后不再使用

### 设置页交互（单列列表，图标化，15 项）
- **布局**：单列可滚动列表，每行 ~90px，含 24px MDI 图标 + 18px 标签 + 进度条(bar) + 16px 数值
- **图标**：每行左侧 24px 彩色 MDI 图标，rows 0-12 跟随主题色，rows 13-14 固定灰色 `0x6B7280`
- **焦点指示**：左边界 3px 主题色竖条 + 背景色 `0x1E2D42`
- **只读行(固件版本/IP地址)**：无焦点条，背景 `0x111827`，图标灰色
- **导航行(蓝牙遥控/遥控器按键)**：背景 `0x111827`，支持焦点高亮 + 点击跳转对应子页面
- **行高 ~90px → 约 3 行可见，超出需滚动**

**浏览模式**（`settings_active_idx = -1`）：
- 旋转编码器 → 移动焦点（无限循环）
- 自动滚动（`lv_obj_scroll_to_view`）确保焦点项可见
- 单击可调项(0-7、10-12) → 进入调节模式；导航项(8-9) → 跳转子页面

**调节模式**（`settings_active_idx = idx`）：
- 选中行 bar 高度 12px (row 0 为 16px) → 24px，背景 `0x25374F`
- 旋转编码器 → 调值，bar/数值实时更新，立即生效
- 单击 → 退回浏览模式（bar 恢复 base 高度：row 0=16px, rows 1-5=12px）
- 只读项(13,14) 单击无反应

**设置项一览**（15 项）：
| # | 名称 | 图标 | 控件 | 范围 | 说明 |
|---|------|------|------|------|------|
| 0 | 最大音量 | mdi-volume-high | bar | 10~255 | 音量上限，保护扬声器 |
| 1 | 开机上限 | mdi-restart | bar | 10~255 | 开机时不超过的值 |
| 2 | 声道平衡 | mdi-tune-vertical | bar | -20~20 | 映射到 0~40 |
| 3 | 输入选择 | mdi-swap-horizontal-bold | label | CD/DAC/PC/AUX | 循环切换 |
| 4 | 输入自动切换 | mdi-swap-horizontal | label | 自动/手动 | 开关自动输入切换 |
| 5 | 屏幕亮度 | mdi-brightness-6 | bar | 0~100 | 实时生效 |
| 6 | 息屏超时 | mdi-timer-outline | bar | 1~60 分钟 | 调低亮度而非关屏 |
| 7 | 主题色彩 | mdi-palette | label+色点 | 8 种主题色 | 循环切换，实时生效 |
| 8 | 蓝牙遥控 | mdi-bluetooth | label | 已连接/未连接 | 导航到蓝牙遥控页 |
| 9 | 遥控器按键 | mdi-keyboard-settings | label | 已映射 | 导航到遥控器按键映射页 |
| 10 | 输出模式 | mdi-swap-vertical-bold | label | 变压器/直通 | 切换输出模式 |
| 11 | 频谱跳转 | mdi-chart-timeline-variant | label | 禁用/5~60秒 | 主屏自动跳转频谱屏时间 |
| 12 | 频谱样式 | mdi-chart-bar | label+色点 | 6 种样式 | 经典柱状/镜像频谱/LED点阵/示波线/蓝表VU/星点火线 |
| 13 | 固件版本 | mdi-information-outline | label (只读) | — | 显示当前固件版本，三击进入诊断页 |
| 14 | IP地址 | mdi-ip-network | label (只读) | — | 显示设备 IP |

### 蓝牙遥控 (BLE HID Host)
- 图标 mdi-bluetooth，标签 "蓝牙遥控"，右侧状态 "已连接"/"未连接"
- **状态栏指示**：主页面右上角双蓝牙图标——大号 `mdi_icons_27` 为手机 APP BLE Server 连接指示，小号 `mdi_icons_12` + 微型电池条（14×7px bar）为 BLE 遥控器连接及电量（耳机电量风格：>50% 主题色、>20% 黄色、<20% 红色）
- 点击 → 跳转 `ble_remote_page`（设置页 Row 8）

### 遥控器按键映射 (Remote Keys Page)
- 图标 mdi-keyboard-settings，标签 "遥控器按键"，右侧 "已映射"
- 点击 → 跳转 `remote_keys_page`（设置页 Row 9）
- **8 个源按键**：音量+/音量-/静音/播放暂停/下一曲/上一曲/电源/切换输入
- **专门学习行**：新增"学习按键"行，负责给 8 个源按键录入实体遥控器 raw HID 指纹；旧的 8 个源按键行只负责选择动作映射，不再承担学习入口
- **重置学习行**：新增"重置学习"行，只清空 8 个 raw HID 指纹，保留 8 个动作映射；便宜遥控器二义性码较多，学习混乱时优先用这一项清表后重学
- **动作映射交互**：旋转选中 0-7 行 → 单击进入调节 → 旋转循环 16 个映射目标 → 单击确认
- **学习交互**：旋转选中"学习按键"行 → 单击进入选择模式 → 旋转选择要学习的源按键 → 再单击进入"学习中" → 按一次 BLE 实体遥控器按键 → 保存 raw HID 指纹；成功显示"已学习: <键名>"约 1.5 秒
- **映射目标**：禁用/音量+/音量-/静音/播放暂停/下一曲/上一曲/电源/切换输入/上划/下划/左划/右划/拍照/切换镜头/确定
- 映射值 NVS 持久化，工厂重置恢复恒等
- raw 学习表同样 NVS 持久化，工厂重置会清空；运行时只查学习表，匹配不到直接忽略，不再走内置 `_parse_report()` 兼容逻辑

### BLE 遥控页
| 行 | 内容 | 说明 |
|----|------|------|
| 0 | 标题 "蓝牙遥控" | 翠绿 18px font_cn |
| 1 | 连接状态 | "未连接" / "扫描中..." / "已连接: xxx 电池: XX%" |
| 1.5 | 电池电量 | 独立行显示百分比，<20% 红色 |
| 2 | 扫描按钮 | 点击开始/停止扫描 |
| 3-5 | 设备槽 (最多3个) | 显示名称 + RSSI，点击连接 |
| 6 | 原始 HID 事件调试 | 连接时显示最近一次 HID 报告 (Page/Usage/Val) |
| 7 | 断开/返回按钮 | 已连接→断开（留在蓝牙页），未连接→返回设置页 |

---

## BLE HID Host 物理遥控器

基于 `PGA/ble_hid_host.h` (namespace `ble_hid_host`)，ESP-IDF Bluedroid GAP + GATT Client，ESP32-S3 同时运行 BLE Server（已有）+ BLE Client（双角色）。

### BLE HID 自学习模式（当前固件规则）
- 当前固件的实体遥控动作只使用自学习 RawFingerprint 表。HID notify 到来后先记录原始报文供学习页使用，再调用 `_match_learned_key(fp)`；匹配不到学习表就直接忽略，不再走内置标准码/兼容码/二义性码预设。
- 旧版 `[80 00]`、`[40 00]`、0x0027 坐标包、Consumer Usage 等预设码仅作为历史调试资料保留；不要再把它们直接映射成音量、静音、上一首/下一首等动作，否则会重新引入“静音短按先加音量”“长按减音量变静音”等冲突。
- 学习入口在“遥控器按键”页的专用学习行中选择动作，再按实体遥控对应按键保存。原先每个动作行双击学习的方案已废弃。

### 架构要点
- 头文件 only，静态函数，风格同 `PGA/pga2311.h`/`PGA/msgeq7.h`
- FreeRTOS 队列桥接 BLE 回调线程 → ESPHome 主循环
- BDA 持久化到 NVS（`ble_remote_peer_bda`，`restore_value=true`）
- 断开自动重试 3 次，间隔逐次增大
- 待机时 BLE 连接保持，遥控器电源键可唤醒

### BLE 遥控器配对流程
1. 设置页 → 点击 "蓝牙遥控" → 进入 ble_remote_page
2. 点击 "开始扫描" → 10s 主动扫描 → 列表显示发现的 HID 设备
3. 旋转编码器选择设备 → 单击连接 → "正在连接..." → "已连接: <设备名>"
4. BDA 自动保存，下次开机自动重连

### 兼容设备
标准 BLE HID 设备（服务 UUID 0x1812），如蓝牙媒体遥控器、自拍遥控器、PPT 翻页器。**不支持**经典蓝牙 (Bluetooth Classic) 遥控器。

### BLE HID 遥控器键位映射

代码中所有码带标注: `[标准]`=USB HID 规范定义, `[兼容]`=廉价遥控器实测有效, `[自定义]`=特定遥控器。
以下码表仅用于识别旧日志/调试资料，当前固件不再直接使用这些预设码触发动作；实体遥控必须先在“遥控器按键”页自学习后才会生效。

#### Consumer 报文 (2/3字节, `uint16_t usage`, Usage Page 0x0C)
| Usage ID | 标注 | 功能 | 事件 |
|----------|------|------|------|
| 0xE9 | 标准 | 音量+ | `HID_EVT_VOLUME_UP` |
| 0xEA | 标准 | 音量− | `HID_EVT_VOLUME_DOWN` |
| 0xE2 | 标准 | 静音 | `HID_EVT_MUTE` |
| 0xCD | 标准 | 播放/暂停 | `HID_EVT_PLAY_PAUSE` |
| 0xB5 | 标准 | 下一曲 | `HID_EVT_NEXT_TRACK` |
| 0xB6 | 标准 | 上一曲 | `HID_EVT_PREV_TRACK` |
| 0x30 | 标准 | 电源 | `HID_EVT_POWER` |
| 0x233 | 自定义 | 切换输入 | `HID_EVT_CYCLE_INPUT` |

#### Keyboard Boot 报文 (8字节, `bytes 2-7 uint8_t key`, Usage Page 0x07)
| Key Code | 标注 | USB 实际含义 | 功能 | 事件 |
|----------|------|-------------|------|------|
| 0xE9,0x80 | 兼容 | Consumer 码借壳 | 音量+ | VOLUME_UP |
| 0xEA,0x81 | 兼容 | Consumer 码借壳 | 音量− | VOLUME_DOWN |
| 0xE2,0x7F | 兼容 | Consumer 码借壳 | 静音 | MUTE |
| 0xCD | 兼容 | Consumer 码借壳 | 播放/暂停 | PLAY_PAUSE |
| 0xB5 | 兼容 | Consumer 码借壳 | 下一曲 | NEXT |
| 0xB6 | 兼容 | Consumer 码借壳 | 上一曲 | PREV |
| 0x30,0x66 | 兼容+标准 | Kbd 3 / Kbd Power | 电源 | POWER |
| 0x68 | 标准 | F13 | 切换输入 | CYCLE_INPUT |
| 0x28 | 标准 | Enter | 确定 | OK |
| 0x2C | 标准 | Space | 拍照(自拍快门) | CAMERA |
| 0x52,0x4B | 标准 | ↑ / PageUp | 上划 | SWIPE_UP |
| 0x51,0x4E | 标准 | ↓ / PageDown | 下划 | SWIPE_DOWN |
| 0x50 | 标准 | ← | 左划 | SWIPE_LEFT |
| 0x4F | 标准 | → | 右划 | SWIPE_RIGHT |
| 0x8C | 自定义 | International1 | 切换镜头(预留) | CAMERA_SWITCH |

> ⚠️ Keyboard `0x30` = 数字键 '3'，标准键盘按 3 会误触电源事件。这是廉价遥控器兼容性代价，`ble_hid_host.h` L270 保留此映射。Consumer 报文的 `0x233` = 563 (>255) 仅 `uint16_t` 可容纳，Keyboard 报文用 `0x68`(F13) 替代。

防抖 30ms，全零释放帧忽略，长按时反复触发。新增事件 SWIPE_UP(9)~OK(15) 默认仅日志记录 + HA 推送，可通过遥控器按键页映射为音量/静音等有效动作。

#### 实测蓝牙遥控器特殊报文
- raw 学习模式使用 `handle + len + FNV-1a(data)` 作为指纹，只支持 8 个核心源按键 (1~8)，不会替代完整 HID Report Map 解析器
- 学习时过滤全零/释放帧；学习成功后清空当前 HID 队列，避免学习那一下同时触发功能
- `[40 00]` 是二义性报文: 单发忽略，900ms 内连续重复才按音量加处理，避免静音短按附近的杂帧误加音量
- `[80 00]` 是二义性报文: 短按/单发按静音处理，连续重复/长按按音量减处理
- `[80 00]` 后面的 `[00 00]` 释放帧**不能立即判定为静音**，否则长按音量减会被误触发成连续静音
- 旧版 `pending_ambig80_mute` / `suppress_power_until_ms` 逻辑已废弃；当前不再用预设码直接触发实体动作
- `0x0027` 十字/触摸类报文中，`[00 33 0B AC 04 ...]` 为下一首，`[00 CD 04 AC 04 ...]` 为上一首，仅作为旧日志识别资料；实际控制必须先自学习
- 长按音量加/减结束时遥控器可能发出 0x0027 的伪电源包；自学习模式下未学习电源键时不会误待机

### 三路同步机制
编码器旋钮、BLE 遥控器、HA 三路都汇聚到同一个 `volume_val` + `send_volume_to_pga`：

```
编码器旋转 → volume_val += diff → volume_number→publish_state() → HA
                                  → send_volume_to_pga → PGA2311 芯片
                                                        → volume_number→publish_state() → HA

BLE 遥控器 → volume_val++ → send_volume_to_pga → PGA2311 芯片
                                                → volume_number→publish_state() → HA

HA 滑条   → volume_number.set_action → volume_val 转换 → send_volume_to_pga → PGA2311
```

`send_volume_to_pga` 脚本尾部同时发布 `volume_number`、`balance_number`、`media_volume` 到 HA，确保所有状态同步。

### 新增功能（对比 fsievers22/esphome-ble-remote）
1. **电池电量读取** — 发现 Battery Service (0x180F) + Battery Level characteristic (0x2A19)，CCCD 注册通知 + 初始读取，在 ble_remote_page 显示百分比
2. **HID 原始事件调试查看器** — NOTIFY_EVT 中捕获 `_last_raw_page/_usage/_value`，`has_raw_event()` / `get_last_raw_event_string()` 供 YAML lambda 和 LVGL 显示
3. **Home Assistant 事件推送** — `ble_hid_event_text` text_sensor，`on_value` → `homeassistant.event: esphome.hid_events`，按键数据随事件推送

---

## 编译和运行

### 配置验证（先验证再编译）
```bash
esphome config 智能前级蓝牙2.0.yaml
```

### 编译
```bash
esphome compile 智能前级蓝牙2.0.yaml
```

### 上传
```bash
esphome run 智能前级蓝牙2.0.yaml
```

### Web 预览 (TFT 模拟器)
```bash
cd preview && py -3.11 -m http.server 8084
```
然后在浏览器打开 `http://localhost:8084`，显示 2x 缩放 (856×284) 的 TFT 模拟。

### 预览服务器配置
`.Codex/launch.json` 中配置了 preview-server，可通过 `preview_start` 工具启动。

---

## 项目核心

### ⚠️ `send_volume_to_pga` — 本项目最核心的脚本
此脚本是整个固件的**心脏**，v1.7.2 已经过多轮论证和优化。**绝对禁止修改此脚本**，任何对此脚本的改动（如移除 while 循环、改为 interval 步进等）都会破坏音量控制的稳定性和时序。

> **设计说明**: 此脚本使用 `while` + `- delay: 15ms` 进行音量渐变。
> **脚本中的 `- delay:` 并非阻塞** — ESPHome 的脚本引擎基于异步协程（Coroutine），执行到 `- delay: 15ms` 时调度器会释放 CPU 并 `yield` 给主循环，WiFi 栈、BLE 通信和 `interval: 50ms` 在此期间仍会正常运转。
> 
> **为何观察到的 interval 执行会延迟？** 根本原因不是主循环被阻塞，而是在 while 循环中高频度（每 15ms 一次）抢占 SPI3 总线调用 `pga2311::set_volume()`，同时 `interval: 50ms` 也在读取 ADC 和刷新 LVGL。这种高频异步任务交替引发了**CPU 缓存抖动（Cache Thrashing）**以及 ESPHome 内核任务队列的拥挤，导致 interval 的实际调度时间点被推迟。
>
> **结论**: 此脚本当前的设计是安全且经过验证的，不会触发 Watchdog 或断开 WiFi。文档保留"禁止修改"的警告，但纠正对阻塞机制的解释。

---

## 关键注意事项

1. **freetype-py 中文路径问题**: Windows 下 `FT_New_Face` 无法加载含中文的绝对路径。修复方案是回退到 `FT_New_Memory_Face` (读取文件到内存再加载)。此补丁需在 freetype-py 升级后重新应用。

2. **LVGL opaque 类型**: ESPHome 2026 managed component 的 lv_obj_t 是 opaque 类型。非复合 widget (bar/label/led/obj) 的 `id()` 直接返回 `lv_obj_t*`，不需要 `->obj_` 间接访问。复合 widget (dropdown/roller) 返回 `LvDropdownType*` 等类型, 需要先强转 `(lv_obj_t*)`。所有 LVGL C API 函数需在 `lvgl_compat.h` 的 `extern "C"` 块中声明。

3. **MCP23017 中断配置**: 配置为 Open-Drain + Active-Low (`open_drain_interrupt: true`)，ESP32-S3 端用内部上拉，防电平冲突。**音频检测输入端建议加硬件 RC 低通滤波**——音频信号临界点的高频抖动会引发中断风暴。若测试中遇到"某路输入突然不检测"的现象，疑似 MCP23017 中断锁死（INT 保持低电平不复位），需 I2C 读取 GPIO/INTCAP 寄存器手动清除。

4. **ESP32-S3 NTC 校准**: 使用 `ADC_ATTEN_DB_6` + `curve_fitting` 校准方案。6dB 衰减虽量程较小 (0~2.5V) 但线性远优于 12dB, 对室内前级足够。

5. **MSGEQ7 时序**: 3.3V 供电时输出建立需 36-40μs (vs 5V 的 18μs), STROBE 低脉冲和 RESET 脉冲均已放宽到 40-100μs。

6. **音频检测反转**: MCP23017 的音频检测 binary_sensor 配置为 INPUT_PULLUP, 且有 `inverted: true`（检测到信号时引脚被拉低）。

7. **继电器映射**: 继电器1(CD)->GPA0, 继电器2(DAC)->GPA1, 继电器3(PC)->GPA2, 继电器4(AUX)->GPA3。

8. **MCP23017 地址**: 0x27, I2C bus_a (IO38/IO40)。

9. **PGA2311 SPI 时钟**: 4MHz (数据手册最高 6.25MHz, 留余量)。

10. **YAML 文件名含中文字符**: 主配置文件 `智能前级蓝牙2.0.yaml` 含中文与空格。ESPHome/ESP-IDF 工具链在 Windows 下可能遇到路径编码问题（如 freetype-py 的 FT_New_Face 无法加载含中文路径）。git 操作时注意文件名编码，编译时使用完整路径。

11. **standby_switch 命名约定**: HA 实体名"待机模式"，ON=待机模式，OFF=设备运行中。`turn_on_action` → 进入待机，`turn_off_action` → 退出待机。新增待机相关逻辑时严格遵守此约定。

12. **硬件静音 GPA6 极性**: 必须牢记硬件是 **高电平=开声, 低电平=静音**。固件里的 `mute_switch` 使用 `inverted: true`，让 HA 的"静音-HW"开关语义变成 ON=输出低电平=静音，OFF=输出高电平=开声。v2.1.49 起 `mute_switch` 只属于开机保护时序, 日常静音/待机/切换只用 PGA2311 软件静音。以后修改静音逻辑时不要把 `switch.turn_on` 当成开声, 也不要把硬件静音重新加回运行路径。

13. **font_cn_small 字体覆盖**: 作为 `default_font`，任何新增 UI 文字（尤其是 BLE 页状态文本）必须确保字符已加入 glyphs 列表（当前约 90 字）。缺字导致 LVGL 渲染空白框。此字体与 font_cn (18px) 独立维护，需分别添加。

14. **YAML 嵌套 if/then/else 缩进陷阱**: ESPHome YAML 中 `- if:` 的 `condition:`/`then:`/`else:` 必须同缩进层级（比 `- if:` 多 4 空格）。`else:` 比 `condition:` 少 2 空格会导致解析器将 `else:` 和后续 `- if:` 误识别为同一 action 条目的两个 key，报错 "Cannot have two actions in one item. Key 'if' overrides 'else'!"。修复时注意 `else:` 本身 + 其下方整个子块的缩进联动。

15. **`remote_keys::get_action_name()` 返回类型**: 必须返回 `std::string` 而非 `const char*`。ESPHome 的 `text: !lambda` 代码生成器会在返回值上自动调用 `.c_str()`，对 `const char*` 再调 `.c_str()` 无效（编译报错 "request for member 'c_str' in ... which is of non-class type 'const char*'"）。直接传给 `lv_label_set_text()` 时需要手动 `.c_str()`。

16. **pga2311.h 与 msgeq7.h 变量风格统一**: 两者均使用 `inline` 变量（C++17 ODR 安全），禁止使用 `static`。`static` 会在多翻译单元场景下产生独立副本，导致 `_last_r/_last_l` 去重失效和 SPI 设备重复初始化。

17. **MSGEQ7 零漂校准前提假设**: 校准在上电时执行，假设此时无音频输入。如果用户先开音响再开前级，校准值会被污染导致频谱幅度偏低。无运行时恢复机制（代码注释已承认）。未来可加 HA 服务或设置页按钮触发重新校准。

18. **send_volume_to_pga 并发边缘场景**: BLE 遥控器和编码器同时触发时，`mode: restart` 丢弃首次调用，音量响应可能延迟约 300ms。当前可接受，属已知限制。

19. **BLE reconnect_retries 溢出保护**: 已加 `if (> 1000) = 4` 防溢出帽。正常运行不超过 3，长期断线才累积。

20. **lvgl_compat.h extern "C" 版本脆弱性**: ESPHome 大版本升级（如 2026→2027）时 LVGL API 签名变化会导致链接期而非编译期崩溃。建议大版本升级后逐一核对 `extern "C"` 块中的函数签名与 ESPHome 内置 LVGL 头文件一致。

21. **主页输入卡片显示职责分离**: `current_input` 只控制输入卡片外框和选中背景；未选中卡片也必须保留灰色外框（当前约 `LV_OPA_40`），不能完全透明。MCP23017 音频检测只控制图标、名称、`SIGNAL/IDLE` 明暗；MSGEQ7 真实频谱只控制当前卡片里的小 LED/条和呼吸搜索信号。不要因为当前输入被选中就点亮图标或 `SIGNAL`。

22. **固件版本隐藏入口**: 设置页焦点停在"固件版本"行时，连续单击 3 次进入诊断页。代码使用 `firmware_hidden_click_count` 做普通单击计数兜底，不再只依赖 `on_multi_click` 的三击匹配；后者在实机上可能被单击/双击规则先消耗。

23. **诊断页布局**: 诊断页跟网页预览保持两列网格风格，标题整行，数据项左右两列，底部为"MSGEQ7 重校准 (单击执行)"与"返回设置"。新增中文文本时必须同步补 `font_cn_small` 与 `font_cn` glyphs，例如"双/中/占/闲/全/段/低/水/位/完/成/失/败"。

24. **独立输入音量保存边界**: 四路输入独立音量槽为 `input_vol_0~3`，运行态跟踪当前实际输出通道用 `active_input`。切输入流程中，调用方会先把 `current_input` 改成目标输入，所以 `switch_input` 必须用 `active_input` 保存旧输入音量，再用 `switching_target_input` 加载目标输入音量。`send_volume_to_pga` 内部的安全保存必须包含 `!id(switching_input)` 条件，避免切换过程把旧输入音量写进新输入槽。

25. **独立输入平衡保存边界**: 四路输入独立平衡槽为 `input_bal_0~3`，跟独立音量使用同一切换边界。调平衡入口必须调用 `save_current_input_balance(current_input, balance)`；`switch_input` 必须先用 `active_input` 保存旧输入平衡，再用 `switching_target_input` 加载目标输入平衡。不要改 `send_volume_to_pga` 核心渐变脚本来做存储，它只负责按当前 `balance` 计算左右声道。

26. **BLE `[80 00]` 二义性遥控码（历史逻辑，已废弃）**: 旧版曾用 `pending_ambig80_mute` 延迟判定来区分短按静音和长按音量减，但实机会出现误触发和串键。当前固件只允许自学习 RawFingerprint 触发实体遥控动作；不要恢复 `[80 00]`/`[40 00]`/0x0027 等预设码直连动作逻辑。

27. **暗屏亮度固定 50%**: `display_idle_timer` 的暗屏亮度应保持 `call.set_brightness(0.50f)`。早期 3%/10% 在实机机箱里不可读，后续不要再按旧变更记录改低。

---

## 已知编译器问题

### 1. ruamel.yaml font glyphs 合并 bug (ESPHome 2026.4.x ~ 2026.5.x)

当 `font:` 段中部分字体有 `glyphs:` 而部分没有时，ruamel.yaml 会将所有字体的 glyphs 合并到第一个字体条目中。这会导致 `Failed config` 警告（cosmetic，不阻断编译）。

**规避方法**：要么所有字体都有 `glyphs:`，要么全都没有。本项目采用：仅 MDI 字体有 `glyphs:`（需要子集化），Roboto/NotoSans/Montserrat 无 `glyphs:`（全字体编译，体积略大）。

### v2.1.0+ 音量迁移 (NVS)

v2.1.0 起 `volume_val` 直接映射 PGA2311 寄存器值 (0-255)，取代旧的百分比编码。旧固件升级时，启动脚本自动迁移：

```
新 reg = 旧 vol / 旧 max * 255
```

迁移通过 `v210_migrated` (NVS 持久化 bool) 保证只运行一次。迁移后 `max_volume` 设为 255，`power_on_limit` 设为 192。各输入源独立音量同步缩放。

### 2. switch-case 变量跨越 case (ESP-IDF 5.5 + GCC)

在 `switch` 语句的 `case` 内声明变量（如 `int x = 0`）时，如果后续 `case` 没有用 `{}` 包围，GCC 会报 `crosses initialization of` 错误。

**修复方案**：每个声明变量的 `case` 都用 `{ }` 包围。

### 3. portENTER_CRITICAL 参数类型 (ESP-IDF 5.5)

`portENTER_CRITICAL()` 需要 `portMUX_TYPE*` 参数，不能传 `SemaphoreHandle_t`。`ble_hid_host.h` 中的 `dropped_events` 计数器已改为原子递增，不再使用临界区。

### 4. is_temperature_valid 前向声明

`msgeq7.h` 中 `is_temperature_valid()` 必须在 `read_ntc()` 之前声明，否则 GCC 报 `was not declared in this scope`。

### 5. guard_ok 跨 lambda 不可见

ESPHome YAML 中，`guard_ok` 在一个 `lambda:` 块中声明，不能在另一个独立的 `lambda:` 或 `if:` 条件中访问。应合并为单个 lambda 并使用 `id(script)->execute()` 内联执行。

---

## 远程编译

项目通常在远程 Docker 环境编译（ESPHome 官方 Docker 镜像）。编译命令：

```bash
# 复制 YAML 到 Docker (文件名可简化为英文)
cp 智能前级蓝牙2.0.yaml /config/esphome/20.yaml
esphome compile /config/esphome/20.yaml
```

> 注意：编译机无需本地字体文件，ESPHome 会自动从 Google Fonts 和 jsDelivr CDN 下载。

输出固件位置：
```
/data/build/zhitong-preamp-2/.pioenvs/zhitong-preamp-2/firmware.bin
```

### 关键注意事项

**30. 学习按键时自动重置 key_map 映射**

学习按键和 key_map 动作映射是两层独立机制:
- learned_key_N - NVS 持久化的实体遥控器 raw HID 指纹
- key_map_N - NVS 持久化的动作映射表（源按键 N 映射到哪个动作）

如果学习了一个按键（写入 learned_key_N）但 key_map_N 之前被意外修改（比如在遥控器按键页旋转编码器改变了映射值），就会导致学A得B的 bug。

**修复**: save_learned_remote_key 脚本在 set_learned_key() 之后自动重置对应的 key_map_N 到恒等值 (N)。这样无论之前映射页怎么调过，只要重新学习就强制同步。

**31. 学习按键时自动清除同指纹旧槽位**

_match_learned_key() 从 action=1 遍历到 9，返回第一个匹配的指纹。如果之前把一个实体按键学成 action 4（播放），后来又学成 action 9（切换输入），learned[4] 和 learned[9] 会有相同的 (handle, len, kind, value) 指纹，导致 action 4 永远先匹配，按出来永远是播放。

**修复**: set_learned_key() 在写入新槽位前，先遍历所有其他槽位，如果发现同指纹的旧槽位就清除。确保每个实体按键指纹唯一对应一个 action 槽。

改动文件: PGA/ble_hid_host.h，set_learned_key() 函数内。

**32. 中心键判定加趋势追踪保护**

上键的方向手势从 y=0x059A 运动到 y=0x1333 时，中间会经过 y=0x0614，这个坐标落在中心定区 (0x0666 ± 150) 内。_normalize_report 先检查中心区再检查追踪延续，导致上键中途的包被误判成中间键。

**修复**: 中心键判定条件加 !trend.tracking。已经在趋势追踪中的方向键跨过中心区时不再被误判为中心键。

改动文件: PGA/ble_hid_host.h，_normalize_report() 函数内 line 430。
**33. mdi_settings_icons_24 字体缺少图标码位**

学习页的"学习按键"、"重置学习"、"暂停"图标，以及信息页的"MSGEQ7 重校准"图标使用 `mdi_settings_icons_24` 字体（18px），但该字体 glyphs 列表缺少对应的 4 个 Unicode PUA 码位，导致显示空白。

**修复**: 在 `mdi_settings_icons_24` 字体的 `glyphs:` 列表中新增 4 个码位：
  - U+F03E4 (暂停图标)
  - U+F0652 (学习按键图标)
  - U+F0450 (重置学习图标)
  - U+F0415 (MSGEQ7校准/刷新图标)

改动文件: `智能前级蓝牙2.0.yaml` line 717，glyphs 列表。
