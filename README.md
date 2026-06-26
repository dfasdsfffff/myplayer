# playerdemo

一个基于 Qt、FFmpeg、SDL2 和 SoundTouch 的跨平台本地播放器示例项目。

本仓库基于原项目 [itisyang/playerdemo](https://github.com/itisyang/playerdemo) 二次整理和改造，保留原作者信息并继续遵循项目许可证。

## 原作者与来源

- 原项目：[`itisyang/playerdemo`](https://github.com/itisyang/playerdemo)
- 原作者：[`itisyang`](https://github.com/itisyang)
- 原项目主页：<https://itisyang.github.io/playerdemo/>

> 本项目是在原作者开源代码基础上继续维护和调整的版本。感谢原作者 `itisyang` 对播放器示例、界面和播放核心实现的贡献。

## 项目特点

- 基于 FFmpeg 的音视频解封装与解码。
- 基于 SDL2 的音频输出和视频渲染支持。
- 基于 SoundTouch 的音频变速、变调处理能力。
- Qt Widgets 图形界面，包含播放控制栏、播放列表、设置窗口、自定义标题栏等模块。
- 核心播放逻辑独立在 `play_core/`，便于 UI 层复用。
- 支持 Qt 版播放器和 Dear ImGui 版播放器两个应用入口。
- 使用 CMake 构建，第三方库默认从仓库 `lib/` 目录读取。
- Windows 下构建后会自动复制 FFmpeg、SDL2、SoundTouch 运行时 DLL 到输出目录。

## 技术栈

| 组件 | 用途 |
| --- | --- |
| C++20 | 主体语言标准 |
| CMake 3.20+ | 构建系统 |
| Qt 6 | Qt Widgets 桌面界面 |
| FFmpeg | 音视频解封装、解码、格式处理 |
| SDL2 | 音频输出、视频渲染和底层平台能力 |
| SoundTouch | 音频时间伸缩和音调处理 |
| sigslot | 线程间信号通知 |
| Dear ImGui | ImGui 版播放器界面 |
| vcpkg | ImGui 等包管理 |

## 目录结构

```text
playerdemo/
├── apps/
│   ├── qt_player/          # Qt Widgets 播放器界面
│   └── imgui_player/       # Dear ImGui 播放器入口
├── play_core/              # 播放核心，尽量与 UI 解耦
├── tests/                  # 测试代码
├── lib/                    # 随仓库提供的第三方依赖
│   ├── ffmpeg/
│   ├── SDL2/
│   ├── soundtouch-2.3.3/
│   └── sigslot-1.2.3/
├── bin/                    # 默认运行时输出目录
├── CMakeLists.txt          # 顶层 CMake 配置
├── CMakePresets.json       # Windows + MSVC + vcpkg 预设
├── vcpkg.json              # vcpkg 依赖清单
└── LICENSE
```

## 核心模块

### Qt 界面层

主要代码位于 `apps/qt_player/`：

- `main.cpp`：Qt 应用入口。
- `MainWid`：主窗口和整体布局。
- `Show`：视频显示区域，处理显示、拖放和全屏相关交互。
- `CtrlBar`：播放控制栏，包含播放、暂停、停止、进度、音量和速度控制。
- `Playlist` / `MediaList` / `PlaylistFile`：播放列表和列表文件管理。
- `Title`：自定义标题栏。
- `SettingWid`：设置界面。
- `About`：关于窗口。
- `VideoCtlBridge`：将播放核心事件桥接为 Qt 信号。
- `GlobalHelper`：样式、配置和工具函数。
- `CustomSlider`：自定义滑块控件。

### 播放核心

主要代码位于 `play_core/`：

- `VideoCtl`：播放控制入口，管理播放生命周期。
- `VideoState`：播放状态、队列、时钟、解码上下文等共享状态。
- `Decoder`：音频、视频、字幕解码封装。
- `PacketQueue` / `FrameQueue`：线程安全的数据队列。
- `Clock`：音视频同步时钟。
- `SoundTouchWrap`：SoundTouch 封装。
- `XLogger`：日志输出。
- `signal.h`：基于 sigslot 的事件通知类型。

## 构建要求

- CMake 3.20 或更高版本。
- 支持 C++20 的编译器。
  - Windows 推荐 MSVC 2022。
  - Linux 推荐 GCC 11+ 或 Clang 14+。
  - macOS 推荐 Apple Clang。
- Qt 6，至少包含 `Core`、`Gui`、`Widgets` 模块。
- vcpkg，用于安装 `imgui` 依赖。
- Windows 默认使用仓库 `lib/` 目录内的 FFmpeg、SDL2、SoundTouch、sigslot。

## Windows 构建

推荐使用 Visual Studio 2022、Qt 6 和 vcpkg。

1. 安装 Qt 6，并确认 Qt 安装目录。例如：

```text
C:/Qt/6.9.3/msvc2022_64
```

2. 安装并配置 vcpkg，确保环境变量 `VCPKG_ROOT` 指向 vcpkg 根目录。

3. 配置项目：

```powershell
cmake --preset windows-msvc-vcpkg
```

如果 Qt 不在默认路径，可以手动指定：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DPLAYERDEMO_QT_ROOT="C:/Qt/6.9.3/msvc2022_64"
```

4. 编译 Debug 或 Release：

```powershell
cmake --build --preset windows-msvc-vcpkg-debug
cmake --build --preset windows-msvc-vcpkg-release
```

5. 运行：

```powershell
.\bin\playerdemo_debug.exe
.\bin\playerdemo.exe
```

ImGui 版输出为：

```powershell
.\bin\playerdemo_imgui_debug.exe
.\bin\playerdemo_imgui.exe
```

## Linux / macOS 构建说明

项目 CMake 已尽量保持跨平台结构，但当前随仓库提供的预编译依赖主要面向 Windows。Linux 和 macOS 构建时通常需要安装或替换对应平台的 FFmpeg、SDL2、SoundTouch、Qt6 和 ImGui 依赖，并通过 CMake cache 变量指定路径。

可参考以下变量：

```text
PLAYERDEMO_DEPS_ROOT
PLAYERDEMO_FFMPEG_ROOT
PLAYERDEMO_SDL2_ROOT
PLAYERDEMO_SOUNDTOUCH_ROOT
PLAYERDEMO_SIGSLOT_ROOT
PLAYERDEMO_QT_ROOT
```

示例：

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/qt6" \
  -DPLAYERDEMO_FFMPEG_ROOT="/path/to/ffmpeg" \
  -DPLAYERDEMO_SDL2_ROOT="/path/to/SDL2" \
  -DPLAYERDEMO_SOUNDTOUCH_ROOT="/path/to/soundtouch" \
  -DPLAYERDEMO_SIGSLOT_ROOT="/path/to/sigslot"

cmake --build build --config Release
```

## 测试

项目包含 `playlistfile_tests` 测试目标。配置完成后可运行：

```powershell
ctest --preset windows-msvc-vcpkg-debug
```

或直接在构建目录中执行：

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

## 使用说明

- 可通过界面打开媒体文件，也可以拖放媒体文件到播放器窗口。
- 底部控制栏提供播放、暂停、停止、进度、音量、速度等常用操作。
- 播放列表支持添加文件、双击播放、上一首、下一首等操作。
- 支持无循环、单曲循环、列表循环、随机播放等播放模式。
- 样式文件位于 `apps/qt_player/res/qss/`，可按需调整界面外观。

## 常见问题

### CMake 找不到 Qt6

确认 `PLAYERDEMO_QT_ROOT` 或 `CMAKE_PREFIX_PATH` 指向正确的 Qt 安装目录：

```powershell
cmake -S . -B build -DPLAYERDEMO_QT_ROOT="C:/Qt/6.9.3/msvc2022_64"
```

### CMake 找不到 imgui

确认已配置 vcpkg toolchain，并且 `VCPKG_ROOT` 环境变量有效。项目的 `vcpkg.json` 会声明 `imgui` 依赖。

### 运行时缺少 DLL

Windows 下正常构建后，CMake 会把 `lib/ffmpeg/bin/*.dll`、`lib/SDL2/lib/x64/SDL2.dll` 和 `lib/soundtouch-2.3.3/lib/SoundTouchDLL_x64.dll` 复制到 `bin/`。如果仍缺少 DLL，请检查构建日志和对应依赖目录是否完整。

### 中文或资源显示异常

项目已在 MSVC 下启用 `/utf-8` 编译选项。请确认源文件、资源文件和 README 均以 UTF-8 保存。

## 开发说明

- 播放核心尽量放在 `play_core/`，UI 层通过桥接类接入核心事件。
- Qt 相关逻辑放在 `apps/qt_player/`。
- ImGui 版入口放在 `apps/imgui_player/`。
- 新增资源时需要同步检查 `apps/qt_player/mainwid.qrc`。
- 新增构建依赖时优先更新 `CMakeLists.txt` 和 `vcpkg.json`。

## 许可证

本项目遵循 GPL License，详见 [LICENSE](LICENSE)。

本仓库基于 [itisyang/playerdemo](https://github.com/itisyang/playerdemo) 开源项目整理和修改，原作者为 [`itisyang`](https://github.com/itisyang)。
