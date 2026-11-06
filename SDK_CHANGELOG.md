# SDK 变更记录

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

C ABI、稳定二进制 ABI、预编译 SDK、共享 Core 构建承诺、QML、跨平台保证、零拷贝/HDR 支持、商业授权结论。详细契约、部署与未验证边界见 `SDK_README.md`。
