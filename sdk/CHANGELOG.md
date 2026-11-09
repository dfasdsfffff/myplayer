# SDK 变更记录

## 2.1.0 — 发行准备

本节按 Git 标签 `2.0.1` 到当前源码的 SDK 变化整理，目标发行标签为 `v2.1.0`。只记录已存在的实现，不表示附件已发布或本次执行了构建/测试。

### 平台与依赖适配

- Core 优先复用 `SoundTouch::SoundTouchDLL`，也接受 `SoundTouch::SoundTouch`；非 DLL 路径使用 SoundTouch C++ API，并适配整数/浮点采样，移除对 Windows 专用 DLL 头的无条件依赖。
- Qt 视频组件在 Linux/macOS 不创建 SDL 原生子窗口：SDL software surface 合成后读回 QImage，交给 QPainter 显示；Windows 正常窗口仍走原生 SDL 路径。不能再将“正常播放不读回”描述为所有平台通用保证。
- Linux 的 SDL 音频初始化失败且未显式设置 `SDL_AUDIODRIVER` 时，记录警告并尝试 dummy 静音输出；显式指定驱动时不自动回退，最终失败仍报告错误。
- Linux Qt 组件在 vcpkg 安装树存在 `lib/libfontconfig.so` 时优先链接该库，降低 Qt 传递依赖混用风险；不保证任意依赖组合兼容。
- Core 部分字符串构造不再依赖 `std::format`，减轻不同标准库实现的编译约束；语言基线仍为 C++20。
- 显式启用 sanitizer 时，静态库将运行时链接选项传递给最终消费者；编译插桩与普通警告选项仍为私有。

### 兼容、交付与验证边界

- C ABI 仍为 v1，公开 C 类型和函数签名未在本段变化中升级。`play_core::Version` / `ps_version()` 和 CMake 库版本仍为 `1.0.0`，不要与项目发行号混用。
- C ABI 安装/导出包仍限定 Windows x64/MSVC Release；Linux/macOS 源码适配不代表已有对应安装包或完整运行验证。
- 源码打包脚本仍固定输出 `PlayerSDK-1.0.0-source.zip`；文档中的 v2.1.0 暂存目录不会改变该文件名。历史验证记录不作为 v2.1.0 通过证明。

## 2.0.1 源码基线中已有的 SDK 演进

以下原“未发布”条目在标签 `2.0.1` 中已存在，保留实施历史，不作为 v2.1.0 新增功能。它们当时未分别标注独立 SDK 发行版本。

### 播放与显示回归修复

- 监控空预览窗延迟创建播放器；SDL 音频初始化失败时返回错误，不再在窗口构造时终止进程。
- SDL 软件渲染在上传 YUV 纹理前设置色彩转换模式，覆盖全范围视频并在呈现后恢复全局模式。
- 播放器切换媒体时清除上一部的外挂字幕；新增对应回归测试。

### 视频帧发布去除 CPU 深拷贝

- YUV 平面共享 FFmpeg 引用，BGRA 通过共享所有权保留内部帧；公开 C++ `VideoFrame::bgra` 改为 `shared_ptr<const vector<uint8_t>>`，手动提交帧须提供不可变共享缓冲。
- 位图字幕纹理在字幕帧生命周期内复用；ASS 字幕在 libass 报告内容未变化时复用纹理，轨道、输出尺寸或 SDL 渲染器变化时失效。

### C ABI v1

- 新增默认启用的 `PlayerSDK::C` 共享库，保留现有 Core/Qt C++ 接口。
- 提供不透明播放器/事件句柄、版本化 C 布局、完整 Core 控制和媒体/帧/字幕访问。
- 明确 UTF-8、调用约定、异常隔离、异步结果、事件保留和跨 DLL 内存所有权。
- 新增纯 C 消费者、x64 布局基线、播放集成测试与 C 示例；详见 `c/README.md`。

### SDK 目录统一

- SDK 源码、Qt 组件、示例、公开测试、外部接入测试及 sigslot 收拢到 `sdk/`。
- 独立构建、宿主嵌入与源码包共用同一个 CMake 入口；公开 include 和目标名不变。
- 应用与私有回归测试仍位于仓库根目录，SDK 专用测试夹具放在 `sdk/tests/`。

### 恢复共享 SDL 显示后端

- 在 `feature/player-sdk-sdl` 分支保留首版 SDK 提交 `5efab6a`，随后仅调整 Qt 显示层及验证/文档。
- VideoWidget 恢复 SDL 原生子窗口、硬件优先/软件回退渲染器，普通 YUV420P 直接上传 IYUV，BGRA 直接上传纹理；BT.2020 保留定向 swscale 转换。
- 视频/位图/文字/ASS 字幕由 SDL 合成；Qt 仅负责窗口布局、输入事件及空背景。
- 原生窗口销毁前释放 SDL 资源；独立持有 SDL video 初始化引用，不调用全局 SDL_Quit。
- 新增 `grabVideoFrame()` 用于显式 SDL 像素读回；无界面测试使用 SDL software surface，不影响正常播放路径。
- 基准报告 schema 3 记录 SDL_RenderPresent 返回，不沿用 QPainter 字段。

## 1.0.0 — 首版源码 SDK（2026-11-06，本地实现快照）

以下保留首版当时的范围和验证背景；“尚未创建发行标签”、QPainter 路径和“不包含 C ABI”均指该快照，不代表当前项目状态。当前平台/ABI/交付边界见上文及接入指南。

### 新增

- `PlayerSDK::Core` 直接指向静态 `play_core`，公开 C++20 API 与私有实现目录隔离。
- `Player`、不可变 CPU 视频/字幕帧、媒体能力快照、RAII 订阅、session/operation 及异步终结结果。
- 分离 open/play、设置式暂停、毫秒定位、保留会话的 stop、异步 close，以及命令/通知预算。
- 可选 `PlayerSDK::QtWidgets`：QObject 线程投递、最新帧邮箱、统一画面与字幕控件。
- 独立 Core/Qt 示例、仓库外 consumer、公开接口/SDL 共存/Qt 显示测试、独立源码归档脚本。

### 迁移与行为变化

- 两个应用改用公开 SDK；应用播放列表/历史/录像不进入 Core。
- 旧私有 Runtime/Controller 仅保留作仓库白盒测试，不是第二套公开 API。
- 根项目默认仅 Core；开发应用/回归通过带前缀选项或现有 presets 显式开启。
- `stop()` 不再等于释放媒体；需要旧式停止释放行为时调用 `close()`。
- `seek()` 单位为毫秒，完成语义为解封装成功，不是首帧确认。
- 倍速受理范围与内部统一为 `[0.1,2.0]`，不承诺所有媒体下的音画同步质量。
- SDK 实时源使用普通 packet 背压；暂停直播不无限堆积，但恢复不保证实时边缘。
- 显示统一为 CPU 帧 + swscale/QImage/QPainter，不沿用旧 SDL GPU 呈现性能结论。
- 内嵌字幕不再只入队位图；文字/ASS 包携带样式头交给共享 Qt 组件。

### 不包含

C ABI、稳定二进制 ABI、预编译 SDK、共享 Core 构建承诺、QML、跨平台保证、零拷贝/HDR 支持、商业授权结论。详细契约、部署与未验证边界见 `README.md`。
