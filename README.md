# MyPlayer 与 MyPlayer Monitor

本文档面向 **v2.1.0 发布准备**；目标 Git 标签为 `v2.1.0`，打包版本参数为 `2.1.0`。发行说明见 [v2.1.0](docs/releases/v2.1.0.md)。文档中的目标附件名不代表附件已经上传。

本仓库包含两个 Qt 桌面软件，共用 C++20 播放核心 `play_core`：

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

v2.1.0 计划按以下名称打包。实际可下载的平台与附件以 [GitHub 发行页](https://github.com/dfasdsfffff/myplayer/releases) 为准：

| 文件 | 完整解压后启动 |
| --- | --- |
| `MyPlayer-2.1.0-windows-x64.zip` | `bin/myplayer.exe` |
| `MyPlayerMonitor-2.1.0-windows-x64.zip` | `bin/myplayer_monitor.exe` |
| `MyPlayer-2.1.0-linux-x86_64.tar.gz` | 解压目录的 `bin/myplayer` 或 `bin/myplayer_monitor`（双应用合包） |

各归档均由脚本生成对应 `.sha256` 文件。Windows 可用 `Get-FileHash <归档路径> -Algorithm SHA256` 比对；Linux 可在归档目录运行 `sha256sum -c <归档文件名>.sha256`。完整解压并保留目录结构，不要只复制可执行文件。Windows 包包含 Qt 插件、媒体依赖、app-local MSVC 运行库和许可证，无需另装 Qt 或 VC++ 运行库；Linux 包仍依赖目标系统的 glibc、图形栈和音频服务。录像等写入操作需要目录写权限。

“免安装”是部署方式，不代表所有设置都保存在程序旁边。播放器设置与监控配置的存储方式不同，见开发指南及监控 README。

发行包版本与程序内版本目前尚未统一：本次目标标签为 `v2.1.0`、包名版本为 `2.1.0`；顶层/监控 CMake 项目、`vcpkg.json` 及两个 `main.cpp` 中的应用版本仍为 `1.0.0`。显式传入打包参数不会更新这些元数据，本轮仅更新文档。

### v2.1.0 主要变化

- 监控软件新增 ONVIF 局域网摄像头发现，可读取设备名称与主/子 RTSP 码流地址，仍支持手动配置。
- 扩展 Linux / macOS 源码构建支持，调整 SoundTouch 接入和平台相关测试，新增 macOS 双应用 CI 配置。
- 新增 Linux 双应用免安装打包脚本，收集依赖、Qt 插件与许可证，生成 SHA-256 校验文件。
- 非 Windows 视频控件改用 SDL 软件表面经 Qt 绘制，避免创建不兼容的原生子窗口；Linux 未指定音频驱动且音频初始化失败时可尝试静音输出。
- 监控程序加入 Windows 应用图标；播放器控制栏动画由主窗口持有，随窗口释放。

继续包含 2.0.1 的播放器修复：菜单关闭时 `Ctrl+O` 等快捷键可用、音频占位层不再覆盖视频、Windows 高 DPI 窗口边缘缩放坐标一致，Enter/空格避免重复切换。跨平台构建与 CI 配置不等于所有平台、摄像头和 GPU 已通过实际验收。

发行验收不能只运行 `--smoke-test`：还需实际通过 `Ctrl+O` 选择文件，确认视频画面可见、暂停/恢复可用，验证视频→音频→视频切换，以及高 DPI 下拖拽窗口边缘、暂停画面缩放和最大化/还原。启动成功或 SDL present 计数不代表最终画面可见。

## 从源码构建

以下 PowerShell 命令均从仓库根目录执行。`build/`、`bin/`、`.cache/` 都是生成目录，干净检出或清理后不存在是正常的；不要依赖历史构建缓存或其他机器的绝对路径。

### 环境与依赖

- CMake 项目及 Preset schema 最低要求 **3.25**，但下文默认的 [Visual Studio 18 2026 生成器](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html) 从 **CMake 4.2** 才提供；照用该生成器时需 **4.2+**。
- C++20 编译器。默认 Preset 使用 **Visual Studio 18 2026**、MSVC x64；需安装桌面 C++ 工作负载及 Windows SDK。
- Qt **6.8+**；播放器需 Core/Gui/Widgets，监控还需 Network/Xml。Windows 构建示例使用 Qt **6.9.3 / msvc2022_64**，这是 Qt SDK 目录名，不是本仓库 Preset 的 VS 生成器名称。
- vcpkg，设置 `VCPKG_ROOT` 为其根目录。`vcpkg.json` 声明 FFmpeg、libass、SDL2、SoundTouch；sigslot 使用仓库内的头文件。
- 播放器通过 `pkg_check_modules(LIBASS ... libass)` 查找 libass，需要可用的 pkg-config/pkgconf 及与当前 Release/Debug、x64 依赖相匹配的元数据。

顶层构建默认从配置好的依赖安装前缀查找 FFmpeg/SDL2/SoundTouch，**不会自动用 `lib/` 中的二进制替代缺失的 vcpkg 包**。仅监控独立构建有 `MONITOR_USE_BUNDLED_DEPS` 开关。

### Linux / macOS 源码构建

准备 CMake 3.25+、Ninja、支持 C++20 的 GCC 或 Clang、Qt 6.8+、pkg-config 和已初始化的 vcpkg。使用与 Qt 相同的 CPU 架构。WSL 下可将构建目录放在 Linux 文件系统中。

Linux x86/x64 构建 FFmpeg 还需要系统提供 NASM。Ubuntu/Debian 在首次 CMake 配置前执行（已有其他构建依赖时）：

```bash
sudo apt-get update
sudo apt-get install -y nasm
```

Linux sanitizer CI 同样在 vcpkg 配置前安装该工具；缺少 NASM 会使 FFmpeg 依赖构建失败，而非 sanitizer 检查失败。

macOS 在首次配置前安装 Qt、Ninja 与 vcpkg 依赖构建工具（`autoconf-archive` 为 gperf 等依赖的 Autotools 检查所需）：

```bash
brew install qtbase pkgconf ninja autoconf autoconf-archive automake libtool nasm
```

```bash
export VCPKG_ROOT="$HOME/tools/vcpkg"
export QT_ROOT="/path/to/Qt"
build_dir="$HOME/myplayer-build"

cmake -S . -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DPLAYERDEMO_QT_ROOT="$QT_ROOT" \
  -DPLAYERDEMO_OUTPUT_DIR="$build_dir/bin" \
  -DMYPLAYER_BUILD_QT_APP=ON \
  -DMYPLAYER_BUILD_MONITOR=ON \
  -DBUILD_TESTING=ON \
  -DMONITOR_BUILD_TESTS=ON
cmake --build "$build_dir" --parallel 4
QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy ctest --test-dir "$build_dir" --output-on-failure
```

使用 Homebrew 安装 `qtbase` 时，Qt 路径可设为 `export QT_ROOT="$(brew --prefix qtbase)"`。构建后的播放器与监控程序位于构建目录的 `bin/`。macOS 当前提供源码构建与 CI 配置，没有专用 `.app` / DMG 打包脚本。

macOS 在 manifest 中显式声明 vcpkg `fontconfig`，确保 libass 的可选字体后端及其 pkg-config 元数据依赖来自同一依赖树；不要仅向 Homebrew 安装 Fontconfig 或忽略 `libass` 的依赖检查。修改 manifest 后重新执行配置，让 vcpkg 安装新增依赖。当前基线的 libass port 只在 Linux 自动声明 Fontconfig，macOS 需由本项目补齐。

Linux 免安装打包需要动态依赖；在上述配置命令中额外传入 `-DVCPKG_OVERLAY_TRIPLETS="$PWD/cmake/triplets" -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic -DMYPLAYER_ENABLE_SANITIZERS=OFF`，并使用独立的构建目录。仓库提供的这个 triplet 仅用于 Linux x64，不应用于 macOS。

Linux 未显式设置 `SDL_AUDIODRIVER` 且音频子系统初始化失败时会尝试 dummy 驱动，此时无声音；显式指定驱动失败不会触发该回退。非 Windows 视频路径使用 SDL 软件渲染与 Qt 绘制，不应套用 Windows 原生 SDL 呈现路径的性能结论。

### Windows 构建两个软件

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

CI 配置覆盖 Windows 双应用、Linux 核心 sanitizer 测试和 macOS 双应用构建。参见 [Windows build](.github/workflows/windows-build.yml)、[Clang sanitizers](.github/workflows/clang-sanitizers.yml) 和 [macOS build](.github/workflows/macos-build.yml)。

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

基准模式请求软件解码、1 倍速、关闭循环/断点恢复及 1280×720 窗口。播放器报告使用 schema 3 / `myplayer_qt_sdk_playback`，保留 `video_frame_sdl_present_returns` 字段名；schema 2 仅用于之前的 QPainter 实现。该字段统计共享控件的新帧通知：Windows 原生路径在 SDL 呈现返回后通知，非 Windows 软件路径在准备 Qt 绘制时通知，并非屏幕/合成器完成计数。GUI 收到的帧数不是解码帧/丢帧计数，也不证明画面实际可见。CPU 以一个逻辑核心为 100%；纯视频样本不测音画同步，短时结果不是长期稳定性结论。

## 双软件免安装打包

### Linux x86_64

先按上面的 Linux 构建说明配置两个应用，使用 `Release` 并设置
`-DMYPLAYER_ENABLE_SANITIZERS=OFF`。构建目录、Qt SDK、vcpkg 动态库和
`patchelf` 必须位于 Linux 环境中。Qt 许可证目录应与所用 SDK 版本匹配。

```bash
export QT_LICENSE_DIR="/path/to/Qt/Src/LICENSES"
export PATCHELF="/path/to/patchelf"
bash scripts/package-portable-linux.sh "$build_dir" 2.1.0
```

`patchelf` 已在 `PATH` 中时可省略 `PATCHELF`。若构建环境缺少 Qt xcb 插件所需的 `libxcb-cursor.so.0`，可在一个临时目录下载并解出目标发行版匹配的 `libxcb-cursor0` 包：

```bash
mkdir -p "$HOME/myplayer-extra"
cd "$HOME/myplayer-extra"
apt download libxcb-cursor0
mkdir -p libxcb-cursor0/root
dpkg-deb -x libxcb-cursor0_*.deb libxcb-cursor0/root
```

回到仓库根目录，传入额外动态库与许可文本后打包：

```bash
EXTRA_LIBRARY_DIR="$HOME/myplayer-extra/libxcb-cursor0/root/usr/lib/x86_64-linux-gnu" \
EXTRA_LIBRARY_LICENSE="$HOME/myplayer-extra/libxcb-cursor0/root/usr/share/doc/libxcb-cursor0/copyright" \
bash scripts/package-portable-linux.sh "$build_dir" 2.1.0
```

两项额外参数须配套提供；脚本只收集配置的 Qt/vcpkg/额外目录中的依赖，不会打包所有系统库。目标系统仍需提供未收集的系统依赖。

Linux x86_64 下脚本生成 `dist/MyPlayer-2.1.0-linux-x86_64.tar.gz` 和对应 `.sha256`，内含两个
程序、Qt 插件、所需 Qt/vcpkg 动态库及许可证文本。解压后运行
`MyPlayer-2.1.0-linux-x86_64/bin/myplayer` 或同目录的 `myplayer_monitor`。
脚本会检查相对库路径并在离屏模式启动两个程序；正常播放、摄像头连接和不同
Linux 发行版上的兼容性仍需在目标系统验证。系统的 glibc、图形栈和音频服务
仍由目标系统提供。

### Windows x64

使用上文的 **Windows x64 / Visual Studio 多配置生成器** 同时启用两个应用；需要完整依赖安装目录、Qt 的 `windeployqt.exe`、Qt 许可证文本和可再分发的 MSVC CRT DLL。监控 EXE 从 `PLAYERDEMO_OUTPUT_DIR`（默认仓库 `bin/`）读取；脚本不承诺支持 Ninja 或仅监控 standalone 构建。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-portable.ps1 `
  -BuildDir build -Configuration Release -Version 2.1.0
```

显式指定 `-Version 2.1.0`，不要依赖脚本旧默认值。脚本拒绝覆盖同名归档或暂存目录；重打同一版本前先移走旧输出。

脚本会：构建两个应用；安装基础运行目录；分别部署 Qt 插件与 app-local MSVC CRT；复制依赖许可证；在移除开发搜索路径后运行各自的 smoke 模式；生成两个 ZIP 和 SHA-256 文件。它**不会**自动提交源码、创建 Git 标签或发布 GitHub/Gitee 发行页，也不会实际验证媒体播放/录像或干净虚拟机兼容性。

相关缓存项与可选参数：

- `PLAYERDEMO_QT_ROOT`、`VCPKG_INSTALLED_DIR`、`VCPKG_TARGET_TRIPLET` 必须对应本次构建的有效路径；后两项通常由 vcpkg toolchain 设置。
- `-QtLicenseDir` 默认使用 Qt SDK 前缀的同级 `Src/LICENSES`；只安装运行 SDK、未安装 Qt 源码时，这个目录可能不存在，需要显式提供完整许可文本。
- `-CrtDir` 可指定 `Microsoft.VC*.CRT` 的 x64 目录；未指定时，脚本从 `CMAKE_GENERATOR_INSTANCE` 指向的 VS 安装下选取可用的最新版本。
- `-Configuration` 只接受 `Release`；`-Version` 默认 `1.1.0`，只控制包名。

许可部署还有一处已知缺口：监控参考代码的 Apache-2.0 完整文本保存在
`apps/monitor/REFERENCE-LICENSE.txt`，Linux 打包脚本会复制为
`licenses/monitor-REFERENCE-LICENSE.txt`，Windows 安装/打包脚本尚未自动复制。
分发 Windows 监控软件前须将该文本放入暂存包的 `licenses/` 并重新归档及生成校验文件；
不能把脚本 smoke 成功当成全部许可合规检查，见[第三方说明](THIRD-PARTY-NOTICES.md)。

`dist/` 为本地生成目录，不保证含有指定版本；v2.1.0 发布前须按本节重新生成并验收附件。CI 的 Windows 打包步骤目前显式使用 `1.0.0`，不会根据 Git 标签自动生成 `2.1.0` 包。

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
