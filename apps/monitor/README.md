# MyPlayer 多路监控

`apps/monitor` 是复用 `play_core` 的独立 Qt Widgets 应用，目标名为 `myplayer_monitor`，与 `apps/qt_player` 中的单播放器分别运行。默认九宫格，可切换 **1 / 4 / 9 / 16 分屏**。

双软件发行包、下载和整仓部署说明见[根 README](../../README.md)。发行包版本为 **1.1.0**，但当前监控应用的 `--version` 和独立 CMake 项目版本仍为 **1.0.0**，两者尚未统一。下文的构建目录都是待新建的输出目录，不是仓库已提供的依赖或可直接运行的程序。

## 界面操作

### 设备配置与预览

1. 点击左侧“添加”，或菜单“文件 → 添加摄像机...”。
2. 填写“名称”“主码流”“辅码流”“保存地址”，四项均不能为空，然后点击“保存”。界面只检查非空和编码后的长度，不会在保存时验证连接或目录可写性。只有一个视频地址时，可将主、辅码流填为相同地址。
3. 从“设备列表”**拖拽设备到预览窗格**，使用该设备的**辅码流**开始预览；拖到已有画面的窗格会停止原会话并换源。单击设备是选择设备和检索录像，不会自动开始预览。只接收设备列表的拖拽，不支持将资源管理器文件直接拖入。
4. 顶部按钮、菜单“视图”或窗口右键“视图”均可切换分屏。缩小布局会销毁超出格数的预览会话；再次放大不会自动恢复这些源。
5. “搜索设备”按名称进行不区分大小写的过滤；选中设备后可“编辑”或“删除”，删除有确认对话框。编辑/删除配置不自动更新已经打开的预览，也不自动重启录像。

“实时预览”和“录像回放”仅切换中央区域的可见性：切到回放页不会停止已有预览或录像。预览禁用音频解码和音频设备；没有单路音量、暂停、停止或主辅码流切换控件。需要替换单路源时重新拖入设备，需要关闭多余会话时缩小布局或关闭应用。

### 录像

- 顶部“全部录像”、菜单“录像 → 开始全部录像”和右键“全部开始录制”作用相同：先停止旧录像，再为**配置中的所有设备**各建一条独立录像线程，读取**主码流**，与是否正在预览、当前分屏数及搜索过滤无关。仅由 `--source` 打开的地址不在此列表中，不会因此被录制。
- 顶部“停止录像”、菜单“录像 → 停止全部录像”或右键“全部停止录制”会停止并等待所有录像线程；关闭主窗口也会执行停止。不提供单设备录制开关、计划录像或录制时长设置。
- 录像直接将选中的视频压缩包重新封装为 MP4，不解码、不转码；尝试保留 MP4 可接受的一个音频流，音频不兼容或写头失败时尝试仅保存视频。不能保证任意编码都可封装。
- 每段从视频关键帧开始，按媒体时间戳达到 **120 秒后遇到的关键帧**切片，因此时长不一定恰好 120 秒；开始录制也可能需要等待首个关键帧。
- 录像输入使用 FFmpeg，RTSP 传输设为 TCP，设置 `rw_timeout=5000000`（微秒）。一次会话结束或失败后间隔 3 秒重试，直至停止；这与预览的有限重连策略不同。本地文件也可被这一输入路径打开，但 EOF 后同样会重开，不应作为“一次性文件转换”使用。
- 顶部状态文字不是每个设备的连接/写盘确认；即使显示“正在录制...”，也可能有输入或输出失败。错误主要由 FFmpeg 日志输出，当前没有逐设备错误面板、断线告警、磁盘空间管理、自动清理、录像加密或异常断电后的 MP4 修复保证。应检查实际文件并正常停止录制后再读取完整片段。

### 录像回放

1. 点击“录像回放”，再单击左侧设备，扫描该设备当前配置的录像目录。
2. 日历以红色粗体标出有录像的日期；点击日期，时间列表显示片段的起始时间。
3. 双击时间项，打开独立的模态回放窗口。支持声音、播放/暂停、进度条定位和 **0.2–2.0 倍速**；拖动进度或速度滑块后松开才提交操作。关闭回放窗口停止该会话。

检索只扫描该设备目录的顶层 `*.mp4` 和 `*.avi`，从文件基本名去掉前四个字符后解析 `yyyy_MM_dd_hh_mm_ss`；本应用生成的 `cam_...` 和同秒数字后缀文件可被识别。日期/时间来自**文件名中的本地创建时间**，不是媒体时间戳或连续录像时间轴。没有跨片段自动续播、任意目录浏览或自动刷新；新增片段后重新单击设备以刷新。修改保存地址或删除设备会改变检索结果。

## 配置与录像存储

- 设备配置固定在 **EXE 所在目录的 `config/monitor-cams.db`**，例如 `bin/config/monitor-cams.db`，与启动工作目录无关。程序自动创建配置子目录；安装位置需要可写，放在受保护的 `Program Files` 中可能导致保存失败，界面会显示错误，不会自动切换存储位置。新增/编辑时，相对录像保存地址会转换为当时工作目录下的绝对路径。
- 文件保存 `XCameraData` 定长二进制记录，使用本地 8 位编码，不是 SQLite。名称最多 1023 字节，主码流、辅码流和保存地址各最多 4095 字节（不含结尾空字符）。加载会检查记录长度、最多 10000 条及各字段的字符串结尾，保存通过 `QSaveFile` 原子替换；加载、迁移和保存失败均有界面提示。
- URL、用户名及密码均为**明文**；应限制配置文件访问权限。命令行含凭据 URL 还可能出现在进程列表中。
- 启动时优先加载固定目录中的配置；仅当它不存在时，依次查找当前工作目录、EXE 目录、EXE 上一级目录中的旧 `monitor-cams.db`，验证后原子写入新位置，保留原文件作为备份。首个找到的旧文件若损坏则提示失败，不会静默改用其他文件；已有新配置即使损坏也不会被旧文件覆盖。旧配置中的相对录像保存地址以旧配置所在目录为基准转换为绝对路径（如原先使用其他工作目录录制，需自行确认保存地址）。也可运行 `myplayer_monitor.exe --migrate-config` 仅执行配置加载/迁移后退出。旧参考项目的 `cams.db` 不会自动导入。通过结构检查不代表设备 URL 已通过连接验证。
- 录像路径为 `<保存地址>/<设备当前的零起始索引>/cam_yyyy_MM_dd_hh_mm_ss.mp4`，同秒冲突追加 `_1`、`_2` 等后缀。例如第一台设备保存地址为 `D:/recordings`，输出到 `D:/recordings/0/`。不会默认写到 `dist`，也没有统一的全局录像目录。
- 设备身份仍采用列表索引：删除设备会使后续索引前移，可能使历史目录与当前设备错配；编辑保存地址不会迁移旧录像。建议停止录像后维护配置，并单独备份配置和录像目录。

## 命令行

以下 PowerShell 示例假定已按下文独立构建 Release，并从仓库根目录启动：

```powershell
& ./bin/myplayer_monitor.exe
& ./bin/myplayer_monitor.exe --layout 4 --source "D:/video/example.mp4"
& ./bin/myplayer_monitor.exe --layout 4 `
  --source "rtsp://camera-a/stream" --source "rtsp://camera-b/stream"
& ./bin/myplayer_monitor.exe --smoke-test --layout 16
```

| 参数 | 实际行为 |
| --- | --- |
| `--source <location>` | 可重复，按顺序打开前几个窗格；接受本地文件或核心支持的 RTSP/HTTP 等地址。仅用于本次预览，不新增/保存设备，也不提供命令行录像。 |
| `--layout <count>` | 仅接受 `1`、`4`、`9`、`16`，默认 `9`；源数量不得超过布局格数，否则退出码为 1。 |
| `--smoke-test` | 打开窗口并提交初始预览后，安排约 1 秒后退出。无源时只是窗口启动检查，不证明视频解码、网络连通或录像成功。 |
| `-h` / `--help` | 输出参数帮助。 |
| `-v` / `--version` | 输出应用版本，当前为 `1.0.0`。 |

初始媒体打开包含异步过程，进程未立即报错不等于已收到画面；主窗口尚未将播放核心的逐路错误信号接到可见提示中。

## 构建

### 前置条件

- CMake **3.25 或更高**、C++20 编译器、Qt **6.8 或更高**的 Core/Gui/Widgets，以及 FFmpeg、SDL2、SoundTouch、pkg-config/libass 和仓库内的 sigslot 头文件。
- Windows 示例使用 **x64 MSVC** 和匹配的 Qt MSVC x64 套件，不能改用 Qt MinGW 套件混搭。示例中的 `Visual Studio 18 2026` 生成器需要 **CMake 4.2+**；使用 VS 2022 时可改为 `Visual Studio 17 2022`。须安装“使用 C++ 的桌面开发”和 Windows SDK。
- 普通终端 PATH 未包含 `cl`/`msbuild` 不等于未安装：Visual Studio 生成器可以定位安装实例；使用 Ninja 则应先进入 x64 Developer PowerShell/开发命令行。

以下命令均在**仓库根目录**的 PowerShell 执行；将 Qt 路径和 vcpkg 根目录改为自己的安装路径。不要复用已清理的构建缓存或把原 `build/.../vcpkg_installed` 当成仍然存在的依赖。

### 方式一：从仓库根目录构建两个应用

根 CMake 默认只构建 Core SDK，两个应用必须显式启用。此路径通过 CMake/toolchain 查找依赖；监控和播放器都使用共享 Qt SDK 组件，因此都需要 pkg-config/libass，不使用 `MONITOR_USE_BUNDLED_DEPS` 来替代这些依赖。

```powershell
$env:VCPKG_ROOT = "D:/ProgramData/vcpkg"
$qt = "D:/Qt/Qt6/6.9.3/msvc2022_64"
cmake -S . -B build/full -G "Visual Studio 18 2026" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  "-DPLAYERDEMO_QT_ROOT=$qt" `
  -DMYPLAYER_BUILD_QT_APP=ON -DMYPLAYER_BUILD_MONITOR=ON `
  -DBUILD_TESTING=OFF
cmake --build build/full --config Release --target myplayer myplayer_monitor
& ./bin/myplayer_monitor.exe
```

两个应用默认都输出到仓库 `bin/`，可通过 `PLAYERDEMO_OUTPUT_DIR` 修改。监控的 Release 文件名为 `myplayer_monitor.exe`，Debug 为 `myplayer_monitor_debug.exe`；独立构建监控时也使用相同规则。若只需从根项目构建监控，可设 `MYPLAYER_BUILD_QT_APP=OFF`；这不会启用下面的备份依赖分支，根项目仍需查找到核心依赖。

### 方式二：独立构建监控，显式使用仓库备份依赖

```powershell
$qt = "D:/Qt/Qt6/6.9.3/msvc2022_64"
cmake -S apps/monitor -B build/monitor -G "Visual Studio 18 2026" -A x64 `
  -DMONITOR_USE_BUNDLED_DEPS=ON `
  -DMONITOR_BUILD_TESTS=OFF `
  "-DCMAKE_PREFIX_PATH=$qt"
cmake --build build/monitor --config Release --target myplayer_monitor
& ./bin/myplayer_monitor.exe
```

`MONITOR_USE_BUNDLED_DEPS` 默认 **OFF**，只在 `apps/monitor` 作为顶层工程时定义，开启后仅支持 Windows x64：直接选用 `lib/ffmpeg-7.1.1` 的头文件/导入库、`lib/SDL2/cmake` 和 `lib/soundtouch-2.3.3/lib`，无需借用已清理的整仓构建产物；Qt、pkg-config 和 libass 及其传递依赖仍须另外准备（例如由兼容的 vcpkg 安装前缀提供，并配置 pkg-config 搜索路径）。该路径不构建单播放器，但仍构建共享 Qt SDK 显示/字幕组件；备份目录并不包含完整的 libass 依赖。关闭此选项时，应提供能查找 FFmpeg、SDL2、SoundTouch 的 toolchain 或包前缀，例如加入：

```powershell
cmake -S apps/monitor -B build/monitor-vcpkg -G "Visual Studio 18 2026" -A x64 `
  -DMONITOR_USE_BUNDLED_DEPS=OFF `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows "-DCMAKE_PREFIX_PATH=$qt"
```

Windows 监控目标的构建后步骤已复制 CMake 可识别的运行时 DLL，并调用该 Qt 套件的 `windeployqt`；备份模式另复制 FFmpeg DLL，**不是要求手工再跑一遍部署**。不过 `windeployqt` 参数包含 `--no-compiler-runtime`，这仍不是带 MSVC 运行库、完整许可证和校验文件的正式便携包。

### 独立构建不等于双软件打包

独立工程没有根项目的播放器安装树，不能把 `build/monitor` 直接交给 `scripts/package-portable.ps1`。该脚本读取整仓配置中的 Qt/vcpkg 路径，要求启用监控，同时构建两个 Release 目标，再分别生成两个 ZIP、运行库/插件、许可证及 SHA-256 文件。发行流程和已有 **V1.1.0 双软件发行**见[根 README](../../README.md)；脚本不负责发布到远端，且已有同版本输出时会拒绝覆盖。

## 验证范围与建议

当前测试代码提供：

- `monitor_integration_tests`：生成本地媒体，检查 16 路各自收到帧并经共享 VideoWidget 呈现、禁音频预览拒绝纯音频源、换源、销毁部分窗格、停止会话，以及 2 秒测试分片可被 FFmpeg 重新打开并读包。
- `monitor_window_smoke_test`：以 `--smoke-test --layout 16` 打开并自动关闭主窗口，不接真实摄像头。

在完成依赖配置后，可用以下命令构建并执行独立监控测试；是否通过应以实际运行结果为准：

```powershell
cmake -S apps/monitor -B build/monitor -G "Visual Studio 18 2026" -A x64 `
  -DMONITOR_USE_BUNDLED_DEPS=ON `
  -DMONITOR_BUILD_TESTS=ON `
  "-DCMAKE_PREFIX_PATH=$qt"
cmake --build build/monitor --config Release --target myplayer_monitor monitor_integration_tests
ctest --test-dir build/monitor -C Release --output-on-failure
```

无论顶层集成构建还是独立构建，监控程序及构建所需运行库都输出到仓库 `bin/`（Debug 为 `myplayer_monitor_debug.exe`，Release 为 `myplayer_monitor.exe`）。

这些测试不能证明真实 RTSP 摄像头兼容性、16 路 4K 性能、长时间录像可靠性、网络重连期间录像完整性或全部音视频编码组合。预览使用核心的默认网络超时和有限重连策略，但 UI 无网络参数设置、ONVIF 发现、PTZ、自动扫描或摄像头配置下发功能；不会自动连接任何参考项目设备。

## 实现与来源

每个预览窗格和录像回放窗口各持有一个公开 SDK `QtPlayer`，显示统一由 `VideoWidget` 处理；Core 与 Qt 各用最新帧邮箱，不逐帧积压 Qt 消息。Qt 组件通过 SDL 纹理显示 CPU 帧和字幕，普通 YUV420P 直接上传 IYUV，只有 BT.2020 颜色空间需要 swscale 回退；不再维护监控专用 renderer/33ms 轮询，而是共享控件管理 SDL。硬件解码帧仍经系统内存，不是零拷贝显示；性能仍需实测。录像仍由独立 FFmpeg 封装线程完成，使用公开 `NetworkRuntime` lease，不访问核心私有对象。

界面、日历、相机配置及图标改编自用户提供的 `h265-ffmpeg/2_project/xviewer`（Apache-2.0），对应许可证保留在 `REFERENCE-LICENSE.txt`；本应用不修改外部参考项目。原有图标仅作为参考界面资源保留，不表示本应用是任何厂商的官方产品。
