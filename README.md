# MyPlayer 与 MyPlayer Monitor

本仓库包含两个 Windows 桌面软件，共用 C++20 播放核心 `play_core`：

| 软件 | 用途 | 源码入口 | CMake 子目录 | CMake 目标 |
| --- | --- | --- | --- | --- |
| MyPlayer | 媒体播放、播放列表、字幕、播放设置 | `apps/qt_player/main.cpp` | `apps/qt_player/CMakeLists.txt` | `myplayer` |
| MyPlayer Monitor | 多路视频预览、设备管理、录像与录像回放 | `apps/monitor/main.cpp` | `apps/monitor/CMakeLists.txt` | `myplayer_monitor` |

技术栈：Qt 6 Widgets、FFmpeg、SDL2、SoundTouch、sigslot；可复用 Qt 组件使用 libass 渲染字幕，两个应用共用该组件。项目基于 GPL 上游 [itisyang/playerdemo](https://github.com/itisyang/playerdemo)，保留原作者归属与许可证。

- 主仓库：<https://github.com/dfasdsfffff/myplayer>
- Gitee 镜像：<https://gitee.com/hu-xiao-yun/myplayer>
- GitHub 发行版：<https://github.com/dfasdsfffff/myplayer/releases>
- [源码 SDK 接入、API 契约与发行](sdk/README.md)
- [C ABI 接入与兼容约定](sdk/c/README.md)
- [SDK 方案及实施验收记录](docs/PLAYER_SDK_PLAN.md)
- [开发与架构指南](docs/PROJECT_GUIDE.md)
- [监控软件使用、配置与独立构建](apps/monitor/README.md)
- [播放核心历史检查记录](docs/playback-core-review.md)
- [第三方组件与许可证](THIRD-PARTY-NOTICES.md)

## 下载与运行

[2.0.1 修复版](https://github.com/dfasdsfffff/myplayer/releases/tag/2.0.1) 提供两个独立的 Windows x64 Release 免安装包：

| 文件 | 完整解压后启动 |
| --- | --- |
| `MyPlayer-2.0.1-windows-x64.zip` | `bin/myplayer.exe` |
| `MyPlayerMonitor-2.0.1-windows-x64.zip` | `bin/myplayer_monitor.exe` |

每个 ZIP 均附有 `.sha256` 校验文件。下载后可用 `Get-FileHash <ZIP路径> -Algorithm SHA256` 比对。保留整个目录结构，不要只复制 EXE。包中包含 Qt 插件、媒体依赖、app-local MSVC 运行库和许可证，不需要另装 Qt 或 VC++ 运行库；录像等写入操作需要目录写权限。

“免安装”是部署方式，不代表所有设置都保存在程序旁边。播放器设置与监控配置的存储方式不同，见开发指南及监控 README。

发行包版本与程序内版本目前尚未统一：本次发行标签和包名为 `2.0.1`，顶层/监控 CMake 项目及两个 `main.cpp` 中的应用版本仍为 `1.0.0`。包名参数不会自动更新这些代码元数据。

GitHub 发行页提供 `2.0.1` 修复版及附件；Gitee 同步源码与标签，但尚未创建对应发行页。

**2.0.1 播放器修复**：修复菜单关闭时 `Ctrl+O` 等快捷键无效、黑色音频占位标签覆盖视频，以及 Windows 高 DPI 下窗口边缘缩放坐标不一致的问题。视频与音频占位层互斥显示；Enter/空格统一由菜单动作处理，避免一次按键重复切换；原生边缘命中判断统一使用物理屏幕像素。原始 `2.0.0` 下载包不包含这些修复，请升级至 `2.0.1`，不要继续使用旧版播放器附件。

发行验收不能只运行 `--smoke-test`：还需实际通过 `Ctrl+O` 选择文件，确认视频画面可见、暂停/恢复可用，验证视频→音频→视频切换，以及高 DPI 下拖拽窗口边缘、暂停画面缩放和最大化/还原。启动成功或 SDL present 计数不代表最终画面可见。

## 从源码构建

以下 PowerShell 命令均从仓库根目录执行。`build/`、`bin/`、`.cache/` 都是生成目录，干净检出或清理后不存在是正常的；不要依赖历史构建缓存或其他机器的绝对路径。

### 环境与依赖

- CMake 项目及 Preset schema 最低要求 **3.25**，但下文默认的 [Visual Studio 18 2026 生成器](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html) 从 **CMake 4.2** 才提供；照用该生成器时需 **4.2+**。
- C++20 编译器。默认 Preset 使用 **Visual Studio 18 2026**、MSVC x64；需安装桌面 C++ 工作负载及 Windows SDK。
- Qt 6 Core/Gui/Widgets；监控程序明确要求 **Qt 6.8+**。现有 Windows 发行使用 Qt **6.9.3 / msvc2022_64**，这是 Qt SDK 目录名，不是本仓库 Preset 的 VS 生成器名称。
- vcpkg，设置 `VCPKG_ROOT` 为其根目录。`vcpkg.json` 声明 FFmpeg、libass、SDL2、SoundTouch；sigslot 使用仓库内的头文件。
- 播放器通过 `pkg_check_modules(LIBASS ... libass)` 查找 libass，需要可用的 pkg-config/pkgconf 及与当前 Release/Debug、x64 依赖相匹配的元数据。

顶层构建默认从配置好的依赖安装前缀查找 FFmpeg/SDL2/SoundTouch，**不会自动用 `lib/` 中的二进制替代缺失的 vcpkg 包**。仅监控独立构建有 `MONITOR_USE_BUNDLED_DEPS` 开关。

### 构建两个软件

将以下两个路径替换为本机实际安装目录；首次配置会由 vcpkg 安装 manifest 依赖，可能需要下载和编译：

```powershell
$env:VCPKG_ROOT = "C:/src/vcpkg"
$qtRoot = "C:/Qt/6.9.3/msvc2022_64"

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DPLAYERDEMO_QT_ROOT="$qtRoot" `
  -DMYPLAYER_BUILD_QT_APP=ON -DBUILD_TESTING=ON `
  -DPLAYER_SDK_BUILD_TESTS=ON `
  -DMYPLAYER_BUILD_MONITOR=ON `
  -DMONITOR_BUILD_TESTS=ON

cmake --build build --config Release --target myplayer myplayer_monitor
```

也可以使用现有 Preset，并显式打开监控目标：

```powershell
cmake --preset windows-msvc-vcpkg -A x64 -T host=x64 `
  -DPLAYERDEMO_QT_ROOT="$qtRoot" `
  -DMYPLAYER_BUILD_MONITOR=ON -DMONITOR_BUILD_TESTS=ON
cmake --build --preset windows-msvc-vcpkg-release
```

Preset 本身只显式启用播放器，不会自动启用监控；Release 没有对应的 test preset，测试时使用下面的 `ctest --test-dir ... -C Release`。

默认的 VS 多配置输出：

```text
bin/myplayer.exe                         # Release 播放器
bin/myplayer_debug.exe                   # Debug 播放器
bin/myplayer_monitor.exe                 # Release 监控软件
bin/myplayer_monitor_debug.exe           # Debug 监控软件
```

播放器构建会复制可由 CMake 目标识别的运行库及 Qt platform 插件，监控构建还会调用 `windeployqt`。构建输出不等于完整免安装发行包；第三方 DLL 的间接依赖、Qt 插件、MSVC 运行库及许可证部署见“打包”。

### 构建开关

| 选项 | 默认值 | 作用 |
| --- | --- | --- |
| `MYPLAYER_BUILD_QT_APP` | `OFF`（Preset 为 ON） | 播放器及对应 UI 测试 |
| `MYPLAYER_BUILD_MONITOR` | `OFF` | 顶层构建中加入监控软件 |
| `BUILD_TESTING` | `OFF`（Preset 为 ON） | 仓库白盒/应用回归；不替代 SDK、监控专用开关 |
| `PLAYER_SDK_BUILD_QT` | `OFF` | 可复用 Qt 组件；构建应用时自动引入 |
| `PLAYER_SDK_BUILD_C` | `ON` | 构建版本化 C ABI 共享库 `PlayerSDK::C`；只需 C++/Qt SDK 时可关闭 |
| `PLAYER_SDK_BUILD_EXAMPLES` | `OFF` | 公开 Core/Qt 示例 |
| `PLAYER_SDK_BUILD_TESTS` | `OFF` | SDK 公开接口测试，不随宿主 BUILD_TESTING 自动开启 |
| `MONITOR_BUILD_TESTS` | `OFF` | 监控集成测试和窗口 smoke 测试，需启用监控子项目 |
| `MYPLAYER_ENABLE_WARNINGS` | `ON` | 项目警告设置 |
| `MYPLAYER_ENABLE_SANITIZERS` | `OFF` | 支持的编译器上的 ASan/UBSan，非 Windows GUI 验证 |

`PLAYERDEMO_QT_ROOT` 指向 Qt SDK 前缀；`PLAYERDEMO_OUTPUT_DIR` 控制两个应用的默认运行输出位置，默认是仓库 `bin/`；`PLAYERDEMO_SIGSLOT_ROOT` 默认指向 `sdk/third_party/sigslot`。`PLAYERDEMO_DEPS_ROOT` 不是启用顶层 bundled 二进制依赖的开关。

### 仅播放核心

```powershell
cmake -S . -B build-core -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DMYPLAYER_BUILD_QT_APP=OFF -DMYPLAYER_BUILD_MONITOR=OFF `
  -DBUILD_TESTING=ON -DPLAYER_SDK_BUILD_TESTS=ON
cmake --build build-core --config Release
ctest --test-dir build-core -C Release --output-on-failure
```

播放核心不链接 Qt，但仍需 FFmpeg、SDL2、SoundTouch 和 sigslot。关闭 UI 不会自动精简 `vcpkg.json` 的安装依赖。

## 测试与验证

只构建两个应用目标不会同时构建全部测试。需要测试时先构建当前配置的全部目标：

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -N
ctest --test-dir build -C Release --output-on-failure
```

测试按构建开关分组，实际注册项以顶层和监控目录的 `CMakeLists.txt`、`ctest -N` 为准，不维护固定测试数量。

- 播放核心：队列、命令邮箱、解码、音频输出、seek/生命周期、网络读取、硬解策略和媒体兼容性等。
- 播放器：播放列表、隐私处理、设置、字幕、主窗口和视频呈现等；部分使用 offscreen/dummy 后端。
- 监控：`monitor_startup_tests` 验证音频初始化失败时预览窗不会崩溃；`monitor_integration_tests` 覆盖多路预览、换源和录像；`monitor_recording_browser_tests` 覆盖录像检索；`monitor_window_smoke_test` 检查窗口启动。均需 `MONITOR_BUILD_TESTS=ON`。
- `myplayer --smoke-test` 只创建 QApplication 和播放运行时后退出，**不会打开完整播放器主窗口，也不会播放媒体**。
- 监控 `--smoke-test` 会显示窗口并在约一秒后退出；未传入视频源时不能证明实际解码、联网或录像可用。

CI 定义见 [.github/workflows/windows-build.yml](.github/workflows/windows-build.yml) 和 [Clang sanitizers workflow](.github/workflows/clang-sanitizers.yml)。当前 Windows workflow 尚未启用监控，却调用要求双应用的打包脚本，并仍传入包版本 `1.0.0`；不能把该 workflow 视为已经同步或通过验证的双软件发行流程。本文档不修改 CI 实现。

### 媒体兼容性与硬解回退

需 `BUILD_TESTING=ON`。外部 `ffmpeg` 命令行工具与链接的 FFmpeg 库是两件事；以下脚本要求外部工具支持 libx264、libx265、AAC、MPEG4，MP3 用例取决于编码器可用性：

```powershell
cmake --build build --config Release --target media_compatibility_tests hardware_decode_policy_tests
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify-media-compatibility.ps1 `
  -BuildDir build -Configuration Release -Ffmpeg ffmpeg
```

脚本在 `.cache/media-compatibility/<独立运行目录>/` 生成样本、日志与 `compatibility.json`。运行时播放结果与直接硬解/帧传输结果分开记录。软件回退成功不等于覆盖了 GPU 硬解；这些用例不能代替扬声器输出、音画同步、所有 GPU/编码档次或真实网络源验收。

### 视频呈现性能

在可交互的 Windows 桌面运行 Release 播放器；需要外部 `ffmpeg` 提供 libx264：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/benchmark-video-presentation.ps1 `
  -Player bin/myplayer.exe -Ffmpeg ffmpeg -RunMs 10000 -TimeoutMs 30000
```

脚本生成 1080p60/4K60 视频，输出 `.cache/benchmarks/baseline.json`、播放报告、进程 CPU/内存采样和日志。`-SkipFixtureGeneration` 复用已存在的样本；`-SkipDecodeReference` 跳过 FFmpeg null-output 解码对照。

已有视频也可直接测量，先创建报告目录：

```powershell
New-Item -ItemType Directory -Force .cache/benchmarks | Out-Null
bin/myplayer.exe --benchmark --benchmark-media C:/media/video.mp4 `
  --benchmark-output .cache/benchmarks/player.json `
  --benchmark-run-ms 10000 --benchmark-timeout-ms 30000
```

基准模式请求软件解码、1 倍速、关闭循环/断点恢复及 1280×720 窗口。SDK 共享控件恢复 SDL 后，播放器报告使用 schema 3 / `myplayer_qt_sdk_playback`，以 `video_frame_sdl_present_returns` 记录 SDL 呈现返回；schema 2 仅用于之前的 QPainter 实现。GUI 收到的帧数与共享 VideoWidget 的 SDL 呈现回调分别统计，不是解码帧/丢帧计数，也不证明屏幕实际可见。CPU 以一个逻辑核心为 100%；纯视频样本不测音画同步，短时结果不是长期稳定性结论。

## 双软件免安装打包

使用上文的 **Windows x64 / Visual Studio 多配置生成器** 同时启用两个应用；需要完整依赖安装目录、Qt 的 `windeployqt.exe`、Qt 许可证文本和可再分发的 MSVC CRT DLL。监控 EXE 从 `PLAYERDEMO_OUTPUT_DIR`（默认仓库 `bin/`）读取；脚本不承诺支持 Ninja 或仅监控 standalone 构建。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-portable.ps1 `
  -BuildDir build -Configuration Release -Version 1.1.1
```

`1.1.1` 仅是下一次打包的示例参数，不代表已经发布该版本。现有 `1.1.0` 包已存在时，脚本拒绝覆盖，需移动旧输出或使用未占用版本。

脚本会：构建两个应用；安装基础运行目录；分别部署 Qt 插件与 app-local MSVC CRT；复制依赖许可证；在移除开发搜索路径后运行各自的 smoke 模式；生成两个 ZIP 和 SHA-256 文件。它**不会**自动提交源码、创建 Git 标签或发布 GitHub/Gitee 发行页，也不会实际验证媒体播放/录像或干净虚拟机兼容性。

相关缓存项与可选参数：

- `PLAYERDEMO_QT_ROOT`、`VCPKG_INSTALLED_DIR`、`VCPKG_TARGET_TRIPLET` 必须对应本次构建的有效路径；后两项通常由 vcpkg toolchain 设置。
- `-QtLicenseDir` 默认使用 Qt SDK 前缀的同级 `Src/LICENSES`；只安装运行 SDK、未安装 Qt 源码时，这个目录可能不存在，需要显式提供完整许可文本。
- `-CrtDir` 可指定 `Microsoft.VC*.CRT` 的 x64 目录；未指定时，脚本从 `CMAKE_GENERATOR_INSTANCE` 指向的 VS 安装下选取可用的最新版本。
- `-Configuration` 只接受 `Release`；`-Version` 默认 `1.1.0`，只控制包名。

许可部署还有一处已知缺口：监控参考代码的 Apache-2.0 完整文本保存在
`apps/monitor/REFERENCE-LICENSE.txt`，但当前安装/打包脚本尚未自动复制该文件。
分发监控软件时仍须携带这份文本及所需归属说明，不能把脚本 smoke 成功当成
已经完成全部许可合规检查；见[第三方说明](THIRD-PARTY-NOTICES.md)。

本地 `dist/` 当前保留已发布 1.1.0 的两个 ZIP 和校验文件；构建与缓存目录可以重建，不应作为文档的隐含前置条件。

## SDK 源码入口

SDK 已统一到 `sdk/`。外部项目优先使用 `add_subdirectory(third_party/PlayerSDK)`，指向复制或解压后的 SDK 根目录；直接引用本仓库时使用 `add_subdirectory(path/to/myplayer/sdk)`。公开头文件 `play_core/...`、`player_sdk_qt/...` 以及目标 `PlayerSDK::Core`、`PlayerSDK::QtWidgets` 保持不变。

SDK 可直接独立配置：`cmake -S sdk -B build/sdk`（仍需提供依赖工具链）。源码发行仍从仓库根目录运行 `scripts/package-sdk.cmake`；`build/` 和 `dist/` 是生成目录，不是 SDK 源码入口。

## 目录与维护入口

```text
apps/qt_player/    播放器 UI、应用播放策略、设置、播放基准
apps/monitor/      监控 UI、设备配置、录像与回放
sdk/core/include/ 公开 C++ SDK API（Player、只读帧、订阅等）
sdk/core/src/      私有解封装/解码、队列、时钟、网络输入与旧回归入口
sdk/qt/            PlayerSDK::QtWidgets，QtPlayer 与 VideoWidget
sdk/examples/      无 Qt 及 Qt Widgets SDK consumer
sdk/cmake/         SDK 依赖/接入与告警规则
cmake/             应用安装规则
scripts/           双软件打包、媒体兼容性和性能验证脚本
sdk/tests/         SDK 公开接口、外部接入测试及共享媒体夹具
tests/             播放核心及播放器私有回归/集成测试
docs/              开发指南、历史检查记录与设计文件
sdk/third_party/   随 SDK 提供的 sigslot 源码
lib/               监控 standalone 可选 Windows 依赖备份
dist/              本地发行产物（不作为源码依赖）
```

`docs/design/*.pen` 是设计稿，不是运行时代码或由 CMake 自动生成的界面。界面实际实现以 C++、Qt `.ui`、资源文件为准。

## 许可证与归属

本项目继承原项目的 GPL 许可。分发二进制时需保留 `LICENSE`、`NOTICE`、`THIRD-PARTY-NOTICES.md`、各依赖的完整许可文本，并按适用许可提供对应源码/源码获取方式；不能仅凭一个上游首页链接就认定满足全部分发义务。

原项目：[itisyang/playerdemo](https://github.com/itisyang/playerdemo)，作者 [itisyang](https://github.com/itisyang)，主页 <https://itisyang.github.io/playerdemo/>。
