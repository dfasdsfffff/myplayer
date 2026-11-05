# MyPlayer 开发指南

本文按当前源码说明应用边界、播放链路和定位入口，不是运行验证报告。配置、构建、运行、测试命令、安装与打包步骤统一见[根 README](../README.md)；不要把历史构建目录或已生成的可执行文件当作环境前提。

## 1. 两个应用与共享核心

| 层次 | 入口与职责 |
| --- | --- |
| Qt 播放器 | `apps/qt_player/main.cpp` → `MainWid`；单媒体播放、播放列表、最近文件、设置、音轨/字幕选择和外置字幕 |
| 视频监控 | `apps/monitor/main.cpp` → `XViewer`；多窗口预览、摄像机配置、分段录制、录像检索和回放 |
| 公共播放核心 | `play_core/`，静态库 `play_core`；解封装、解码、队列、时钟、音频输出和视频/字幕数据派发，不依赖 Qt |

两个应用均使用 `PlaybackRuntime`，不是把播放器窗口嵌入监控程序。`VideoCtl::MakeInstance()` 创建独立实例，非单例；`RuntimeManager` 引用计数管理共享的 SDL 音频和 FFmpeg 网络初始化。

顶层选项 `MYPLAYER_BUILD_QT_APP`、`MYPLAYER_BUILD_MONITOR` 控制应用构建；监控另有独立入口 CMake。具体依赖与配置以[顶层 CMake](../CMakeLists.txt)、[核心 CMake](../play_core/CMakeLists.txt)、[监控 CMake](../apps/monitor/CMakeLists.txt)和根 README 为准。核心依赖 FFmpeg、SDL2、SoundTouch 和 header-only sigslot；播放器 UI 使用 Qt Widgets，字幕渲染另依赖 libass。不能因为 `play_core` 无 Qt 依赖，就认为默认整套测试也不需要 Qt。

发行标签/压缩包版本不等于程序内部版本。目前源码中顶层 `project()`、两个 `main.cpp` 的应用版本以及 `GlobalHelper::GetAppVersion()` 仍使用 `1.0.0`；不要据此把 V1.1.0 发行包描述为已经完成内部版本统一。

## 2. 类职责与定位入口

### 公共播放层

| 文件/类型 | 当前职责 |
| --- | --- |
| `playback_runtime.h/.cpp` / `PlaybackRuntime` | 拥有 `VideoCtl` 和控制接口，转发核心信号；析构先 `stopAndWait()` 再断开连接 |
| `playback_controller.h/.cpp` / `PlaybackController` | 应用侧动作接口；`playback_controller_videoctl.cpp` 将动作绑定到 `VideoCtl`，不是播放工作线程 |
| `videoctl.h/.cpp` / `VideoCtl` | 播放生命周期、刷新循环、组件开关、同步调度、seek/暂停/选轨命令执行、循环策略与重连编排 |
| `media_session.h/.cpp` / `MediaSession` | 持有当前 `VideoState`，通过关闭回调释放会话 |
| `stream_reader.h/.cpp` / `StreamReader` | 打开/探测输入、选流、解封装、路由 packet、处理 seek、EOF 与输入/解码错误上报 |
| `decoder.h/.cpp` / `Decoder` | codec 上下文、packet serial、send/receive 解码协议与解码线程回收；字幕使用 FFmpeg 字幕解码接口 |
| `decoder_workers.h/.cpp` / `DecoderWorkers` | 音频、视频、字幕 worker，将解码结果放入各自 `FrameQueue` |
| `packet_queue.*` / `PacketQueue` | 带 SDL 锁/条件变量的 packet 队列，保存序列与计数，支持 abort/flush；flush 增加 serial |
| `frame_queue.*` / `FrameQueue` | 有界帧队列，管理解码帧引用、字幕对象、读写等待和 keep-last 行为 |
| `audio_output.*`、`audio_render_queue.*` | SDL 音频设备、重采样/PCM 准备、生产线程、PCM 环形队列与回调消费 |
| `clock.*`、`media_sync.*` | 音频/视频/外部时钟、主时钟选择、显示延迟和外部时钟速度调整 |
| `video_frame_converter.*`、`renderer_dispatcher.*` | 将 AVFrame 转为可跨线程持有的 `VideoFrame`，派发尺寸变化与视频帧；不创建应用显示窗口 |
| `network_input.*`、`reconnect_controller.*` | 输入分类/选项/超时/错误映射/地址脱敏，以及可取消的重连等待 |
| `hardware_decode.*` | Windows D3D11VA 设备与像素格式协商、硬件帧下载 |
| `subtitle_dispatcher.*`、`subtitle_frame.h` | 将 FFmpeg 字幕复制为带起止时间的独立字幕数据 |
| `playback_command_mailbox.*` | 汇总暂停切换、seek 和选轨请求，交给刷新循环消费 |

`VideoState` 不是一个全原子的状态机。它由 `SessionState`、`MediaClockState`、`AudioState`、`VideoTrackState`、`SubtitleState`、`FilterState` 组合：取消/暂停等跨线程标志使用原子量，seek 请求使用 `seek_mutex`，轨道组件使用 `trackMutex`；`VideoCtl` 另用 `m_streamMutex` 保护当前会话指针。修改字段时要跟随现有访问方的锁、队列与所有权规则，不能仅凭字段分组推断可并发访问。

### Qt 播放器

- `MainWid` 组合窗口、菜单、列表、设置、`PlaybackRuntime` 与 `PlaybackRuntimeBridge`，处理打开来源、状态显示、轨道菜单和续播。
- `PlaybackRuntimeBridge` 把原生 sigslot 通知投递到 Qt 对象线程。视频帧走 `LatestVideoFrameMailbox`，覆盖尚未呈现的旧帧并合并投递；其他通知使用 queued invoke。核心原生信号本身没有“自动切到 GUI 线程”的保证。
- `Show` 在 Qt 主线程中使用原生子窗口上的 SDL renderer/texture，持有当前视频帧并叠加字幕；`CtrlBar` 负责播放控制和进度；`Title` 负责标题栏。
- `Playlist` / `MediaList` 负责列表和导航，`PlaylistFile` 负责 M3U/M3U8；`media_format_registry.*` 统一文件筛选与音频/字幕类型判断。
- `SettingWid` / `AppPreferences` 负责设置编辑、合法值整理和持久化；`GlobalHelper` 提供配置路径、窗口/列表/最近文件/播放位置存储等；`playback_status_presenter.*` 负责状态到用户提示的映射。
- `SubtitleRenderer` 使用 libass 处理外置字幕与可到达的文字数据，并使用 SDL 合成字幕图像。

### 监控应用

- `XViewer` 管理分屏、摄像机列表、录制控制、日期/录像文件列表；`XCameraWidget` 是可接收摄像机拖放的预览窗口。
- `CoreVideoWidget` 每次打开创建自己的 `PlaybackRuntime`，核心帧回调只更新互斥锁保护的最新帧；Qt 定时器约每 33 ms 取帧并进行 SDL 显示。它没有使用播放器的 `PlaybackRuntimeBridge` 或 `Show`。
- 预览打开时使用静音模式，且 `MediaSource::enableAudio=false`，不是仍打开音频设备后只将音量设零。`XPlayVideo` 用 `CoreVideoWidget` 提供录像回放、暂停、seek、倍速。
- `XCameraRecord` 是独立的 FFmpeg 录制路径，不通过 `VideoCtl` 取显示帧；`XCameraConfig` 管理摄像机数据。监控具体操作见[监控说明](../apps/monitor/README.md)。

## 3. 从打开到呈现

```text
播放器 MainWid / 监控 CoreVideoWidget
  → PlaybackRuntime::controller() → PlaybackController → VideoCtl
  → StreamReader（读取线程）：open / probe / av_read_frame
  → audioq / videoq / subtitleq（PacketQueue）
  → DecoderWorkers（各轨解码线程）
  → sampq / pictq / subpq（FrameQueue）

音频：sampq → AudioRenderQueue 生产线程 → 重采样/PCM → 环形队列
       → SDL 音频回调 → 音频设备、音频时钟
视频：pictq → VideoCtl 刷新线程同步选帧 → VideoFrameConverter
       → 核心信号 → 应用最新帧交接 → GUI 线程 SDL 呈现
字幕：subpq → 核心时间选择/派发 → 播放器 SubtitleRenderer
```

### 打开、状态与错误

`VideoCtl::StartPlay()` 先停止并等待旧的刷新线程，再创建会话/读取线程和刷新线程。返回 `true` 仅说明启动流程已建立，不代表网络已经连通、codec 已打开或首帧已显示；异步失败由 `SigPlaybackStatus` 通知。

读取线程执行 `avformat_open_input()`、`avformat_find_stream_info()`、`av_find_best_stream()`，打开可用的音频/视频/字幕组件。至少需要成功打开音频或视频之一。`MediaInfo` 发布时包含时长、seek 能力、音轨/字幕轨元数据等；轨道列表不表示每种 codec 都已验证可解码。

`PlaybackState` 声明了 `Opening`、`Buffering`、`Reconnecting`、`Playing`、`Failed`、`Stopped`。当前不能把它解释为覆盖所有 UI 行为的完整状态机：暂停通过独立的 `SigPauseStat` 通知；`Buffering` 虽被声明，当前核心没有发送该状态的路径；`Playing` 在流组件打开后发出，不是首帧呈现确认。解码 worker 的不可恢复错误写入 `decoderError`，读取线程统一将其转换为 `DecoderFailure` 状态。`AVERROR_INVALIDDATA` 是当前 `Decoder` 明确视为可恢复的错误，不能概括为所有解码错误都会被忽略。

### 线程边界

每个活跃会话可以有读取线程、刷新线程、音频/视频/字幕解码线程、额外的音频 PCM 生产线程，以及 SDL 自己的音频回调；未打开的轨道不会对应完整的处理链。Qt 主线程只负责 UI 和视频呈现，不执行常规解封装/解码。FFmpeg 自身还可能创建内部 codec 线程，因此不能写成固定的“三线程”或“五线程”模型。

同步默认采用音频主时钟；没有音轨时 `MediaSync` 回退到外部时钟。视频刷新按 PTS、时钟差和帧持续时间决定延迟、丢弃旧 serial 或过晚的帧。界面秒数是主时钟的整数通知，不是逐帧呈现时间戳。

## 4. 音频、视频与硬解的实际边界

### 音频

SDL 设备协商输出为 `AUDIO_S16SYS`。解码线程只产出音频帧；`AudioRenderQueue` 的生产线程调用 `AudioOutput::DecodeFrame()`，根据输入与设备格式使用 swresample，并在现有分支中调用 SoundTouch。SDL 回调从 PCM 环形队列读数据、在不足时补静音、按音量缩放，再按生产时钟、待播 PCM 和估算设备延迟更新时钟；它不再在回调内阻塞等待解码或执行重采样。

倍速不能只看 UI 下拉框就推断所有格式处理一致。默认源码中 `CONFIG_AVFILTER` 为 0，CMake 没有为核心启用它；存在的 `atempo` / `setpts` 分支不等于默认播放链。SoundTouch 当前位于重采样分支内，EOF 尾部处理、不同输入格式与暂停恢复仍需针对真实媒体验证。核心 `set_play_speed()` 当前接受 `(0.1, 2.0]`，而偏好值整理接受 `[0.1, 2.0]`；不要把二者写成已统一的契约。

### 视频帧与显示

`VideoFrame` 有 `Bgra32`、`Yuv420P` 两种格式，不是统一 BGRA 拷贝：

- 偶数宽高的 `AV_PIX_FMT_YUV420P` 保留 AVFrame 引用，通过共享 plane 所有权传给应用，支持各 plane 独立 stride。
- 其他格式由缓存的 swscale context 转为 BGRA，使用 `bgra` 存储；转换检查尺寸、色彩空间/范围与转换结果。
- 对象带 SAR、旋转、色彩空间/范围和不支持的 HDR transfer 标记。携带这些元数据不意味着每个显示后端都完整处理它们。
- 播放器 `Show` 与监控 `CoreVideoWidget` 都支持 SDL IYUV/BGRA 纹理。两者均使用 `ComputeVideoDisplayRect()`；监控显示路径显式选择 YUV 色彩转换并使用 `SDL_RenderCopyEx()` 旋转，播放器 `Show` 当前使用 `SDL_RenderCopy()`，不能将两个后端的旋转/色彩行为写成完全一致。

转换器复用空闲的 `VideoFrame` 对象，但 YUV 路径仍分配并引用 AVFrame，池全部被外部持有时也会创建新对象。界面最新帧邮箱避免旧帧在事件队列中无限积压，不会撤销此前已经完成的转换/引用开销，也不等价于无丢帧播放。

HDR 的 PQ/HLG transfer 会被标记；核心有一次性提示，但没有完整 HDR 色调映射管线，不能宣称 HDR 正确显示。YUV 快路径也不会因该标记自动改走 BGRA，因此提示文本不应当作实际格式选择的证明。

### 硬件解码

`HardwareDecodeContext` 当前只实现 Windows D3D11VA。偏好有 `Auto`、`Disabled`、`D3D11VA`；其中 `D3D11VA` 只是启用偏好，不保证硬解成功。设备不可用/初始化失败会留下软件解码配置，像素格式协商失败也有回退行为。

硬件 AVFrame 通过 `av_hwframe_transfer_data()` 下载到可复用的软件 AVFrame，之后仍走核心帧转换和 SDL 纹理上传。这不是 GPU 零拷贝。下载失败会返回解码错误；不能描述成任何运行中硬解故障都能自动无缝切换软件解码。

## 5. seek、暂停、停止与 EOF

- **控制请求**：暂停、seek、选轨先写入 `PlaybackCommandMailbox`，由刷新循环取出执行；输入 seek 再以完整请求交给读取线程。音量/倍速/停止有各自路径，并非所有操作都经邮箱。
- **seek**：最终受 `CanSeek(MediaInfo)` 限制；读取线程调用 `avformat_seek_file()`，成功后 flush 各 packet 队列并增加 serial。解码器发现 serial 改变后 flush codec，消费方丢弃旧序列帧；音频环形队列使用 generation 防止旧 PCM 回填，并清理 SoundTouch 样本。不能简单写成“UI 直接清空全部帧并重启线程”。暂停时 seek 会请求单步刷新。
- **暂停**：刷新线程更新 paused 标志及三个时钟，读取线程依据变化调用 `av_read_pause()` / `av_read_play()`；生产线程停止准备新的 PCM。暂停路径并没有直接调用 `AudioOutput::Pause()` 暂停 SDL 设备，因此已缓冲音频的行为仍应专项验证。
- **停止**：`stop()` / `OnStop()` 发出取消并请求退出，不保证返回时全部资源已释放；`stopAndWait()` / `OnStopAndWait()` 还会 join 刷新线程。销毁会话先取消 I/O、唤醒读取和所有队列消费者、关闭音频输出，再 join 读取线程，逐个 abort/join 解码组件，最后释放状态。`SigStopFinished` 是资源清理后的通知，不应与停止请求混用。
- **EOF**：读取 EOF 时为已启用轨道发送空 packet，让 codec 排空。正常结束判定等待音视频解码器当前 serial 完成、帧队列消耗完，以及音频 `renderQueue->drained()`（包含待播 PCM 和输出截止时间）。这不证明 SoundTouch/可选滤镜内部尾部已完整 flush。
- **循环**：全部循环/随机模式向应用发“下一项/随机项”信号；单曲模式请求 seek；不循环模式触发停止。这些策略并不属于解封装器自动创建播放列表的能力。

## 6. 网络来源与限制

`MediaSource` 保存地址、`NetworkOptions` 和是否启用音频。来源分类识别 HTTP/HTTPS、RTSP、RTP、UDP，其他带 scheme 的地址归为其他网络来源；具体协议、容器和 codec 能否播放仍取决于构建出来的 FFmpeg。

- 输入选项包含 probe/analyze、读超时；HTTP 支持 user-agent、headers 和 FFmpeg 重连选项；RTSP 支持 TCP/UDP 与连接超时。打开、探测、读取各自设置 `IoControl` deadline，由 FFmpeg interrupt callback 检查超时或取消。
- 应用层 `ReconnectController` 对 `Timeout`、`NetworkUnavailable`、`ConnectionLost` 按次数和封顶指数退避重新打开会话，停止可以取消等待。HTTP 的 FFmpeg 内部重连与此路径不是同一层机制。
- `NetworkOptions::stablePlaybackReset` 目前仅有字段声明，没有稳定播放后清零重连计数的实现，不应将其写成现成功能。
- RTSP/RTP/UDP 按实时源处理，没有已知时长且不能 seek；其他来源由实际 AVIO seek 能力决定。HTTP 不必然是直播，也不必然可拖动进度。
- 实时输入使用 `UseUnlimitedBuffer()` 绕过普通 packet 背压，不是固定容量的低延迟网络缓存策略；不要承诺长时间断续输入下内存始终有界。
- `RedactMediaLocation()` 会遮蔽 URL userinfo 和部分已知敏感查询参数，但这不等于所有第三方日志、应用路径和配置文件自动脱敏。排查问题时分享日志/配置前仍需检查凭据。

## 7. 字幕支持与明确限制

核心 `SubtitleDispatcher::Copy()` 可复制位图以及 ASS/text 字段；但 `DecoderWorkers::Subtitle()` 当前只将 `AVSubtitle::format == 0` 的结果入队，其他格式释放后丢弃。因此不能仅凭 dispatcher 和 libass 的存在就宣称所有内嵌 ASS/SRT/文字字幕可用。内嵌字幕通过刷新时钟选择有效时间段，并在 seek、关闭字幕或停止时清除活动对象。

播放器外置字幕入口在 `MainWid::OpenSubtitleFile()`、`Show::LoadExternalSubtitleFile()`；文件对话框列出 SRT/ASS/SSA。SRT 由本地简化解析器转为 ASS，ASS/SSA 交给 libass，再与视频经 SDL 合成；不承诺任意编码、任意 SRT 扩展语法都支持。`Show` 当前使用整数播放秒数作为字幕渲染时钟，即使 libass 内部缓存粒度更细，也不能称为已经实现毫秒级 UI 字幕同步。

监控 `CoreVideoWidget` 没有连接字幕信号/叠加字幕渲染器，播放器外置字幕能力不能直接套用到监控预览或录像回放。

## 8. 配置与持久化

### 播放器

`GlobalHelper::GetConfigFilePath()` 使用 `QStandardPaths::AppConfigLocation` 下的 `player_config.ini`，不是要求用户在程序目录预先建立 `config/`。首次目标不存在时会尝试从程序目录的旧 `config/player_config.ini` 迁移，迁移列表/最近文件时应用凭据过滤。

同一 INI 中保存窗口、列表、最近文件、播放位置及 `preferences`。`AppPreferences` 保存音量、速度、循环、续播开关、网络超时/次数、RTSP transport、硬解偏好，并经过 `SanitizePreferences()` 整理。`GlobalHelper` 仍有旧 `play/*` 存取接口，不能把这些键当作设置页当前唯一来源。

本地续播以 canonical 路径 SHA-1 作为位置键；`MainWid` 在进度变化时标脏并合并定时保存，切换/停止等路径调用 flush。网络打开路径不建立本地续播目标。恢复还需要可 seek、非直播、有有效时长以及 `ShouldResume()` 判定；接近结尾由 `ShouldClearResume()` 判定清除，不是任何文件都无条件跳回上次秒数。

列表和最近文件写入使用 `MayPersistMediaLocation()` 过滤含敏感凭据的来源；导入/导出另见 `PlaylistFile` 与隐私相关测试。此规则不代表监控配置也采用同一种存储策略。

### 监控

`XViewer` 使用相对当前工作目录的 `monitor-cams.db`；名称虽是 `.db`，`XCameraConfig` 实际逐项保存 `XCameraData` 二进制结构，不是 SQLite，也不是播放器 INI。摄像机名称、主/辅码流、录制目录均在该记录中，地址可能包含明文凭据，应按敏感配置保护。它不是跨平台/跨结构布局的通用数据库格式。

录像根目录来自摄像机的 `save_path`，`XViewer::StartRecord()` 再追加摄像机索引子目录并启动 `XCameraRecord`；不是统一写入播放器的配置目录。

## 9. 测试分组与排查入口

下面是按职责整理的入口，不是固定测试数量，也不是全部测试名称。注册条件、标签、超时、工作目录及 dummy/offscreen 环境应直接查[顶层 CMake 的测试区](../CMakeLists.txt)和[监控 CMake](../apps/monitor/CMakeLists.txt)。执行方式以根 README 为准。

| 分组 | 示例与关注点 |
| --- | --- |
| 核心结构、并发和生命周期 | `play_core_refactor_tests`、`playback_command_mailbox_tests`、`clock_concurrency_tests`、`playback_lifecycle_stress_tests`（`STRESS_TESTS` 标签） |
| 解码、读取与音频 | `decoder_workers_tests`、`stream_reader_tests`、`audio_output_tests`、`audio_render_queue_tests`、`soundtouch_wrap_tests`、`filter_configurator_tests` |
| 媒体与显示契约 | `media_info_tests`、`video_frame_format_tests`、`video_geometry_tests`、`hardware_decode_policy_tests`、`track_switching_tests`、字幕 dispatcher/renderer 测试 |
| 网络与端到端播放 | `network_playback_tests`、`reconnect_controller_tests`；`playback_integration_tests`、`network_integration_tests` 标记 `INTEGRATION`，`media_compatibility_tests` 另标记 `COMPATIBILITY` |
| 播放器 UI、列表与设置 | preferences/settings、resume、列表导航/文件/隐私、最新帧邮箱、`main_window_behavior_tests`、`show_widget_tests` 等；部分是源码契约检查而非真实窗口呈现测试 |
| 构建、安装与入口 | CMake 配置、发行元数据、安装布局、播放器 smoke test；不能替代实际发行包运行和手工播放 |
| 监控 | `MONITOR_BUILD_TESTS` 控制多路播放/录制集成测试及监控窗口 smoke test |

新增功能时从对应测试文件和 CMake 注册条件一起定位；不要将存在测试文件等同于本次已经运行通过。网络集成、硬解设备、可见 GUI、长时播放和性能指标需要相应环境与专项验证。

## 10. 常见修改的起点

- 打开文件/URL、最近文件、续播、错误提示：`apps/qt_player/mainwid.cpp`、`playback_status_presenter.cpp`、`globalhelper.cpp`。
- 输入阻塞、超时、重连、网络 seek：`play_core/network_input.cpp`、`stream_reader.cpp`、`reconnect_controller.cpp`、`VideoCtl::LoopThread()`。
- seek 后旧帧/旧音频、停止卡住、EOF 丢尾：`VideoCtl::stream_seek()` / `stream_close()`、`StreamReader::Run()`、`Decoder::decode_frame()`、`AudioRenderQueue` 的 generation/drained 规则。
- 色彩/画幅/纹理上传：`video_frame_converter.cpp`、`video_frame.h`、播放器 `show.cpp` 与监控 `core_video_widget.cpp` 分别检查，不能只改其中一个就宣称全应用一致。
- 内嵌/外置字幕：`decoder_workers.cpp`、`subtitle_dispatcher.cpp`、`subtitle_renderer.cpp`、`Show` 的时间来源。
- 摄像机配置、录像、回放：`apps/monitor/xcamera_config.cpp`、`xcamera_record.cpp`、`xviewer.cpp`、`xplayvideo.cpp`。

[播放核心评审记录](playback-core-review.md)保留历史待验证线索，不是当前缺陷清单或本次重新审计的结论。
