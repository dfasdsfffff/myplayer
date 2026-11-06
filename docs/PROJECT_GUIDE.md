# MyPlayer 开发指南

本文说明当前源码边界与排查入口，不代替构建、运行或性能验收报告。应用构建与成品打包见[根 README](../README.md)，公开 SDK 构建/API/状态表/线程契约见[SDK_README](../SDK_README.md)，SDK 实施验收记录见[方案](PLAYER_SDK_PLAN.md)。

## 1. 层次与依赖方向

| 层次 | 入口与职责 |
| --- | --- |
| MyPlayer | `apps/qt_player/main.cpp` → `MainWid`；播放列表、最近文件、设置、音轨/字幕选择 |
| MyPlayer Monitor | `apps/monitor/main.cpp` → `XViewer`；多路预览、摄像机配置、分段录像、检索及回放 |
| Qt SDK | `qt/include/player_sdk_qt/`；`QtPlayer` 负责 QObject 投递/生命周期，`VideoWidget` 负责统一画面和字幕显示 |
| Core SDK | `play_core/include/play_core/`；`Player` 是唯一受支持的 C++ 播放入口，别名 `PlayerSDK::Core` 对应静态 target `play_core` |
| 私有核心 | `play_core/src/`；解封装、解码、队列、同步、音频输出、网络输入/重连 |

两个应用均通过公开 Qt SDK 接入 Core，不再包含 Core 私有头文件。SDK 不反向依赖应用。旧 `PlaybackRuntime`、`PlaybackController` 仅为历史白盒测试保留，在仓库启用 `BUILD_TESTING` 时编入，嵌入/独立源码发行不编入这些入口；没有 `Player → PlaybackRuntime → Controller` 的 SDK 转发链。

`VideoCtl::MakeInstance()` 是私有独立实例工厂，不是单例。`RuntimeManager` 引用计数管理 SDL 音频和 FFmpeg 网络初始化；宿主先初始化 SDL 时，SDK 仅释放自身获得的 subsystem 引用。监控录像直接使用 FFmpeg，通过公开 `NetworkRuntime` lease 与 SDK 共存，不访问 `RuntimeManager`。

Core 依赖 FFmpeg、SDL2、SoundTouch 和 sigslot，不依赖 Qt/libass。Qt 组件依赖 Qt Widgets、libass 和 swscale；两个应用共用它。嵌入默认不构建 Qt、示例、测试或应用，不修改宿主 CRT、输出目录和全局编译选项。仓库开发 presets 显式开启应用及回归测试。

发行标签/软件包版本与源码内部版本仍需区分；SDK 首版声明 `1.0.0` 不代表既有 V1.1.0 桌面软件已重新发布。

## 2. 公开入口与私有职责

### 公开 API

- `play_core/player.h`：`Player`、`Request`、`OperationResult`、`PlayerEvent`、`Subscription`。
- `play_core/media_source.h`：UTF-8 输入、网络参数、独立于 FFmpeg 的轨道/媒体信息与 `Rational`。
- `play_core/video_frame.h`：只读 CPU 帧、像素/颜色信息、独立共享所有权、64 位毫秒 PTS、文本/位图字幕。
- `play_core/network_runtime.h`：宿主直接使用 FFmpeg 时可选的共享网络初始化 lease。
- `player_sdk_qt/qt_player.h`、`video_widget.h`：QObject 线程桥接、帧邮箱、可嵌入显示与字幕。

公开头文件不出现 FFmpeg、SDL、sigslot 或 `VideoCtl`。SDK 对外使用不可变帧快照；关闭和销毁不会使宿主保留的帧失效。

### 私有核心（以下文件均位于 `play_core/src/`）

| 文件/类型 | 职责 |
| --- | --- |
| `player.cpp` / `Player::Impl` | 串行受理/执行、session 与 operation、状态、订阅/派发、容量控制、原生数据到公开快照转换 |
| `videoctl.*` / `VideoCtl` | 具体会话生命周期、刷新循环、组件开关、同步调度、命令实际应用、seek 完成及重连 |
| `media_session.*` / `MediaSession` | 持有 `VideoState`，通过关闭回调释放会话 |
| `stream_reader.*` / `StreamReader` | 打开/探测、选流、解封装、packet 路由、seek、EOF 和输入/解码错误 |
| `decoder.*` / `Decoder` | codec、packet serial、send/receive 协议与线程回收，字幕使用 FFmpeg 字幕解码接口 |
| `decoder_workers.*` | 各轨解码并向 `FrameQueue` 发布 |
| `packet_queue.*`、`frame_queue.*` | packet 背压、帧队列、abort/flush、serial 与 keep-last |
| `audio_output.*`、`audio_render_queue.*` | SDL 设备、重采样/SoundTouch、PCM 生产、环形队列和回调消费 |
| `clock.*`、`media_sync.*` | 音频/视频/外部时钟、主时钟、显示延迟、同步和丢帧 |
| `video_frame_converter.*`、`renderer_dispatcher.*` | AVFrame 到私有 CPU 帧转换和派发，不创建窗口 |
| `network_input.*`、`reconnect_controller.*` | 输入选项/超时/取消、错误分类、脱敏及可取消退避 |
| `hardware_decode.*` | Windows D3D11VA 协商、硬件帧下载及软件回退策略 |
| `subtitle_dispatcher.*` | 位图/文字/ASS 独立数据复制，保留 ASS codec header |
| `playback_command_mailbox.*` | 历史控制命令邮箱；公开 SDK 命令另以受控闭包在刷新线程执行 |

`VideoState` 由 Session/Clock/Audio/Video/Subtitle/Filter 状态组成，不是全原子状态机。seek 使用 `seek_mutex`，组件访问使用 `trackMutex`，当前会话由 `m_streamMutex` 保护。修改字段时跟随现有锁、队列和所有权规则。

## 3. 应用适配与从打开到呈现

播放器 `MainWid` 组合窗口、菜单、列表、设置和应用本地 `AppPlaybackSession/AppPlaybackController`。这两个应用类只把已有 UI 命令、下一首/随机/单曲策略映射到公开 `QtPlayer`，不拥有解码器或另一套 SDK 状态机。`PlaybackRuntimeBridge` 保留历史 UI 信号名字，但仅做 **Qt 信号到应用展示模型** 的转换，不再订阅原生 sigslot 或自行创建工作线程。

`Show` 保留拖放、快捷键、音频占位提示和外置字幕入口，其子 `VideoWidget` 提供统一画面与字幕。`CtrlBar` 负责控制和进度；`Title` 负责标题；`Playlist/MediaList/PlaylistFile` 负责列表导航及 M3U/M3U8；`SettingWid/AppPreferences/GlobalHelper` 负责设置和持久化。

监控 `CoreVideoWidget` 基于同一个 `VideoWidget`，拥有独立 `QtPlayer`，不再维护 SDL 窗口、renderer、texture 或另一套轮询邮箱。静音预览使用 `MediaSource::enableAudio=false`，不是仍解码音轨后只把音量设零。`XPlayVideo` 使用其公开方法提供录像回放/暂停/seek/倍速；`XCameraRecord` 是独立录制路径，不从显示帧转码录像。

```text
两个应用 / 外部 Qt 宿主
  → QtPlayer → play_core::Player（串行控制线程） → VideoCtl
  → StreamReader（读取线程） → PacketQueue
  → DecoderWorkers（各轨线程） → FrameQueue

音频：sampq → AudioRenderQueue → 重采样/PCM → 有界环形队列
       → SDL 音频回调 → 设备/音频时钟
视频：pictq → 刷新线程按时钟选帧 → CPU 转换 → 公开不可变快照
       → Core 独立串行事件线程/最新帧邮箱
       → QtPlayer QObject 线程/最新帧邮箱 → VideoWidget QImage/QPainter
字幕：subpq → 时间选择 → 公开字幕事件 → VideoWidget（libass/位图/普通文字）
```

Qt 主线程不执行解封装或解码。每实例另有控制及事件线程；FFmpeg codec 和 SDL 也可能创建内部线程，不能写成固定“三线程”模型。

## 4. 打开、控制与结束

- `open()` 与 `play()` 分离：打开完成为 Ready，仍暂停；流组件打开不代表首帧呈现。可在 Opening 排队 play/pause/seek 等请求。
- 每个已接受请求有 session/operation，恰好一次终结通知；切换与 close 结清旧操作。帧/位置/字幕可覆盖，结果不覆盖。每实例请求预算 128，订阅预算 32；Qt 侧另保留预算直到 UI 真正消费结果。
- 快速切换时旧 session 的画面、状态、错误被过滤；旧 operation 的取消/被替代结果仍交付。close 后旧内部回调不能使状态回到 Playing/Failed。
- Core 普通控制可跨线程提交；Qt 方法仅在 QObject 线程调用。回调允许异步 close，禁止在自身回调中同步等待、销毁 Player 或注销自身订阅。
- 显式注销/订阅析构等待在途回调，返回后不会再访问宿主捕获上下文。Player 析构等待核心/派发退出；慢宿主回调不阻止核心取消，但可延迟销毁。
- seek 使用 64 位毫秒，完成代表解封装定位成功，不是附近首帧就绪；返回请求目标和解封装目标。定位后的关键帧/呈现位置可能不同。serial、queue flush、PCM generation 防止旧数据继续消费。
- `setPaused(bool)` 设置明确意图，刷新线程更新暂停/时钟；读取线程调用 `av_read_pause/play`。暂停 seek 保留核心的单步刷新行为。
- SDK `stop()` 保留当前媒体，仅暂停推进；可定位媒体下次 play 定位 0，不可定位直播恢复保留会话。释放资源使用 `close()`。旧私有 `OnStop/OnStopAndWait` 则用于取消/join/释放，不等于新 stop。
- 关闭先取消 I/O/退避，唤醒队列、停止输出并回收读取/解码/刷新线程。自然 EOF 排空音视频队列和 PCM 后产生 Ended；应用播放列表/随机/单曲循环留在应用。
- 工作者有异常边界；宿主回调异常被隔离。分配失败、系统资源耗尽的策略及合法状态表详见 SDK_README。

## 5. 音视频、网络与字幕边界

SDL 输出为 `AUDIO_S16SYS`。PCM 回调从有界环形队列读取、补静音、缩放音量并更新估算时钟，不在回调内阻塞解码或重采样。默认 `CONFIG_AVFILTER=0`，SoundTouch 位于当前重采样分支，不能从 UI 倍速选项推断所有格式下的音画同步已验证；公开倍速范围与内部统一为 `[0.1,2.0]`。

内部偶数尺寸 YUV420P 路径保留 AVFrame 引用，其他格式经 swscale 转 BGRA；公开 SDK 帧再复制成独立只读 CPU 数据，避免与可复用解码缓冲产生可变别名。Qt 显示使用 QImage/QPainter，YUV 通过 swscale 按颜色范围/空间转换，支持 SAR 和旋转；不再是 SDL GPU 纹理呈现，不能沿用旧性能结论。

D3D11VA 仍只在 Windows 实现，偏好不保证设备可用；硬件帧下载到 CPU，不是 GPU 零拷贝。PQ/HLG 仅标记，不提供完整 HDR 色调映射。运行中硬解下载失败不保证无缝软回退。

输入包含 probe/analyze、读/连接超时、HTTP user-agent/headers、RTSP TCP/UDP；打开/探测/读取使用独立 I/O deadline 和 interrupt cancellation。SDK 可取消重连退避，成功重连恢复之前暂停/播放意图，最终失败才发公开错误。`stablePlaybackReset` 仍是预留字段，尚无稳定播放后重置重连次数实现。

SDK 来源启用普通 packet 背压，不沿用旧实时源的 unlimitedBuffer 分支，避免暂停直播时无限堆积。不可定位直播恢复可能有有界缓存延迟，不承诺回到最新实时边缘。HTTP 不必然直播或可定位；能力来自实际 AVIO/媒体信息。

内嵌字幕现在将 FFmpeg 位图和文字格式均入队；ASS 包保留 codec header，由 Qt 组件按包时间渲染，普通文字及位图另行呈现。外部 ASS/SSA/SRT 由共享控件处理；SRT 仍是简化 UTF-8 解析，不承诺任意编码/扩展语法。播放器与监控共享字幕路径，Qt 显示使用核心毫秒位置，不从整数秒重建精度。

SDK 自身公开错误不返回原始带凭据 URL；`RedactMediaLocation` 保护自有诊断不代表覆盖所有 FFmpeg/宿主日志、配置或凭据，分享排查材料前仍须检查。

## 6. 持久化与录像（应用业务）

播放器配置位于 `QStandardPaths::AppConfigLocation/player_config.ini`，首次可迁移程序目录旧配置。保存窗口、列表、最近文件、播放位置和 `preferences`，由 `SanitizePreferences` 整理音量、倍速、循环、网络/硬解偏好。旧 `play/*` 键不是设置页唯一来源。

本地续播使用 canonical 路径 SHA-1 为键，进度标脏后合并保存；网络来源不建立本地续播目标。恢复需要可 seek、非直播、有效时长和 `ShouldResume`，接近结束按 `ShouldClearResume` 清理。列表/最近文件的持久化使用凭据过滤，不自动覆盖监控配置。

监控 `monitor-cams.db` 是相对工作目录的原生结构二进制文件，不是 SQLite；记录可能包含明文 URL 凭据，需按敏感配置保护，不是跨平台结构协议。录像来自摄像机 `save_path` 并追加索引目录；`XCameraRecord` 直接 FFmpeg packet remux 分段，不经 SDK 显示帧。

## 7. 测试与排查

- 公开 SDK：`tests/sdk`，测试受理/终结、生命周期、毫秒、UTF-8、慢回调/队列预算、异常、保留帧、SDL 共存；Qt 测真实画面、字幕、UI 卡顿、双向销毁。
- 外部宿主：`tests/consumer` 可复制出仓库测试 Core/Qt targets 和宿主构建隔离；`scripts/package-sdk.cmake` 验证最小源码发行。
- 私有管线：队列/时钟、decoder/reader、audio、hardware、seek/track、media compatibility、network integration；历史 Runtime 仅供这些白盒测试。
- 应用：设置/列表/隐私/主窗口、`show_widget_tests`（源码契约，不等于真实 UI）、监控 16 路预览/切换/销毁/分段录制和 smoke。
- 旧 SDL subtitle renderer 和邮箱仅保留为 `tests/support` 回归夹具，不参与应用或 SDK 显示。
- 修改打开/派发/状态从 `play_core/src/player.cpp` 和 `qt/qt_player.cpp` 入手；具体 I/O/取消见 `stream_reader`、`network_input`、`reconnect_controller`、`VideoCtl::LoopThread`。
- 修改色彩/显示/字幕从共享 `qt/video_widget.cpp` 入手，不再分别修改两套应用 renderer。字幕解码另查 `decoder_workers` 与 `subtitle_dispatcher`。

实际注册条件、超时、dummy/offscreen 环境以 CMake 为准。自动短时测试不能替代真实摄像机、长时同步、GPU/格式覆盖、性能和干净机器发行验收。[历史播放核心评审](playback-core-review.md)保留线索，不是本次重新审计结论。
