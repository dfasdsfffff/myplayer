# 可嵌入播放器源码 SDK 方案与实施记录

## 1. 方案状态与目标

本文原为 SDK 设计方案，现作为历史需求与实施记录保存。2026-11-06 已实现首版源码 API、构建隔离、Qt 组件和两个应用迁移；第 2～10 节保留当时设计，第 11～12 节保留当时实施与本机验证记录，不作为 v2.1.0 重新验收结论。实际 API/状态表/限制以 [SDK 接入指南](../sdk/README.md)、公开头文件及下文当前边界为准。“未创建标签/未推送”、旧包名、测试数量和 QPainter 说明仅指相应历史快照，不能用于判断当前标签或发布状态。

### v2.1.0 当前边界（源码核对）

- 当前默认构建 `PlayerSDK::Core` 与 `PlayerSDK::C`，Qt、示例与公开测试默认关闭。Core/Qt 为 C++20 源码 API，C ABI 主版本为 1；C ABI 指南见 [SDK C 接入](../sdk/c/README.md)。历史“首版不提供 C ABI”“默认 Core-only”“安装留待以后”不再描述当前完整功能。
- 项目发行目标标签为 `v2.1.0`，但 SDK 自报版本及 CMake project/库 `VERSION` 仍为 `1.0.0`。`scripts/package-sdk.cmake` 也仍固定生成 `PlayerSDK-1.0.0-source.zip`，不能将它误写成已发布的 v2.1.0 附件。
- C ABI 安装、运行依赖收集与 `find_package(PlayerSDK CONFIG REQUIRED)` 导出规则限 Windows x64/MSVC Release；C++ Core/Qt 未提供对应安装导出契约，继续源码接入。
- Linux/macOS 已有源码适配：支持 `SoundTouch::SoundTouch`，Qt 使用 SDL software surface 合成并读回 QImage 交给 QPainter，不创建 Windows 式原生 SDL 子窗口；Windows 正常窗口仍使用 SDL 原生子窗口。当前不能再统一声称“QPainter 仅画空背景、播放不读回”。
- Linux 在未指定 `SDL_AUDIODRIVER` 且音频初始化失败时可尝试 dummy 静音输出；Linux Qt 会在配置的 vcpkg 树存在 `lib/libfontconfig.so` 时优先链接该库。它们不是有声设备、全部平台或任意依赖组合已通过验收的证明。
- 当前公开 BGRA 缓冲为 `shared_ptr<const vector<uint8_t>>`，YUV plane 共享 FFmpeg 引用，避免在公开事件发布时深拷贝像素；不代表硬解零拷贝或所有格式都无转换/分配。手工提交帧的迁移要求见 SDK 指南。
- 监控延迟创建播放器、YUV 软件色彩转换及外挂字幕清理已在 `2.0.1` 源码基线存在，不列作 v2.1.0 新增。当前新增范围按 [SDK 变更记录](../sdk/CHANGELOG.md) 区分。

本次以源码与文档核对为主，仅运行现有 Windows 应用的启动 smoke 与构建/测试清单查询；未重新构建、执行 SDK/应用测试套件、完整 GUI 验证或性能基准。第 9 节勾选及第 11～12 节结果保持历史含义。干净机器、真实摄像机、长时性能、跨平台支持矩阵和最终许可审计仍须独立验收。

### 原始目标（历史）

首版直接将现有 `play_core` 改造为源码 SDK，采用“源码交付 + C++ API + 可选 Qt Widgets 组件”，不在其上新增独立的 `player_sdk` 包装库。使用方将 SDK 作为 CMake 子项目加入自己的工程，用自己的工具链一起构建，无需理解 FFmpeg、SDL 和内部线程即可集成播放。现有 `apps/qt_player` 与 `apps/monitor` 保留，逐步迁移为 SDK 的真实使用方，不再新增第三个独立播放器。

首版不提供 C ABI，不将预编译 DLL 作为交付主路径，也不承诺稳定二进制 ABI。升级 SDK 源码后，使用方需要重新构建 SDK 和依赖该接口的代码。首版重点是稳定、清晰的源码 API、可靠生命周期和易用的构建接入，而不是让旧 EXE 无需重编译就替换新版 DLL。

首版验证 Windows x64 和约定的 MSVC 工具链组合，C++ 语言基线沿用项目当前 C++20；Qt 组件面向 Qt Widgets，支持版本以实测矩阵为准。源码交付不代表已经支持任意编译器或跨平台构建。

Core 提供播放控制、内置音频输出和 CPU 视频帧交付，不负责非 Qt 窗口显示。Qt Widgets 组件提供可直接嵌入的显示控件；非 Qt 宿主自行渲染帧，首版不提供传入 `HWND` 即播放的 Windows 渲染适配器。

当前架构说明见[开发指南](PROJECT_GUIDE.md)，现有构建、测试及成品打包步骤见[根 README](../README.md)。源码 SDK 接入、构建与部署的当前说明见 [SDK 接入指南](../sdk/README.md)；本文保留最初建设目标，并在文末记录具体实现/验证边界。

## 2. 原设计时的基础与缺口（历史）

| 已有基础 | SDK 化需要补齐 |
| --- | --- |
| `play_core` 已是无 Qt 依赖的静态库 | 稳定公开头文件、隐藏内部类型、形成可嵌入的 CMake targets |
| `PlaybackRuntime::Create()` 提供统一创建入口 | 对外提供播放对象，不暴露内部 controller 和 `VideoCtl` |
| 已有本地文件、网络源、暂停、定位、倍速和轨道选择 | 统一命名、状态、时间单位、能力查询及异步完成语义 |
| 已有视频帧、字幕、媒体信息和错误事件 | 明确回调线程、数据生命周期、事件顺序和关闭规则 |
| 两个应用分别接入核心 | 抽取重复的 Qt 桥接和显示逻辑，避免 SDK 依赖应用 |
| 已有核心集成测试和成品部署脚本 | 增加仓库外源码 consumer、宿主构建隔离和共存测试 |

原设计证据入口（链接已随目录迁移；下列说明区分当时基础与当前代码）：

- [核心 CMake](../sdk/core/CMakeLists.txt)：当时需收紧公开目录；当前只向 consumer 公开 `include/`，FFmpeg/sigslot/`src/` 为私有，静态链接依赖仍传递。
- [PlaybackRuntime](../sdk/core/src/playback_runtime.h)：历史实例入口；当前只为仓库内部回归保留，公开入口为 `play_core::Player`。
- [私有媒体源及状态定义](../sdk/core/src/media_source.h)：允许内部 FFmpeg 类型；当前公开定义位于 `sdk/core/include/play_core/`，不泄漏这些类型。
- [Qt 播放器桥接](../apps/qt_player/playback_runtime_bridge.h)：历史桥接基础；当前应用桥接使用公开 Qt SDK。
- [监控显示组件](../apps/monitor/core_video_widget.cpp)：历史独立接入点；当前使用共享 `VideoWidget`，不是另一套私有 SDL 播放实现。

SDK 的重点是公开边界、生命周期和源码接入，不是重写解封装、解码或同步管线，也不是让使用方手动复制所有 `.cpp` 文件。

## 3. 分层与依赖方向

| 层次 | 职责 | 公开策略 |
| --- | --- | --- |
| `play_core` | 公开 C++ 播放 API，以及库内的解封装、解码、同步、音频输出、网络重连 | 本身即 Core SDK，不依赖 Qt；仅承诺指定公开头文件的源码兼容性 |
| `player_sdk_qt` | 基于 `play_core` 公开 API 的 QObject 桥接、播放控件、Qt 线程投递 | 公开，可选，随宿主一起构建 |

依赖方向：

```text
现有播放器 / 现有监控软件 / 外部 Qt 宿主
                  ↓
       player_sdk_qt（可选）
                  ↓
       play_core（源码 SDK） ← 外部 C++ 宿主
       ├─ include/play_core/：公开 API
       └─ src/：私有播放实现
```

SDK 不得反向依赖 `apps`。播放列表、历史记录、设置、监控布局、摄像机管理和录制业务保留在应用层。不设计额外的 C API、`player_sdk` 或 `player_sdk_cpp` 库；公开接口与私有实现都属于同一个 `play_core` target。

直接 SDK 化不等于把现有全部核心头文件公开：在 `play_core` 内划分 `include/play_core/` 与 `src/`，外部只包含公开目录。将现有 `PlaybackRuntime` 的实例入口职责整理为公开 `Player`，将 `PlaybackController`、`VideoCtl`、原生信号及解码线程结构收为私有实现。优先演进、合并已有入口，不长期叠加 `Player → PlaybackRuntime → Controller` 的机械转发链；内部辅助类可按实际职责保留，但不能形成另一套公开 API 或重复状态机。

首版默认将 `play_core` 构建为一个静态 SDK 库，由宿主链接；不强制改变宿主的 `BUILD_SHARED_LIBS`。动态库构建、独立二进制发行及 C ABI 适配留待实际需求出现后单独设计和验证，不默认支持。静态 SDK 不等于所有第三方依赖都静态链接，也不代表最终应用无需部署第三方 DLL。

## 4. C++ 公开接口

### 4.1 类型与实现边界

公开类型使用 `play_core` 命名空间，典型包含方式为 `#include <play_core/player.h>`。允许使用 `std::string`、`std::vector`、`std::optional`、智能指针和 C++ 回调类型，无需为了 C ABI 转换为不透明 C 句柄或手工 retain/release。

| 拟定类型 | 职责 |
| --- | --- |
| `Player` | 一个独立的播放实例，负责控制与生命周期 |
| `MediaSource` | 文件路径、网络地址、连接参数与音频开关 |
| `PlayerOptions` | 硬解偏好、循环模式等实例配置 |
| `MediaInfo` | 时长、尺寸、轨道、直播属性及可定位能力 |
| `PlayerError` | 错误分类、详细信息和底层诊断信息 |
| `VideoFrame` | 只读视频帧、格式信息及明确的数据有效期 |
| `SubtitleFrame` | 文本或位图字幕及显示时间 |
| `Subscription` | 事件订阅及断连生命周期管理 |

公开头文件不出现 `AVFrame`、`AVMediaType`、`AVRational`、SDL、sigslot 或 `VideoCtl`；媒体类型、宽高比和颜色信息使用 SDK 自有定义。`Player` 可用 PImpl 隐藏实现、减少编译依赖，但不据此承诺 ABI 稳定，也不要求整个 SDK header-only。

`Player` 不可复制；首版可保持不可移动，通过 `std::unique_ptr<Player>` 转移所有权，避免在活动回调和线程关联中移动对象。帧及媒体信息以值或只读共享对象表达，不让宿主持有内部可变状态的裸指针。

### 4.2 资源与错误规则

- 播放对象及订阅由 RAII 管理，析构遵守第 5 节的线程与等待约束；智能指针不能替代明确的关闭流程。
- 视频帧采用只读共享所有权，例如 `std::shared_ptr<const VideoFrame>`。内部不得在发布后修改宿主可见的数据，帧缓冲区也必须满足这一约束。
- 第一版沿用 CPU 帧路径，定义像素格式、plane、stride、尺寸、宽高比、旋转、颜色信息、时间戳及未知时间戳的表示。帧数据视图只在持有对应帧对象期间有效。
- 帧和只读媒体信息快照可独立于播放器存活。持有旧帧不能阻止会话关闭；宿主长期保存帧造成的内存占用由宿主负责。
- 字符串统一采用 UTF-8，定义文件路径和 URL 的区分规则。异步请求在返回前复制或取得输入数据所有权，不保留临时字符串、引用或 `string_view`。
- 普通借用事件参数仅在回调期间有效；需要保留时复制值或保存共享所有权。回调捕获的宿主对象必须存活至注销完成，不默认认为捕获 `this` 安全。
- 预期的参数和播放错误使用 SDK 结果类型或异步错误事件，不依赖全局 last-error。C++20 下定义项目自己的结果类型，不要求 C++23 的 `std::expected`。
- 阶段 1 明确构造、资源分配失败及各公开方法的异常策略。析构不抛异常，内部工作线程设置异常处理边界；宿主回调不得抛出异常逃逸到派发线程。

### 4.3 播放控制

拟定控制操作：

```cpp
open(source);
play();
setPaused(true);
seek(positionMs);
stop();
close();

setVolume(volume);
setPlaybackRate(rate);
selectAudioTrack(trackId);
selectSubtitleTrack(trackId);
```

这些仅展示命名，不是完整声明。实现前必须固定以下契约：

- `open()` 建立媒体会话，`play()` 请求播放；允许打开后排队请求播放，不把打开成功当作首帧已显示。
- 暂停采用明确设置语义，而不是只有切换操作。
- `stop()` 停止推进播放但保留已打开媒体；`close()` 关闭媒体会话并释放资源。停止后的播放起点按点播、直播分别定义，不隐含所有媒体都能回到起点。
- 打开、定位和关闭等耗时操作异步完成。同步结果只表示是否受理，不能表示播放成功。每个已接受的异步请求分配 `OperationId`，最终必须有且仅有一次成功、失败、取消或被替代的终结通知，并携带 `SessionId`。关闭或切换时也要结清旧请求。
- 定位完成必须区分请求位置与实际位置，明确其代表解封装定位完成还是目标附近首帧就绪；轨道切换同样需要定义完成与失败语义。
- 时间统一为 64 位毫秒，未知时长通过 `std::optional` 表达，不能用 `0` 冒充。当前整数秒进度需要从核心补齐精度，不能简单乘以 1000。
- 明确音量、倍速范围、关闭字幕的表示和轨道标识规则；不能把 UI 当前可选项当作核心已验证能力。
- 直播流是否能够定位以媒体能力为准，不对不可定位输入承诺 seek。

现有 `StartPlay()` 将打开和播放合并，暂停是 toggle，停止路径会释放会话。上述契约需要调整内部生命周期，不能只给现有方法改名。阶段 1 必须给出状态转换表，明确各状态下的合法命令、连续打开的替代规则及直播停止策略；保存源地址后重新打开不等于保留已打开媒体。

### 4.4 状态与事件

至少提供状态变化、媒体信息、播放位置、操作完成、视频帧、字幕、自然播放结束、错误和关闭完成事件。

状态需要区分未打开、打开中、就绪但未播放、播放、暂停、重连、停止、关闭中、自然结束和失败。缓冲状态仅在有真实检测与触发依据后开放，不能把枚举存在当作行为已经实现。

统一同一会话内事件顺序、错误后的终态和重复关闭行为。播放列表中的“下一项”“随机播放”等属于应用业务，不作为 SDK 核心事件。

## 5. 线程与生命周期

### 5.1 控制与事件线程

每个实例内部串行执行控制命令。普通控制请求允许从宿主不同线程提交，按受理顺序执行；耗时操作不阻塞宿主 UI 等待旧播放线程退出或网络读取。销毁不得与其他 API 调用并发，宿主负责停止使用该对象。

事件由每实例的串行派发上下文交付，不直接在内部解码、读取或刷新线程上执行宿主业务回调。Qt 包装层将事件投递到 QObject 所在线程。允许回调内提交异步控制请求，但禁止在回调内销毁播放器、同步等待关闭或执行等待自身回调退出的注销操作。

提供具有完成语义的回调注销操作：完成后无进行中或未来回调再访问该订阅的宿主上下文；已排队通知被取消。正常订阅存续期间保证已接受操作的终结通知，主动注销或销毁后不再保证向该订阅交付通知。订阅析构和显式注销的等待行为必须文档化，Qt 桥接遵守同一规则。

不能依赖宿主回调及时返回才能完成核心停止。媒体关闭先取消该会话任务并等待工作线程退出，再释放输出资源；关闭后播放器可再次打开。销毁则永久停止接收任务，并等待核心和在途回调结束。宿主回调必须最终返回；慢回调可以延迟通知与销毁返回，但不能阻止核心取消。

### 5.2 帧交接与会话隔离

C++ SDK 帧交付和 Qt 视频显示都采用最新帧邮箱或有界队列，不逐帧无限排队。宿主或 UI 卡顿时覆盖未呈现的旧帧；位置通知可合并，操作终结、错误和关闭完成不能静默丢弃。对命令及待交付结果设置容量预算，超过容量同步拒绝新请求，避免慢回调时结果无限增长。

每次打开媒体建立会话标识。快速执行 A → B → C 时，A、B 的延迟画面、状态、错误不能污染 C。帧和事件的过滤必须覆盖关闭与重新打开，不只覆盖正常切换。旧操作的取消或被替代通知仍按旧会话编号交付用于请求结算，不得更新新会话状态。

### 5.3 销毁与共享资源

- 显示目标解绑后不得继续访问其窗口句柄。
- 明确播放对象与显示控件的所有权和销毁顺序，支持先销毁控件或先关闭播放器。
- 正常退出优先走异步关闭；`Player` 析构保留安全回收职责，不承诺瞬时完成。销毁返回后不再有该播放器的回调。
- 关闭播放器后，宿主持有的帧仍可读取和安全释放，不能引用已经释放的播放器内部资源。
- 一个实例关闭不得破坏其他实例或宿主已有的 SDL 状态。
- 验证共享初始化引用计数、宿主先初始化 SDL、实例反复创建销毁等场景。

当前 `PlaybackRuntime` 析构会等待停止，原生信号不负责自动切换到 GUI 线程；源码交付和 RAII 都不能自动解决这些问题。

## 6. Qt 嵌入组件

提供两个主要对象：

- `QtPlayer`：直接使用 `play_core` 的公开 C++ API，提供播放控制、属性、信号及播放对象生命周期管理，不包含其私有头文件。
- `VideoWidget`：画面、比例适配与字幕显示，可直接加入 Qt 布局。

拟定使用体验：

```cpp
auto* player = new QtPlayer(this);
auto* view = new VideoWidget(this);

view->setPlayer(player);
layout->addWidget(view);

connect(player, &QtPlayer::errorOccurred,
        this, &MainWindow::showPlaybackError);

player->open(QUrl::fromLocalFile(filePath));
player->play();
```

抽取并统一两个应用的桥接和显示实现，不直接将 `MainWid`、`Show` 或监控业务窗口作为公开 SDK 控件。控件不包含工具栏、播放列表、业务菜单和持久化设置。

字幕渲染相关 libass 依赖留在需要该能力的组件中，纯 C++ 使用者不因 Qt 控件被迫引入 UI 依赖。Qt 层不另建播放状态机，不复制核心播放逻辑；QObject 操作遵守其线程亲和性，不能直接继承 Core 的多线程调用许可。

首版只支持一个播放实例绑定一个显示控件；多视图共享、QML 和自定义音频输出后续按实际需求扩展。

## 7. 源码接入、构建与发行

### 7.1 主路径：CMake 子项目

拟定发布包结构如下，保留 `play_core` 作为 SDK 本体。包根 CMake 只负责选择组件，不生成额外的核心包装库；交付物必须包含构建脚本和依赖说明，不能依赖未交付的应用目录：

```text
PlayerSDK/
  CMakeLists.txt
  cmake/
  play_core/
    CMakeLists.txt
    include/play_core/
      player.h
      media_source.h
      video_frame.h
    src/                   # Player 实现及原有解码、同步等私有代码
  qt/                      # 可选 player_sdk_qt 组件
  examples/
  tests/
  licenses/
  README.md
```

拟定无 Qt consumer：

```cmake
cmake_minimum_required(VERSION 3.25)
project(host_app LANGUAGES CXX)

add_subdirectory(third_party/PlayerSDK)

add_executable(host_app main.cpp)
target_link_libraries(host_app PRIVATE PlayerSDK::Core)
```

拟定 Qt consumer（在 `add_subdirectory()` 前启用组件）：

```cmake
set(PLAYER_SDK_BUILD_QT ON CACHE BOOL "Build Qt Widgets integration")
add_subdirectory(third_party/PlayerSDK)

target_link_libraries(my_qt_app PRIVATE PlayerSDK::QtWidgets)
```

`PlayerSDK::Core` 是 `play_core` target 的 CMake 别名，不是新增的一层库；`PlayerSDK::QtWidgets` 对应可选 Qt 组件，链接 `PlayerSDK::Core` 和 Qt 依赖。`PlayerSDK` 只是发布包与 targets 的命名，不表示存在 `player_sdk` 实现层。不额外引入 `PlayerSDK::Cpp`。源码还可以由宿主通过 `FetchContent` 获取，但版本必须固定到发布标签或提交，不依赖浮动分支；获取方式不改变构建契约。

### 7.2 子项目构建约束

- SDK 子项目默认只构建 Core；Qt、示例、测试和两个应用分别使用带项目前缀的选项，嵌入时默认关闭。保留仓库顶层开发模式下显式构建应用和测试的能力。
- 不强制修改宿主的 `BUILD_TESTING`、`BUILD_SHARED_LIBS`、输出目录、安装前缀、CRT 或全局编译标志。SDK 测试不能仅因宿主启用测试就自动加入。
- 使用 target 级 `target_compile_features`、包含路径、编译定义和链接依赖；C++20 要求准确传递给 consumer，警告及 sanitizer 选项不传播给宿主。
- 内部路径以 SDK 自身目录为基准，不假定 `CMAKE_SOURCE_DIR` 就是 SDK 根目录，不写入宿主源码树，也不将成品固定输出到仓库 `bin`。
- Qt 的 AUTOMOC、资源和部署逻辑限制在对应组件；关闭 Qt 后不得查找 Qt 或 libass，也不得被 Qt 专属测试强制引入它们。
- `play_core` 仅公开自身的 `include/` 目录，`src/` 和第三方实现头文件目录使用私有包含路径。内部白盒测试可以显式访问私有目录，但不得因此将它们传递给 consumer。静态链接所需依赖须由 targets 正确传递，不能为了“隐藏依赖”使宿主最终链接失败。
- 不擅自下载、安装依赖或改写宿主工具链。优先复用兼容的已有依赖 targets；缺失时按文档查找并给出明确错误。

### 7.3 依赖与交付内容

发布固定版本的源码归档或 Git 标签，并提供：

1. 最低 CMake、C++、MSVC、Windows 和 Qt 版本及验证矩阵。
2. FFmpeg、SDL、SoundTouch、sigslot、libass 的来源、版本、构建选项和获取步骤；可提供锁定版本的包管理配置。
3. 第三方代码随包交付还是由宿主提供的明确清单，不依赖开发机绝对路径或未交付目录。
4. 仓库外最小 C++ 示例与 Qt Widgets 示例，说明错误处理、帧消费、订阅和退出流程。
5. 运行时 DLL、Qt 插件等部署清单；SDK 本身静态链接时，动态第三方依赖仍需部署。
6. API 文档、版本变更、迁移说明、许可证和版权声明。

SDK、宿主及第三方二进制仍需匹配架构、兼容工具链和运行时配置。源码交付不会自动修复不兼容的第三方库，也不能消除同名不同版本 DLL 的加载冲突。

安装及 `find_package(PlayerSDK CONFIG ...)` 可作为后续便利能力：由使用方先从源码构建、安装，再在同一兼容构建环境消费。它不属于首版必要验收项，也不意味着提供可任意替换的稳定二进制 SDK；实现时须补齐静态依赖导出和安装重定位验证。

### 7.4 源码 API 版本政策

- 发布版本号与变更记录，明确哪些公开头文件属于受支持接口；内部头文件不保证兼容。
- 稳定发布后，同一主版本尽量保持公开源码 API 兼容；破坏性变更提升主版本并提供迁移说明，必要时保留弃用过渡接口。
- 升级源码后重新编译 SDK 和消费代码，不承诺旧目标文件、旧 EXE 或预编译 Qt 包装可直接复用。
- 使用旧版 consumer 源码重新构建验证源码兼容性，不再以“旧二进制替换新版 DLL”作为验收标准。
- 暂不设计 C 导出、ABI 版本协商、`struct_size` 或导出符号基线。以后确有独立 DLL 升级或跨语言需求时，再增加单独的 C ABI 适配层。

## 8. 原实施顺序（历史）

| 阶段 | 工作 | 完成标准 |
| --- | --- | --- |
| 1. 固定公开契约 | 定义 C++ 头文件、状态转换表、结果类型、操作编号、线程和所有权 | 接口不泄漏内部类型，能力与限制有明确说明 |
| 2. 核心目录与接入整理 | 在 `play_core` 内划分公开/私有目录，改造其 CMake 并提供 Core 别名、依赖说明和仓库外 consumer | 不新增核心包装库，宿主通过 `add_subdirectory()` 编译链接，不需要 Qt 或内部头文件 |
| 3. 原地改造播放接口 | 将现有运行时入口整理为 `Player`，在同一库内完善生命周期、命令与事件、时间精度、会话隔离和 RAII | 无 Qt 示例可播放并接收帧，生命周期测试通过 |
| 4. 抽取 Qt 组件 | 合并桥接、帧交接、显示和字幕处理 | 仓库外 Qt 窗口无需复制应用代码即可嵌入播放 |
| 5. 迁移两个应用 | 改为使用公开 C++ SDK 和 Qt 组件 | 原有功能不回退，不再直接访问核心内部对象 |
| 6. 源码发行验证 | 依赖、许可、干净环境、宿主共存和源码兼容测试 | 仅凭发布源码和依赖说明即可接入、构建和运行 |

先固定公开契约，再做封装与目录调整。每阶段复用已有核心测试，避免先整体搬迁再集中排查回归。源码接入测试前移，不等两个仓库内应用迁移完才验证外部宿主。

## 9. 首版验收清单（历史，非 v2.1.0 签收）

- [ ] 本地文件、网络源、暂停、倍速、定位、音轨和字幕保持现有能力。
- [x] 快速连续打开媒体时，没有过期画面、状态或错误串入（自动生命周期/会话测试范围）。
- [x] 打开中、播放中和重连中关闭，不崩溃、不死锁（本地媒体及 loopback HTTP 测试）。
- [x] 定位与关闭并发时，事件不会作用于已关闭会话（会话过滤/唯一终结路径；既有定位与关闭回归）。
- [x] 两实例同时播放，关闭其中一个不影响另一个（本地媒体自动测试）。
- [x] 控件与播放对象按不同顺序销毁均安全（Qt 自动测试）。
- [x] UI 或 C++ 回调缓慢时，帧、命令和待交付结果不会无限积压（128 请求预算/最新帧测试）。
- [x] 宿主已有 SDL 初始化时可正常共存（反复双实例创建销毁测试）。
- [x] 打开成功但未请求播放时，不意外推进播放或输出声音（初始暂停路径/位置测试，不代替扬声器验收）。
- [x] 已接受异步请求有可关联且唯一的终结结果，注销行为符合约定（测试覆盖范围见实施记录）。
- [x] 回调内请求异步关闭不死锁；注销完成或析构返回后不再访问宿主回调上下文（RAII/回调关闭测试）。
- [x] 关闭播放器后，保留的帧仍可读取并安全释放；RAII 和异常策略经过测试。
- [ ] UTF-8 中文路径及带凭据 URL 正确处理，公开错误与日志不泄露凭据。
- [x] Core SDK 就是改造后的 `play_core` target，没有额外的核心包装库或重复公开运行时入口。
- [x] 外部应用及 Qt 组件只包含 `sdk/core/include/` 中的公开头文件，不访问 `VideoCtl`、内部 controller 或原生信号。
- [x] 仓库外 C++ 工程通过 `add_subdirectory()` 接入，不需要 Qt 或内部头文件。
- [x] 仓库外 Qt Widgets 工程仅链接公开 target 即可显示视频和字幕（独立 consumer 编译/启动，公开 Qt 测试验证视频与字幕）。
- [x] 嵌入时不自动构建 SDK 应用、测试和示例，不改变宿主输出目录、CRT 或全局选项。
- [x] 发布源码移到新路径后仍能构建，构建不依赖原开发机路径或未交付文件（外部依赖由明确前缀提供）。
- [ ] 按文档准备依赖后，在干净环境构建和运行，不依赖未声明的环境变量。
- [x] 本机已验证组合的 Debug/Release 构建验证通过，第三方运行依赖完整；更广支持矩阵仍未验证。
- [ ] 旧版 consumer 源码可按源码兼容政策重新构建，破坏性变更有迁移说明。
- [ ] 现有两个应用迁移后完成真实 UI 回归，不仅通过编译和单元测试。

多实例能力、回调重入保证、跨平台行为和性能上限均需实测，当前不作为已验证结论。

## 10. 首版非目标与许可证

首版不包含 C ABI、稳定二进制 ABI、预编译 SDK 发行、动态 SDK 构建承诺、跨语言绑定、媒体库、播放列表业务、监控布局、摄像机管理、录屏、推流、转码、视频编辑、QML 或自定义音频输出。

当前硬解为 Windows D3D11VA 路径，硬件帧会下载到 CPU；不承诺 GPU 零拷贝、HDR 或跨平台硬解。

仓库根目录 [LICENSE](../LICENSE) 为 GPLv3。源码 SDK 不会自动改变现有代码和依赖的授权条件，对外发行前必须核对：

- 核心代码来源、修改权和再授权条件。
- FFmpeg 实际构建配置及启用组件的许可证。
- Qt、SDL、SoundTouch、sigslot、libass 等实际发行依赖的许可义务。
- 许可证文本、版权声明、对应源码或源码获取方式等交付要求。

让使用方自行编译、静态链接或直接复制源码，都不自动意味着允许闭源分发。若目标包含闭源商业宿主，应先完成授权核查，再确定发行政策。

## 11. 实施与验证记录（2026-11-06，首版提交快照）

本节保留提交 `5efab6a` 的 QPainter 显示实现及当时验证结果。后续恢复共享 SDL 合成的历史见第 12 节；当前 Windows 原生显示与非 Windows 软件读回的区别见第 1 节。本节 QPainter 描述和旧 ZIP 不代表当前全部平台的后端与交付版本。

### 已完成的实现

1. **公开边界**：`sdk/core/include/play_core/{player,media_source,video_frame,network_runtime}.h`；公开类型无 FFmpeg/SDL/sigslot 依赖。`sdk/core/src/` 收拢原管线。`PlayerSDK::Core` 是原静态 `play_core` 的 alias，无额外 Core 包装库。
2. **入口与生命周期**：`Player::Impl` 直接管理私有 `VideoCtl`，不经过旧 Runtime/Controller；独立串行命令和事件上下文、operation/session、唯一结算、128 请求预算、最多 32 订阅、帧/位置/字幕合并。旧 Runtime 仅在仓库白盒测试模式编译，不作为源码 SDK 接口。
3. **控制语义**：open 初始暂停、play 可排队、设置式暂停、保留会话 stop、异步 close、毫秒主时钟/定位结果、轨道应用结果、重连取消/恢复、最终错误分类；位置和帧 PTS 归一化媒体起始偏移。完整合法状态表与异常/注销约束见 [SDK_README](../sdk/README.md)。
4. **帧与字幕**：发布独立不可变 CPU 存储，持帧不阻止关闭；内嵌文字/ASS 包不再被旧位图格式检查丢弃，保留样式头；Qt 共享视频/字幕显示。
5. **Qt 与应用**：可选 `PlayerSDK::QtWidgets`，`QtPlayer` 有 QObject 线程投递和 UI 侧预算，`VideoWidget` 有单视图绑定/双向销毁保护。两个应用改为仅使用公开 SDK；播放器播放列表/历史与监控录制仍在应用。
6. **源码发行**：默认 Core-only、外部 consumer 构建隔离、Core/Qt 示例、公开 API 测试、依赖/部署/许可清单、迁移说明和 `sdk/CHANGELOG.md`；`scripts/package-sdk.cmake` 生成不含 apps/build/DLL 的独立源码包。

### 本机验证环境与结果

本机 Windows 11 x64；CMake 4.3.3；Visual Studio 18 2026 / MSVC 19.51（工具目录 14.51.36231）；Qt 6.9.3 msvc2022_64；FFmpeg 9.0.1、SDL 2.32.10、SoundTouch 2.4.1、sigslot 1.2.3、libass 0.17.5。只把此组合记为实测，不据最低版本声明扩大支持矩阵。

| 验证 | 实际结果 | 本地证据 |
| --- | --- | --- |
| Debug 全量构建/测试 | **46/46 通过** | `build/sdk-debug-build.log`、`build/sdk-debug-tests.log` |
| Release 全量构建/测试 | **46/46 通过** | `build/sdk-release-build.log`、`build/sdk-release-tests.log` |
| 公开 Core 生命周期 | 打开不推进、play/pause、1234ms 定位、stop/restart、A→B→C、双实例/关闭、保留帧、重复 close/reopen、UTF-8 中文路径、慢回调容量、回调内 close、异常隔离通过 | `sdk/tests/api/player_sdk_tests.cpp` |
| SDK 网络取消 | loopback HTTP 的打开中关闭、10 秒重连退避中关闭，旧 open 取消结算且 close 在测试的 1500ms 窗口内完成 | `tests/network_integration_tests.cpp` |
| SDL 共存 | 宿主先初始化 SDL 后，反复双实例创建/销毁不破坏宿主 subsystem | `sdk/tests/api/runtime_coexistence_tests.cpp` |
| Qt 真实呈现 | 视频帧呈现、UI 停顿后最新帧、请求容量、UTF-8 SRT、先销毁 player/view 和单视图切换通过；另以 windows 平台运行并检查截图，画面/字幕可见 | `sdk/tests/api/qt_player_tests.cpp`、`build/sdk-widget-final.png` |
| 监控真实集成 | 16 路本地媒体、切源、销毁窗格、禁音频拒绝纯音频、2 秒分段录制、主窗口 smoke 通过（Debug/Release） | `monitor_integration_tests`、`monitor_window_smoke_test` |
| 实际播放器路径 | Release 打开生成的 MPEG4/AAC 640×360 测试片，2 秒运行后正常 close/回收，报告无错误；QPainter 提交有计数，不当作 compositor 可见/性能上限结论 | `build/sdk-player-ui-schema2.json`（schema 2） |
| 仓库外无 Qt consumer | 独立临时目录、宿主 C++17/BUILD_SHARED_LIBS/BUILD_TESTING/CRT/输出目录隔离检查，Core 编译/运行通过；不查找 Qt | `build/sdk-consumer-configure.log`、`build/sdk-consumer-build.log` |
| 仓库外 Qt consumer | 仅公开 targets 编译，按宿主部署 Qt 运行库后启动/退出通过 | `build/sdk-qt-consumer-configure.log`、`build/sdk-qt-consumer-build.log` |
| 最终源码包移位 | ZIP 解压到仓库外，Core/Qt consumer、两示例、公开测试均在 Debug/Release 编译通过；三个 SDK 测试在两配置各 **3/3 通过**；两个 consumer 在两配置运行通过；Core console 完整播放 MPEG4/AAC 样本并自然结束 | `build/sdk-final-consumer-*.log`、`build/sdk-final-source-*-tests.log`、`build/sdk-source-console-playback.log` |

最终本地交付：`dist/PlayerSDK-1.0.0-source/PlayerSDK-1.0.0-source.zip`，SHA-256 见同目录 `.sha256`。这是本地源码归档，未提交 Git、未创建标签、未推送或发布到远端。生成目录日志/截图用于本次验证，不是源码依赖。

用户提供的 Qt 错误截图对应 `offscreen` 测试插件未部署：`windeployqt` 默认仅部署 windows，测试环境指定 offscreen，导致平台初始化失败。现已补齐测试平台插件复制，并在 Debug/Release 与独立包测试中复验通过，不需要重装 Qt。

### 明确保留的限制与待验收

- 初版倍速范围最终固定为 `[0.1,2.0]`，不是 UI 选项推导出的任意范围；所有媒体倍速音画同步仍需专项验证。
- seek 完成是解封装成功，`actualPositionMs` 是解封装目标，不是附近首帧时间；pending seek 可因 busy 异步失败，不伪报成功。轨道成功是实际应用/打开解码器，不是首帧确认。
- stop 保留媒体，可定位源 play 从 0 开始；不可定位直播恢复会话，可能有有界缓存延迟，不承诺实时边缘。SDK 使用 packet 背压，不沿用旧实时无限缓冲。
- 显示路径改为 swscale/QImage/QPainter，CPU 拷贝和显示成本可能高于旧 SDL 路径；旧基准性能不能沿用。本次没有 1080p/4K60、GPU、长时资源/同步压力和真实摄像机性能结论。
- 本次验证复用本机已声明的外部依赖安装前缀；**不是**干净机器/虚拟机验证。未验证所有最低 Qt/MSVC 版本组合或跨平台行为。
- 尚无已发布旧版公开 SDK consumer，因此不把本次相同 consumer 在源码归档重编等同于跨发布版本源码兼容验证。
- UTF-8 中文文件/字幕已测试，网络错误分类与关闭已测；所有带凭据 URL/第三方 FFmpeg 日志及实际认证服务仍需专项隐私/认证验证。SDK 自有诊断不输出原始地址，但不会覆盖宿主全局 FFmpeg 日志回调。
- 两应用已编译并通过上述真实 UI/集成路径，但没有宣称完整手工交互/格式/音轨字幕组合回归已全部完成；第 9 节相关广义验收项保持未勾选。
- GPL/第三方许可清单已提供；闭源商业授权、对应源码义务及最终二进制完整许可证审计没有替使用方完成。

## 12. SDL 显示恢复（2026-11-06）

按用户要求，先创建 `feature/player-sdk-sdl` 并以 `5efab6a` 提交第 11 节的完整 SDK 实现，保留可回退节点，再在该分支恢复 SDL 显示。Core 公开 API、异步生命周期和两个应用的 SDK 接入保持不变，本轮没有修改 `play_core`。

- 共享 `VideoWidget` 使用 Qt 管理的原生子窗口，SDL_CreateWindowFrom 绑定，优先硬件 renderer、失败时尝试 SDL software renderer；普通 YUV420P 直接上传 IYUV 纹理，BGRA 直接上传纹理。
- BT.2020 YUV 因 SDL2 没有对应转换模式，保留 swscale 转 BGRA 的定向回退，不让普通 YUV 帧全量转 RGB。SAR/旋转、位图和 libass 文字/ASS/SRT 字幕统一由 SDL 合成。
- QPainter 只保留空背景/业务占位，不绘制视频。QImage 仅在显式 `grabVideoFrame()` 时通过 SDL_RenderReadPixels 读回，用于截图/验证，不是常规播放中转。
- 原生窗口销毁前释放 SDL renderer/texture/window 包装；Qt 仍拥有原生窗口。每控件取得并释放自己的 SDL video subsystem 引用，不调用全局 SDL_Quit，不破坏宿主已有初始化。
- 无界面 offscreen/minimal 测试使用 SDL surface/software renderer，不把其结果当作 native/hardware 验证。实际 windows 平台另外运行画面/字幕及生命周期测试。
- 实际主窗口验证暴露“原生子窗口覆盖后，父控件 update 不一定触发绘制”的首帧停滞问题；改为 GUI 线程收到帧直接提交 SDL，并新增嵌套控件连续呈现回归。
- 基准报告升级 schema 3，统计 SDL_RenderPresent 返回，不能据其返回推断显示器可见或 GPU 完成；schema 2 保留作之前 QPainter 快照的历史含义。

验证日志使用 `build/sdk-sdl-*.log`，本轮截图为 `build/sdk-sdl-widget.png`，实际播放器报告为 `build/sdk-sdl-player-ui.json`。之前 `dist/PlayerSDK-1.0.0-source/` 是提交 `5efab6a` 的 QPainter 快照，不覆盖它。

最终验证结果：

| 项目 | 结果 |
| --- | --- |
| Debug 完整构建/回归 | **46/46 通过**；`build/sdk-sdl-debug-tests.log` |
| Release 完整构建/回归 | **46/46 通过**；`build/sdk-sdl-release-tests.log` |
| 原生 windows SDL 显示 | 视频/字幕、嵌套控件连续呈现、BGRA/IYUV、BT.2020 回退、SAR/90° 旋转、resize/clear、控件/播放器销毁顺序、宿主 SDL video 初始化共存均通过；SDL 读回截图已检查 |
| 实际 MyPlayer 主窗口 | MPEG4/AAC 640×360 样本播放 2 秒，139 次 GUI 帧交付、137 次新帧 SDL present 返回，正常关闭回收且无错误；这些不是解码帧率或屏幕可见性计数 |
| 监控 | 全量套件中的 16 路本地预览/切源/销毁/分段录像和窗口 smoke 通过 |
| 仓库外 SDK 源码包 | 新输出 `build/sdk-sdl-source-final/PlayerSDK-1.0.0-source.zip` 解压到临时目录，Release consumer 编译、三个 SDK 测试 **3/3 通过**，原生 Qt consumer 启动/退出通过；见 `build/sdk-sdl-consumer-*.log` |

当时 SDL 后续改动按用户要求作为独立提交保存在 `feature/player-sdk-sdl` 分支且尚未推送；此句仅记录当时状态，不描述当前远端或标签。该轮仍未宣称完成长时间/GPU/4K 性能、真实摄像机及干净机器验证。v2.1.0 文档核对没有重跑表内验证。

