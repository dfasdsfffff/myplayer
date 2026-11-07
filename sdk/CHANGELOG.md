# SDK 变更记录

## 未发布 — 视频帧发布去除 CPU 深拷贝

- YUV 平面共享 FFmpeg 引用，BGRA 通过共享所有权保留内部帧；公开 C++ `VideoFrame::bgra` 改为 `shared_ptr<const vector<uint8_t>>`，手动提交帧须提供不可变共享缓冲。
- 位图字幕纹理在字幕帧生命周期内复用；ASS 字幕在 libass 报告内容未变化时复用纹理，轨道、输出尺寸或 SDL 渲染器变化时失效。

## 未发布 — C ABI v1

- 新增默认启用的 `PlayerSDK::C` 共享库，保留现有 Core/Qt C++ 接口。
- 提供不透明播放器/事件句柄、版本化 C 布局、完整 Core 控制和媒体/帧/字幕访问。
- 明确 UTF-8、调用约定、异常隔离、异步结果、事件保留和跨 DLL 内存所有权。
- 新增纯 C 消费者、x64 布局基线、播放集成测试与 C 示例；详见 `c/README.md`。

## 未发布 — SDK 目录统一

- SDK 源码、Qt 组件、示例、公开测试、外部接入测试及 sigslot 收拢到 `sdk/`。
- 独立构建、宿主嵌入与源码包共用同一个 CMake 入口；公开 include 和目标名不变。
- 应用与私有回归测试仍位于仓库根目录，SDK 专用测试夹具放在 `sdk/tests/`。

## 未发布 — 恢复共享 SDL 显示后端

- 在 `feature/player-sdk-sdl` 分支保留首版 SDK 提交 `5efab6a`，随后仅调整 Qt 显示层及验证/文档。
- VideoWidget 恢复 SDL 原生子窗口、硬件优先/软件回退渲染器，普通 YUV420P 直接上传 IYUV，BGRA 直接上传纹理；BT.2020 保留定向 swscale 转换。
- 视频/位图/文字/ASS 字幕由 SDL 合成；Qt 仅负责窗口布局、输入事件及空背景。
- 原生窗口销毁前释放 SDL 资源；独立持有 SDL video 初始化引用，不调用全局 SDL_Quit。
- 新增 `grabVideoFrame()` 用于显式 SDL 像素读回；无界面测试使用 SDL software surface，不影响正常播放路径。
- 基准报告 schema 3 记录 SDL_RenderPresent 返回，不沿用 QPainter 字段。

## 1.0.0 — 首版源码 SDK（2026-11-06，本地实现，尚未创建发行标签）

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
