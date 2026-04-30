# playerdemo

[![GitHub issues](https://img.shields.io/github/issues/itisyang/playerdemo.svg)](https://github.com/itisyang/playerdemo/issues)
[![GitHub stars](https://img.shields.io/github/stars/itisyang/playerdemo.svg)](https://github.com/itisyang/playerdemo/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/itisyang/playerdemo.svg)](https://github.com/itisyang/playerdemo/network)
[![GitHub release](https://img.shields.io/github/release/itisyang/playerdemo.svg)](https://github.com/itisyang/playerdemo/releases)
![Build Status](https://github.com/itisyang/playerdemo/actions/workflows/windows.yml/badge.svg)
![Build Status](https://github.com/itisyang/playerdemo/actions/workflows/macos.yml/badge.svg)
![Build Status](https://github.com/itisyang/playerdemo/actions/workflows/ubuntu.yml/badge.svg)
![language](https://img.shields.io/badge/language-c++-DeepPink.svg)
[![GitHub license](https://img.shields.io/github/license/itisyang/playerdemo.svg)](https://github.com/itisyang/playerdemo/blob/master/LICENSE)

一个功能丰富的跨平台视频播放器，开源版 PotPlayer。用于学习和交流音视频技术。

> **Note:** 本项目迁移到了 CMake 构建系统（原 .pro 文件已废弃）。所有依赖库已预置在 `lib/` 目录中，无需手动下载。

---

## 功能特性

- **多格式支持** - 基于 FFmpeg 61.x 解码，支持几乎所有音视频格式
- **高清渲染** - SDL2 2.32.x 硬件加速渲染，支持多种像素格式
- **音频处理** - SoundTouch 2.3.3 提供无变调变速、音量控制
- **完整控制** - 播放/暂停/停止、快进/快退、循环模式（无/单曲/列表/随机）
- **播放列表** - 拖拽添加、双击播放、上一曲/下一曲、随机播放
- **快捷键支持** - 全屏、拖拽文件、鼠标滚轮控制音量/进度
- **设置持久化** - 窗口位置、音量、播放速度等配置自动保存
- **跨平台** - Windows / Linux / macOS

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
- `VideoCtlBridge` - 将核心层信号转换为 Qt 信号

**核心引擎 (`play_core/`)**
- `VideoCtl` - 单例控制器，管理播放生命周期和解码线程
- `VideoState` - 全局播放状态（队列、时钟、解码器、纹理）
- `Decoder` - 通用解码器（音/视/字幕），支持线程解码
- `FrameQueue` / `PacketQueue` - 线程安全队列
- `Clock` - 音视频同步时钟
- 使用 `sigslot` 实现线程安全的事件通知

### 技术栈

| 组件 | 版本 | 用途 |
|------|------|------|
| **FFmpeg** | 61.x | 解封装、解码、滤镜、格式转换 |
| **SDL2** | 2.32.x | 窗口、视频渲染、音频输出 |
| **SoundTouch** | 2.3.3 | 音频时间伸缩/音调变换 |
| **sigslot** | 1.2.3 | 线程安全信号槽（替代 Qt 信号跨线程通信）|
| **Qt** | 6.x (Qt6) | GUI 界面 |

---

## 系统要求

- **CMake** 3.20+
- **C++ 编译器** 支持 C++20 标准 (MSVC / GCC 11+ / Clang 14+)
- **Qt6** (Core, Gui, Widgets 模块)

---

## 编译指南

### Windows (推荐使用 Qt Creator 或 Visual Studio)

1. **安装 Qt6** (通过 Qt Online Installer 或 vcpkg)
   ```bash
   # 或者使用 vcpkg
   vcpkg install qt6-base
   ```

2. **配置 CMake**
   - 打开 Qt Creator，克隆/打开项目
   - CMake 会自动找到 `lib/` 下的预编译库
   - 或使用命令行：
   ```powershell
   mkdir build && cd build
   cmake .. -DCMAKE_PREFIX_PATH="C:/Qt/6.9.3/msvc2019_64"
   cmake --build . --config Release
   ```

3. **运行**
   - 可执行文件生成在 `bin/` 目录
   - 确保 SoundTouch 的 DLL 在可执行文件同目录（Windows 自动拷贝）

### Linux (Ubuntu/Debian)

```bash
# 安装依赖
sudo apt-get update
sudo apt-get install build-essential cmake libsdl2-dev qt6-base-dev

# 构建
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="/usr/lib/x86_64-linux-gnu/qt6"
cmake --build . --config Release

# 运行
./bin/playerdemo
```

### macOS

```bash
# 使用 Homebrew 安装依赖（可选，lib/已有预编译库）
brew install cmake sdl2 qt6

# 构建
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix qt6)"
cmake --build . --config Release

# 运行
./bin/playerdemo.app/Contents/MacOS/playerdemo
```

---

## 使用说明

1. **打开文件** - 拖拽视频文件到窗口，或通过菜单/播放列表添加
2. **播放控制** - 底部控制栏：播放/暂停、停止、进度条、音量
3. **快捷键**：
   - `Space` - 播放/暂停
   - `F` - 全屏切换
   - `Ctrl+O` - 打开文件
   - `Ctrl+Q` - 退出
   - 鼠标滚轮 - 音量/进度调节
4. **设置** - 右上角菜单可调整播放速度、循环模式、清晰度等

---

## 项目结构

```
playerdemo-master/
├── CMakeLists.txt          # 主构建配置
├── README.md               # 项目说明
├── LICENSE                 # MIT 许可证
├── .gitignore
├── src/                    # Qt 界面层
│   ├── CMakeLists.txt
│   ├── main.cpp            # 程序入口
│   ├── mainwid.h/.cpp      # 主窗口
│   ├── show.h/.cpp         # 视频显示控件
│   ├── ctrlbar.h/.cpp      # 控制栏
│   ├── playlist.h/.cpp     # 播放列表
│   ├── settingwid.h/.cpp   # 设置页面
│   ├── title.h/.cpp        # 标题栏
│   ├── about.h/.cpp        # 关于对话框
│   └── videoctl_bridge.h/.cpp  # 核心桥接
├── play_core/              # 核心引擎 (无 Qt 依赖)
│   ├── CMakeLists.txt
│   ├── datactl.h           # 统一导出头文件
│   ├── videoctl.h/.cpp     # 播放控制器
│   ├── video_state.h       # 播放状态结构
│   ├── decoder.h/.cpp      # 解码器
│   ├── frame_queue.h       # 帧队列
│   ├── packet_queue.h      # 包队列
│   ├── clock.h/.cpp        # 时钟同步
│   ├── av_types.h          # FFmpeg 类型定义
│   ├── av_constants.h      # 常量定义
│   └── ...                 # 其他辅助类
├── lib/                    # 第三方库 (预编译)
│   ├── ffmpeg/             # FFmpeg 61.x headers + libs
│   ├── SDL2/               # SDL2 2.32.x headers + libs
│   ├── soundtouch-2.3.3/   # SoundTouch 源码
│   └── sigslot-1.2.3/      # sigslot 源码
├── build/                  # CMake 生成文件
└── bin/                    # 可执行文件
```

---

## 开发说明

### 设计模式

- **单例** - `VideoCtl` 全局唯一实例
- **工厂** - `VideoCtl::MakeInstance()` 创建实例
- **桥接** - `VideoCtlBridge` 隔离核心引擎与 UI
- **观察者** - `sigslot` 实现事件订阅
- **RAII** - 资源自动管理（队列、解码器上下文）

### 线程模型

- 解封装/读取线程
- 音频解码线程
- 视频解码线程
- 字幕解码线程
- 播放/刷新线程
- Qt 主线程 (UI)

### 日志系统

使用 `XLogger` 类，支持多级别（Debug/Info/Warn/Error/Fatal），输出到文件和控制台。

---

## 常见问题

**Q: 编译时找不到 Qt 库？**  
A: 设置 `CMAKE_PREFIX_PATH` 指向 Qt6 的安装路径。

**Q: 运行时缺少 DLL？**  
A: Windows 下确保 `bin/` 目录包含 SoundTouch、FFmpeg、SDL2 的 DLL。CMake 已配置自动拷贝。

**Q: 如何更换 FFmpeg/SDL2 版本？**  
A: 替换 `lib/` 目录下的头文件和库文件，重新构建即可。

**Q: 支持硬件加速吗？**  
A: 支持。SDL2 会自动检测并使用 D3D11VA (Windows)、VAAPI (Linux)、VideoToolbox (macOS)。

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
