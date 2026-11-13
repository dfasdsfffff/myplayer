# MyPlayer 开发指南

本文按 **v2.1.0** 发布准备说明当前源码边界与排查入口，不代替构建、运行或性能验收报告；本次核对文档与源码，验证现有 Windows 两应用的启动 smoke 和构建/测试清单查询，未重新构建或执行测试套件。发行版本参数为 `2.1.0`、标签目标为 `v2.1.0`，不表示附件已发布。应用构建与成品打包见[根 README](../README.md)，公开 SDK 构建/API/状态表/线程契约见[SDK_README](../sdk/README.md)，SDK 实施验收记录见[方案](PLAYER_SDK_PLAN.md)。

## 1. 层次与依赖方向

| 层次 | 入口与职责 |
| --- | --- |
| MyPlayer | `apps/qt_player/main.cpp` → `MainWid`；播放列表、最近文件、设置、音轨/字幕选择 |
| MyPlayer Monitor | `apps/monitor/main.cpp` → `XViewer`；多路预览、ONVIF 发现与码流读取、摄像机配置、分段录像、检索及回放 |
| Qt SDK | `sdk/qt/include/player_sdk_qt/`；`QtPlayer` 负责 QObject 投递/生命周期，`VideoWidget` 负责统一画面和字幕显示 |
| C SDK | `sdk/c/include/player_sdk_c/`；`PlayerSDK::C` 是默认启用的版本化 C ABI 共享库，调用约定见 [C ABI 指南](../sdk/c/README.md) |
| Core SDK | `sdk/core/include/play_core/`；`Player` 是唯一受支持的 C++ 播放入口，别名 `PlayerSDK::Core` 对应静态 target `play_core` |
| 私有核心 | `sdk/core/src/`；解封装、解码、队列、同步、音频输出、网络输入/重连 |

两个应用均通过公开 Qt SDK 接入 Core，不再包含 Core 私有头文件。SDK 不反向依赖应用。旧 `PlaybackRuntime`、`PlaybackController` 仅为历史白盒测试保留，在仓库启用 `BUILD_TESTING` 时编入，嵌入/独立源码发行不编入这些入口；没有 `Player → PlaybackRuntime → Controller` 的 SDK 转发链。

`VideoCtl::MakeInstance()` 是私有独立实例工厂，不是单例。`RuntimeManager` 引用计数管理 SDL 音频和 FFmpeg 网络初始化；宿主先初始化 SDL 时，SDK 仅释放自身获得的 subsystem 引用。监控录像直接使用 FFmpeg，通过公开 `NetworkRuntime` lease 与 SDK 共存，不访问 `RuntimeManager`。

Core 依赖 FFmpeg、SDL2、SoundTouch 和 sigslot，不依赖 Qt/libass。Qt 组件依赖 Qt Widgets、libass 和 swscale；两个应用共用它。监控另需 Qt Network/Xml 处理 ONVIF。嵌入默认不构建 Qt、示例、测试或应用，不修改宿主 CRT、输出目录和全局编译选项。仓库 Windows 开发 preset 显式开启播放器及仓库/SDK 回归测试，**不自动开启监控和监控测试**，应另设 `MYPLAYER_BUILD_MONITOR=ON`、`MONITOR_BUILD_TESTS=ON`。

发行标签/软件包版本与源码内部版本仍需区分：根项目、监控独立项目、监控 `--version` 和播放器版本常量当前仍声明 `1.0.0`，不是本次目标发行版本 `2.1.0`；不能根据程序自报版本判断发行附件已更新。`V1.1.0`、`2.0.0`、`2.0.1` 是历史标签，不改写为新版本。

## 2. 公开入口与私有职责

### 公开 API

- `play_core/player.h`：`Player`、`Request`、`OperationResult`、`PlayerEvent`、`Subscription`。
- `play_core/media_source.h`：UTF-8 输入、网络参数、独立于 FFmpeg 的轨道/媒体信息与 `Rational`。
- `play_core/video_frame.h`：只读 CPU 帧、像素/颜色信息、独立共享所有权、64 位毫秒 PTS、文本/位图字幕。
- `play_core/network_runtime.h`：宿主直接使用 FFmpeg 时可选的共享网络初始化 lease。
- `player_sdk_qt/qt_player.h`、`video_widget.h`：QObject 线程桥接、帧邮箱、可嵌入显示与字幕。

公开头文件不出现 FFmpeg、SDL、sigslot 或 `VideoCtl`。SDK 对外使用不可变帧快照；关闭和销毁不会使宿主保留的帧失效。

### 私有核心（以下文件均位于 `sdk/core/src/`）

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

监控 `CoreVideoWidget` 基于同一个 `VideoWidget`，首次打开媒体时才创建独立 `QtPlayer`；构造空实时窗格不会初始化 SDL 音频，创建播放器失败由该窗格的 `open()` 返回错误。但 `XViewer::SetupPlaybackUi()` 在主窗口构造时已创建回放面板的 `QtPlayer`，不能把单个空窗格的启动测试推广成“整窗在任何音频初始化失败时都能启动”。不再维护监控专用 SDL 窗口、renderer、texture 或另一套轮询邮箱。静音预览使用 `MediaSource::enableAudio=false`，禁用音频解码/设备输出，不等于免除 SDK 实例的 SDL 音频子系统初始化。主界面内嵌回放面板直接调用独立 `QtPlayer` 的暂停/seek/倍速接口；`XPlayVideo` 也提供独立回放对话框实现。`XCameraRecord` 是独立录制路径，不从显示帧转码录像。

```text
两个应用 / 外部 Qt 宿主
  → QtPlayer → play_core::Player（串行控制线程） → VideoCtl
  → StreamReader（读取线程） → PacketQueue
  → DecoderWorkers（各轨线程） → FrameQueue

音频：sampq → AudioRenderQueue → 重采样/PCM → 有界环形队列
       → SDL 音频回调 → 设备/音频时钟
视频：pictq → 刷新线程按时钟选帧 → CPU 转换 → 公开不可变快照
       → Core 独立串行事件线程/最新帧邮箱
       → QtPlayer QObject 线程/最新帧邮箱 → VideoWidget SDL 渲染
         ├ Windows 原生平台：SDL 子窗口纹理/RenderPresent
         └ 非 Windows、offscreen/minimal：软件 surface/回读 → Qt 绘制
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

内部偶数尺寸 YUV420P 路径以 `av_frame_ref` 保留 CPU AVFrame backing，其他格式经 swscale 转 BGRA；公开 SDK 帧通过共享 plane 所有权或 BGRA aliasing `shared_ptr` 保持只读快照，**不会为了公开包装再复制全部像素**。转换器仅复用未被外部持有的帧，关闭/销毁后保留快照仍有效，但此路径不是硬解零拷贝。共享 `VideoWidget` 支持 SAR 与旋转，普通 YUV420P 上传 IYUV、BGRA 直接上传，SDL2 不支持的 BT.2020 YUV 转换保留 swscale 转 BGRA 回退；字幕经 SDL 混合。Windows 非 offscreen/minimal 平台使用 SDL 原生子窗口和渲染器，SDL 资源在原生窗口销毁前释放，QPainter 只画背景。**所有非 Windows 平台**以及 offscreen/minimal 使用 SDL software renderer/surface，每次呈现回读到 QImage 再由 QPainter 绘制，不创建 SDL 原生子窗口。SDL video subsystem 持有并释放自己的初始化引用；不能将 Windows 加速显示的性能结论推广到软件回读路径。

D3D11VA 仍只在 Windows 实现，偏好不保证设备可用；硬件帧下载到 CPU，不是 GPU 零拷贝。PQ/HLG 仅标记，不提供完整 HDR 色调映射。运行中硬解下载失败不保证无缝软回退。

Linux 在未显式设置 `SDL_AUDIODRIVER` 且首次 SDL 音频初始化失败时记录警告并尝试 `dummy` 驱动，允许静音运行；显式指定无效驱动仍返回失败。此回退不意味着已找到真实音频设备，macOS/Windows 也没有同样的自动 dummy 分支。vcpkg manifest 在 Linux 为 SDL2 启用 `alsa` feature（经 `libasound.so.2` 动态加载），构建出的程序有真实音频后端；缺少 ALSA/PipeWire 兼容层时才会进入上述静音回退。

输入包含 probe/analyze、读/连接超时、HTTP user-agent/headers、RTSP TCP/UDP；打开/探测/读取使用独立 I/O deadline 和 interrupt cancellation。SDK 可取消重连退避，成功重连恢复之前暂停/播放意图，最终失败才发公开错误。`stablePlaybackReset` 仍是预留字段，尚无稳定播放后重置重连次数实现。

SDK 来源启用普通 packet 背压，不沿用旧实时源的 unlimitedBuffer 分支，避免暂停直播时无限堆积。不可定位直播恢复可能有有界缓存延迟，不承诺回到最新实时边缘。HTTP 不必然直播或可定位；能力来自实际 AVIO/媒体信息。

内嵌字幕现在将 FFmpeg 位图和文字格式均入队；ASS 包保留 codec header，由 Qt 组件按包时间渲染，普通文字及位图另行呈现。外部 ASS/SSA/SRT 由共享控件处理；SRT 仍是简化 UTF-8 解析，不承诺任意编码/扩展语法。播放器与监控共享字幕路径，Qt 显示使用核心毫秒位置，不从整数秒重建精度。

SDK 自身公开错误不返回原始带凭据 URL；`RedactMediaLocation` 保护自有诊断不代表覆盖所有 FFmpeg/宿主日志、配置或凭据，分享排查材料前仍须检查。

## 6. 持久化与录像（应用业务）

播放器配置位于 `QStandardPaths::AppConfigLocation/player_config.ini`，首次可迁移程序目录下旧 `config/player_config.ini`，保留旧文件。保存窗口、列表、最近文件、播放位置和 `preferences`，由 `SanitizePreferences` 整理音量、倍速、循环、网络/硬解偏好。设置页的“应用”写入并应用，“确定”另关闭窗口，“取消”不提交本次表单修改，但不会撤销此前已点击“应用”的结果。音量、倍速和循环作用于当前播放器；硬解偏好交给 SDK，影响**下一次 open**；重连次数、连接/读取超时和 RTSP TCP/UDP 在下一次打开网络源时复制到 `MediaSource`，不重新配置已打开的连接。续播开关影响后续本地打开。旧 `play/*` 键不是设置页唯一来源；这些设置不共享给监控应用。

本地续播使用 canonical 路径 SHA-1 为键，进度标脏后合并保存；网络来源不建立本地续播目标。恢复需要可 seek、非直播、有效时长和 `ShouldResume`，接近结束按 `ShouldClearResume` 清理。列表/最近文件的持久化使用凭据过滤，不自动覆盖监控配置。

监控配置固定在 EXE 目录下的 `config/monitor-cams.db`，与启动工作目录无关；首次启动在新配置不存在时迁移工作目录、EXE 目录或其上一级目录的旧 `monitor-cams.db`，保留原文件，并将相对录像地址按旧配置目录转换为绝对路径。该文件是原生结构二进制记录，不是 SQLite；可能包含明文 URL 凭据，需按敏感配置保护，不是跨平台结构协议。安装目录需要写权限，加载/迁移/保存失败会提示。录像来自摄像机 `save_path` 并追加索引目录；`XCameraRecord` 直接 FFmpeg packet remux 分段，不经 SDK 显示帧。

监控分屏另由用户级 `QSettings("MyPlayer", "Monitor")` 的 `live/layout` 保存，仅记录 1/4/9/16 的数量，不记录设备绑定或预览 URL。设备配置仍为本地 8 位编码的定长字段，不是可直接跨系统/编码环境交换的数据库；新增/编辑将相对保存地址转换为当时工作目录下的绝对路径。录像按主码流、120 秒后遇到的关键帧分段，辅码流仅供设备拖入预览。勾选只定义启停目标，追加启动不重启已有任务；任务标记不代表已连通或成功写盘。删除设备要求先停止全部录像，但停止后删除仍会改变索引，可能使历史目录错配。检索是同步扫描当前设备索引目录顶层的特定命名 MP4/AVI，不是持续索引、跨片段时间轴或自动续播。

ONVIF 入口在 `apps/monitor/onvif_camera.cpp`，仅新增设备时由用户触发。它对可多播 IPv4 网卡发送 TTL=1 的 WS-Discovery（UDP 239.255.255.250:3702），约 2.5 秒后列出最多 128 个端点；通过 Device/Media v1 的 GetCapabilities/GetProfiles/GetStreamUri 读取 RTSP 单播地址，按分辨率像素数选择最大/最小配置作为主/辅码流。认证使用 UsernameToken PasswordDigest 及 Qt HTTP 认证回调；SOAP 单请求约 4 秒超时、缓冲约 1 MiB 上限，禁代理和重定向，返回服务/RTSP 地址的主机固定为发现来源 IPv4。凭据最终写入 RTSP URL 和明文设备配置；仅应在可信网络使用，不代表 ONVIF 全 Profile 合规，也没有 Media2/PTZ、IPv6、跨网段发现或摄像头参数下发。真实设备兼容性与发现防火墙规则仍需验收，失败可取消并手动填写。

## 7. 构建、平台与部署边界

- 根工程默认只构建 SDK；两个应用分别由 `MYPLAYER_BUILD_QT_APP`、`MYPLAYER_BUILD_MONITOR` 开启。应用使用 Qt 6.8+、C++20、CMake 3.25+；Windows preset 的 VS 2026 生成器另需 CMake 4.2+。Windows 必须匹配 MSVC/Qt/依赖架构，不能混用 Qt MinGW 套件。
- 监控可独立配置；`MONITOR_USE_BUNDLED_DEPS` 仅在独立工程定义且默认 OFF，ON 分支只支持仓库的 Windows x64 FFmpeg/SDL2/SoundTouch 备份，不包含完整 Qt/libass 依赖。Linux/macOS 使用包/toolchain 查找，SoundTouch 的非 Windows 分支链接 C++ API；不能使用 Windows `.lib`/DLL 备份替代。
- 程序默认输出到仓库 `bin/`，由 `PLAYERDEMO_OUTPUT_DIR` 改写。Windows 应用资源脚本、运行库复制及监控 `windeployqt` 都有平台条件；非 Windows 不执行 Windows 部署动作。仅编译成功不等于运行时插件/库/许可证齐全。
- Windows 双 ZIP 用 `scripts/package-portable.ps1 -BuildDir <整仓构建目录> -Version 2.1.0`；要求两个应用、匹配的 Qt/vcpkg、Qt 许可证及 MSVC CRT，执行安装、Qt 部署、暂存包 smoke 并生成 SHA-256。监控独立构建不能代替整仓安装树。
- Linux 用 `scripts/package-portable-linux.sh <Release构建目录> 2.1.0`，两个应用同置于一个 tar.gz；需 sanitizers OFF、Qt/vcpkg 缓存路径、Qt 许可证、`patchelf`。它收集匹配前缀的依赖和 Qt 插件、调整 RPATH、检查暂存库路径及无界面启动；系统库仍由宿主提供，不保证跨所有发行版兼容。脚本默认版本不是本次发行版本，应显式传 `2.1.0`。
- `.github/workflows/macos-build.yml` 定义 Homebrew Qt + vcpkg/Ninja 的双应用 Release 构建及 CTest；当前没有对应 `.app`/DMG、签名或公证脚本。Windows workflow 与 Linux Clang sanitizer workflow 的范围不同，存在配置不证明本次执行成功。具体命令及许可边界见根 README/第三方说明。


## 8. 测试与排查

- 公开 SDK：`sdk/tests/api`，测试受理/终结、生命周期、毫秒、UTF-8、慢回调/队列预算、异常、保留帧、SDL 共存；Qt 测真实画面、字幕、UI 卡顿、双向销毁。
- 外部宿主：`sdk/tests/consumer` 可复制出仓库测试 Core/Qt targets 和宿主构建隔离；`scripts/package-sdk.cmake` 验证最小源码发行。
- 私有管线：队列/时钟、decoder/reader、audio、hardware、seek/track、media compatibility、network integration；历史 Runtime 仅供这些白盒测试。
- 应用：设置/列表/隐私/主窗口；`main_window_behavior_tests` 检查关闭菜单时 Ctrl+O、Enter 单次切换全屏及视频/音频占位层互斥。监控测试由独立 `MONITOR_BUILD_TESTS` 开启，覆盖 16 路预览/切换/销毁/分段录制、录像检索/回放与按勾选设备启停、无效音频驱动下的空实时窗格及窗口 smoke；非 Windows 启动测试另检查无原生子窗口。**没有 ONVIF 专门自动测试或真实摄像头验收结论**。不再用源码字符串断言替代 UI 行为测试。
- Windows 边缘缩放回归需要原生平台：`cmake -E env QT_QPA_PLATFORM=windows SDL_AUDIODRIVER=dummy build/tests/Release/main_window_behavior_tests/main_window_behavior_tests.exe`。该模式对真实 HWND 的四边、四角和客户区发送物理像素命中查询；默认 offscreen 测试不执行此检查。200% 缩放下另需在实际播放/暂停时拖拽视频侧边和窗口角点，检查画面比例及最大化/还原。
- 旧 SDL subtitle renderer 和邮箱仅保留为 `tests/support` 回归夹具，不参与应用或 SDK 显示。
- 修改打开/派发/状态从 `sdk/core/src/player.cpp` 和 `sdk/qt/qt_player.cpp` 入手；具体 I/O/取消见 `stream_reader`、`network_input`、`reconnect_controller`、`VideoCtl::LoopThread`。
- 修改色彩/显示/字幕从共享 `qt/video_widget.cpp` 入手，不再分别修改两套应用 renderer。字幕解码另查 `decoder_workers` 与 `subtitle_dispatcher`。

实际注册条件、超时、dummy/offscreen 环境以 CMake 为准：例如监控 integration 与窗口 smoke 未强制指定 Qt offscreen，无桌面运行需显式设置平台；Windows 的发行元数据/安装布局测试有 Windows 条件。macOS workflow 定义测试步骤，但本文未执行或确认其结果。自动短时测试不能替代真实摄像机/ONVIF、长时同步、GPU/格式覆盖、性能和干净机器发行验收。[历史播放核心评审](playback-core-review.md)保留线索，不是本次重新审计结论。
