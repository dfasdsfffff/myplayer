# playerdemo

[![GitHub issues](https://img.shields.io/github/issues/itisyang/playerdemo.svg)](https://github.com/itisyang/playerdemo/issues)
[![GitHub stars](https://img.shields.io/github/stars/itisyang/playerdemo.svg)](https://github.com/itisyang/playerdemo/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/itisyang/playerdemo.svg)](https://github.com/itisyang/playerdemo/network)
[![GitHub release](https://img.shields.io/github/release/itisyang/playerdemo.svg)](https://github.com/itisyang/playerdemo/releases)
![language](https://img.shields.io/badge/language-c++-DeepPink.svg)
[![GitHub license](https://img.shields.io/github/license/itisyang/playerdemo.svg)](https://github.com/itisyang/playerdemo/blob/master/LICENSE)

一个功能丰富的跨平台视频播放器，开源版 PotPlayer。用于学习和交流音视频技术。

> **Note:** 本项目使用 CMake 构建系统，所有依赖库已预置在 `lib/` 目录中，无需手动下载。

---

## 功能特性

- **多格式支持** - 基于 FFmpeg 61 解码，支持几乎所有音视频格式
- **高清渲染** - SDL2 2.32.10 硬件加速渲染，支持多种像素格式
- **音频处理** - SoundTouch 2.3.3 提供无变调变速、音量控制
- **完整控制** - 播放/暂停/停止、快进/快退、循环模式（无/单曲/列表/随机）
- **播放列表** - 拖拽添加、双击播放、上一曲/下一曲、随机播放
- **快捷键支持** - 全屏、拖拽文件、鼠标滚轮控制音量/进度
- **设置持久化** - 窗口位置、音量、播放速度等配置自动保存
- **自定义主题** - 基于 QSS/CSS 的样式系统，支持自定义外观
- **跨平台设计** - Windows / Linux / macOS（主要测试平台为 Windows）

---

## 架构设计

```
playerdemo/
├── src/              # Qt UI 层 (Qt6 Widgets)
├── play_core/        # 核心播放引擎 (Qt 无关，纯 C++)
├── lib/              # 第三方库 (FFmpeg, SDL2, SoundTouch, sigslot)
├── build/            # CMake 构建目录
└── bin/              # 可执行文件输出
```

### 核心模块

**UI 层 (`src/`)**
- `MainWid` - 主窗口，管理布局和事件分发
- `Show` - 视频显示区域，处理拖放和全屏
- `CtrlBar` - 控制栏（播放、进度条、音量、速度）
- `Playlist` - 播放列表管理
- `Title` - 自定义标题栏
- `About` - 关于对话框
- `SettingWid` - 设置页面
- `VideoCtlBridge` - 将核心层信号转换为 Qt 信号
- `GlobalHelper` - 全局辅助函数（样式、配置持久化）
- `CustomSlider` - 自定义滑块控件
- `MediaList` - 媒体列表管理

**核心引擎 (`play_core/`)**
- `VideoCtl` - 单例控制器，管理播放生命周期和解码线程
- `VideoState` - 全局播放状态（队列、时钟、解码器、纹理）
- `Decoder` - 通用解码器（音/视/字幕），支持线程解码
- `FrameQueue` / `PacketQueue` - 线程安全队列
- `Clock` - 音视频同步时钟
- `XLogger` - 日志系统
- 使用 `sigslot` 实现线程安全的事件通知

### 技术栈

| 组件 | 版本 | 用途 |
|------|------|------|
| **FFmpeg** | 61 (libavcodec-61.dll) | 解封装、解码、滤镜、格式转换 |
| **SDL2** | 2.32.10 | 窗口、视频渲染、音频输出 |
| **SoundTouch** | 2.3.3 | 音频时间伸缩/音调变换 |
| **sigslot** | 1.2.3 | 线程安全信号槽（替代 Qt 信号跨线程通信）|
| **Qt** | 6.9.3 | GUI 界面 (Core, Gui, Widgets 模块) |

---

## 系统要求

- **CMake** 3.20+
- **C++ 编译器** 支持 C++20 标准 (MSVC 2022 / GCC 11+ / Clang 14+)
- **Qt6** 6.9.3 或更高版本 (Core, Gui, Widgets 模块)
- **操作系统**: Windows 10/11, Linux, macOS

---

## 编译指南

### Windows (推荐使用 Qt Creator 或 Visual Studio 2022)

1. **安装 Qt6** (通过 Qt Online Installer)
   - 下载地址: https://www.qt.io/download-qt-installer
   - 安装 Qt 6.9.3 或更高版本（MSVC 2022 64-bit）
   - 或使用 vcpkg: `vcpkg install qt6-base`

2. **配置 CMake**
   - **方式一：使用 Qt Creator**
     - 打开 Qt Creator，选择 "打开项目"
     - 选择项目根目录的 `CMakeLists.txt`
     - CMake 会自动配置 Qt6 路径和预编译库

   - **方式二：命令行构建**
   ```powershell
   # 创建构建目录
   mkdir build
   cd build

   # 配置项目（根据实际 Qt6 安装路径调整）
   cmake .. -DCMAKE_PREFIX_PATH="C:/Qt/6.9.3/msvc2022_64"

   # 编译
   cmake --build . --config Release

   # 或使用 MSVC 直接打开
   cmake .. -G "Visual Studio 17 2022"
   # 然后打开生成的 playerdemo.sln
   ```

3. **运行**
   - 可执行文件生成在 `bin/` 目录
   - Release 版本: `bin/playerdemo.exe`
   - Debug 版本: `bin/playerdemo_debug.exe`
   - SoundTouch DLL 会自动拷贝到输出目录

### Linux (Ubuntu/Debian)

```bash
# 安装依赖
sudo apt-get update
sudo apt-get install build-essential cmake qt6-base-dev qt6-tools-dev

# 注意：lib/ 目录中已有预编译的 SDL2 库，也可以选择使用系统版本
# sudo apt-get install libsdl2-dev

# 构建
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="/usr/lib/x86_64-linux-gnu/qt6"
cmake --build . --config Release

# 运行
./bin/playerdemo
```

### macOS

```bash
# 使用 Homebrew 安装 CMake 和 Qt6
brew install cmake qt@6

# 设置 Qt6 路径
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"

# 构建
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"
cmake --build . --config Release

# 运行
./bin/playerdemo
```

---

## 使用说明

1. **打开文件** - 拖拽视频文件到窗口，或通过菜单/播放列表添加
2. **播放控制** - 底部控制栏：播放/暂停、停止、进度条、音量
3. **快捷键**：
   - `Space` - 播放/暂停
   - `F3` - 打开文件
   - `F` / `Enter` - 全屏切换
   - `Ctrl+Enter` - 强制全屏
   - `F1` - 关于
   - `F4` - 关闭
   - `F5` - 设置
   - `F6` - 播放列表
   - `鼠标滚轮` - 音量/进度调节
   - `Ctrl+O` - 打开文件
   - `Alt+F4` - 退出
4. **播放列表** - 支持拖拽添加文件，双击播放
5. **循环模式** - 支持不循环、单曲循环、列表循环、随机播放

---

## 项目结构

```
playerdemo/
├── CMakeLists.txt              # 主构建配置
├── README.md                   # 项目说明
├── LICENSE                     # MIT 许可证
├── .gitignore
├── src/                        # Qt 界面层
│   ├── main.cpp                # 程序入口
│   ├── mainwid.h/.cpp/.ui      # 主窗口
│   ├── show.h/.cpp/.ui         # 视频显示控件
│   ├── ctrlbar.h/.cpp/.ui      # 控制栏
│   ├── playlist.h/.cpp/.ui     # 播放列表
│   ├── settingwid.h/.cpp/.ui   # 设置页面
│   ├── title.h/.cpp/.ui        # 自定义标题栏
│   ├── about.h/.cpp/.ui        # 关于对话框
│   ├── videoctl_bridge.h/.cpp  # 核心桥接层
│   ├── globalhelper.h/.cpp     # 全局辅助函数
│   ├── medialist.h/.cpp        # 媒体列表管理
│   ├── CustomSlider.h/.cpp     # 自定义滑块
│   ├── mainwid.qrc             # Qt 资源文件
│   ├── playerdemo.rc           # Windows 资源文件
│   └── res/                    # 资源文件目录
│       ├── *.svg               # 图标资源
│       ├── *.ttf               # 字体文件
│       ├── *.ico               # 应用图标
│       ├── menu.json           # 菜单配置
│       └── qss/                # 样式表文件
│           ├── design-system.css
│           ├── ctrlbar.css
│           ├── playlist.css
│           └── ...
├── play_core/                  # 核心引擎 (无 Qt 依赖)
│   ├── CMakeLists.txt          # 核心模块构建配置
│   ├── datactl.h               # 统一导出头文件
│   ├── videoctl.h/.cpp         # 播放控制器
│   ├── video_state.h           # 播放状态结构
│   ├── decoder.h/.cpp          # 解码器
│   ├── frame_queue.h/.cpp      # 帧队列
│   ├── packet_queue.h/.cpp     # 包队列
│   ├── clock.h/.cpp            # 时钟同步
│   ├── av_types.h              # FFmpeg 类型定义
│   ├── av_constants.h          # 常量定义
│   ├── av_compat.h             # FFmpeg 兼容层
│   ├── enums.h                 # 枚举定义
│   ├── signal.h                # 信号类型别名 (sigslot)
│   ├── soundtouch_wrap.h/.cpp  # SoundTouch 封装
│   └── xlogger.h/.cpp          # 日志系统
├── lib/                        # 第三方库 (预编译)
│   ├── ffmpeg/                 # FFmpeg 61 headers + libs
│   │   ├── include/            # 头文件
│   │   ├── bin/                # DLL 文件
│   │   └── lib/                # 导入库
│   ├── SDL2/                   # SDL2 2.32.10
│   │   ├── include/            # 头文件
│   │   ├── lib/x64/            # 库文件
│   │   └── cmake/              # CMake 配置
│   ├── soundtouch-2.3.3/       # SoundTouch
│   │   └── lib/                # DLL 和静态库
│   └── sigslot-1.2.3/          # sigslot signal/slot 库
│       └── include/            # 头文件
├── build/                      # CMake 生成文件（不提交到 Git）
└── bin/                        # 可执行文件和依赖 DLL
    ├── playerdemo.exe          # Release 可执行文件
    ├── playerdemo_debug.exe    # Debug 可执行文件
    ├── *.dll                   # 运行时依赖库
    └── platforms/              # Qt 平台插件
```

---

## 开发说明

### 设计模式

- **单例模式** - `VideoCtl` 全局唯一实例，提供 `GetInstance()` 和 `MakeInstance()`
- **工厂模式** - `VideoCtl::MakeInstance()` 创建独立实例
- **桥接模式** - `VideoCtlBridge` 隔离核心引擎与 UI 层
- **观察者模式** - `sigslot::signal` 实现事件订阅和通知
- **RAII** - 资源自动管理（队列、解码器上下文）

### 线程模型

项目采用多线程架构：
- **读取线程** (`ReadThread`) - 负责解封装和读取数据包
- **音频解码线程** (`audio_thread`) - 解码音频帧
- **视频解码线程** (`video_thread`) - 解码视频帧
- **字幕解码线程** (`subtitle_thread`) - 解码字幕
- **播放/刷新线程** (`LoopThread`) - 音视频同步和渲染
- **Qt 主线程** - UI 事件处理

### 信号系统

使用 `sigslot` 库实现线程安全的信号/槽机制：
- `Signal<const std::string&> SigPlayMsg` - 播放消息
- `Signal<int, int> SigFrameDimensionsChanged` - 视频尺寸变化
- `Signal<int> SigVideoTotalSeconds` - 视频总时长
- `Signal<int> SigVideoPlaySeconds` - 播放进度
- `Signal<double> SigVideoVolume` - 音量变化
- `Signal<bool> SigPauseStat` - 暂停状态
- `Signal<> SigStop` - 停止播放
- `Signal<> SigPlayNextOne` - 播放下一首
- `Signal<> SigRandomPlayOne` - 随机播放

### 日志系统

使用 `XLogger` 类，支持多级别日志：
- Debug
- Info
- Warn
- Error
- Fatal

日志输出到文件和控制台。

### 样式系统

项目使用基于 CSS 的 QSS 样式表：
- `design-system.css` - 全局设计系统样式
- `ctrlbar.css` - 控制栏样式
- `playlist.css` - 播放列表样式
- `title.css` - 标题栏样式
- 等等

---

## 常见问题

**Q: 编译时找不到 Qt 库？**
A: 设置 `CMAKE_PREFIX_PATH` 指向 Qt6 的安装路径，例如：
```bash
cmake .. -DCMAKE_PREFIX_PATH="C:/Qt/6.9.3/msvc2022_64"
```

**Q: 运行时缺少 DLL？**
A: Windows 下确保 `bin/` 目录包含所有必需的 DLL。CMake 已配置自动拷贝 SoundTouch DLL，其他库的 DLL 也应放置在 `bin/` 目录。

**Q: 如何更换 FFmpeg/SDL2 版本？**
A: 替换 `lib/` 目录下的头文件和库文件，然后重新构建项目。

**Q: 支持硬件加速吗？**
A: 支持。SDL2 会自动检测并使用硬件加速：
- Windows: D3D11VA
- Linux: VAAPI
- macOS: VideoToolbox

**Q: 如何自定义样式？**
A: 修改 `src/res/qss/` 目录下的 CSS 文件，或通过 `GlobalHelper::GetThemeStr()` 加载自定义样式。

**Q: 编译时出现 C4819 警告？**
A: CMake 已配置 `/utf-8` 编译选项以解决此问题。如仍有问题，确保源文件保存为 UTF-8 编码。

---

## 贡献

欢迎提交 Issue 和 Pull Request！

- 代码风格：遵循 Qt/C++ 规范
- 提交前请确保编译通过
- 新增功能建议先讨论

---

## 许可证

MIT License - 详见 [LICENSE](LICENSE) 文件

---

## 链接

- 项目主页: https://github.com/itisyang/playerdemo
- 作者博客: https://itisyang.github.io/playerdemo/
- 问题反馈: https://github.com/itisyang/playerdemo/issues
