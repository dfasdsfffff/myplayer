# 播放核心历史评审记录与待验证线索（v2.1.0 源码核对）

记录日期：2026-11-05。原记录范围：原 `play_core/`，以及当时 Qt 播放器视频显示与最新帧邮箱调用链。当前核心实现位于 `sdk/core/src/`，共享显示实现位于 `sdk/qt/`。

## 文档性质与当前适用范围

这是历史评审留下的优化候选与待验证线索，不是已确认缺陷清单、性能基准或完整安全/并发审计报告。本次为项目 v2.1.0 发行准备，仅对照当前实现及 `2.0.1` 后的源码变化修订说明，没有重新执行构建、播放测试、GUI 手工验证、性能基准或线程检测器，也不据此宣告问题已修复或仍可复现。

当前架构入口见[开发指南](PROJECT_GUIDE.md)，构建/测试/打包命令统一见[根 README](../README.md)。测试集合与运行条件以[顶层 CMake](../CMakeLists.txt)及[监控 CMake](../apps/monitor/CMakeLists.txt)为准，不沿用旧构建目录或固定测试数量。

播放器 `Show` 与监控 `CoreVideoWidget` 现在都使用 SDK 的 `VideoWidget` 显示；原有 `LatestVideoFrameMailbox` 和字幕 renderer 已移到 `tests/support/`，仅供历史回归测试。以下邮箱观察需要按当前 Core/Qt SDK 事件路径重新测量，不能直接作为现行瓶颈结论。

### v2.1.0 新增适配对历史结论的影响

- `VideoWidget` 在 Windows 正常窗口仍以 SDL 原生子窗口显示；Linux/macOS 以及 Windows 的 `offscreen`/`minimal` 使用 SDL software surface 合成、`SDL_RenderReadPixels` 读回，再由 QPainter 绘制。因此“正常播放不读回、QPainter 仅绘制空背景”只适用于 Windows 正常原生路径；非 Windows 显示仍有读回/绘制成本。
- `framePresented()` / `presentedFrames()` 仅统计当前渲染路径已准备的新帧，不代表 QWidget 已完成绘制或屏幕可见；历史 schema 3 的 SDL present 统计不能直接拿来比较不同显示路径的可见帧率。
- SoundTouch 已同时支持 DLL 包装与 C++ API，并处理整数/浮点采样转换。Linux 音频初始化失败且未指定 `SDL_AUDIODRIVER` 时可尝试 dummy 静音输出；构造成功/自动测试播放不代表真实设备有声。
- Linux Qt 的 vcpkg Fontconfig 优先链接与静态库 sanitizer 运行时链接传播属于构建适配，不等同于字体全覆盖或已通过 sanitizer 检测。平台构建/部署约定见 [SDK 指南](../sdk/README.md)。

## 优化候选（不是已测量的瓶颈）

| 历史编号 | 当前源码可确认的机制 | 适用边界 |
| --- | --- | --- |
| D-005 | Windows D3D11VA 帧经 `HardwareDecodeContext::transferToSoftware()` / `av_hwframe_transfer_data()` 下载到 CPU，再上传 SDL 纹理；非 Windows 目前不启用 D3D11VA，并额外使用软件合成/读回显示 | 当前不是硬件纹理零拷贝。应分平台测量解码、下载、转换、上传以及适用时的读回/Qt 绘制成本，不能未经测量直接推导应更换后端 |
| D-006 | 核心先执行 `VideoFrameConverter::convert()` / 派发，Core 与 Qt SDK 的事件队列之后分别合并待交付的视频帧 | 邮箱不能消除已完成的转换/引用开销。偶数尺寸 YUV420P 保留 plane 引用，公开 BGRA 以 aliasing shared_ptr 持有私有帧，事件发布不再深拷贝像素；其他格式仍可能先转 BGRA。“转换前丢帧”需结合格式、同步与持有者评估 |
| D-007（剩余部分） | 转换器仅复用 `use_count()==1` 的空闲 `VideoFrame`，硬件下载复用 `Decoder::transferFrame`；YUV420P 引用路径仍 `av_frame_alloc()` / `av_frame_ref()`，所有帧被持有时仍创建新对象 | 不能再概括为每帧所有对象均重新分配，也不能宣称热路径已无分配。BGRA 与 YUV 公开持帧都会延长 backing 生命周期；是否需要 AVFrame 池须先测量并维护跨线程引用所有权 |

对应证据入口：

- `sdk/core/src/decoder.cpp`：`Decoder::decode_frame()`。
- `sdk/core/src/hardware_decode.cpp`：`HardwareDecodeContext::transferToSoftware()`。
- `sdk/core/src/video_frame_converter.cpp`：`VideoFrameConverter::convert()`、`acquireFrame()`。
- `sdk/core/src/player.cpp`、`sdk/qt/qt_player.cpp`：Core/Qt 事件合并及交付。
- `sdk/qt/video_widget.cpp`：两个应用共用的 SDL 合成；Windows 原生路径直接呈现，软件 surface 路径额外读回交给 QPainter，上传前设置 YUV 色彩模式。

## 保留的待验证线索

下面均未在本次文档更新中确认是实际播放故障；接口层异常条件与播放入口可达性需要分开判断。

1. **倍速与 EOF 尾部**：`AudioOutput::DecodeFrame()` 的 SoundTouch 路径（包括新 C++ API 包装），以及 `DecoderWorkers` 中可选音视频滤镜的排空。默认 `CONFIG_AVFILTER` 为 0；不能仅凭滤镜分支存在，就宣称默认播放已通过滤镜 EOF 验证。读取线程当前等待 codec serial、帧队列以及 `AudioRenderQueue::drained()`，这与 SoundTouch/重采样器等处理库内部尾部完整排空不是同一保证；两种 SoundTouch 接入路径应分别覆盖。
2. **暂停/恢复与已缓冲音频**：`VideoCtl::stream_toggle_pause()` 更新 paused 和时钟，SDL 回调继续消费已准备 PCM；需要验证设备缓冲、恢复时钟重锚定及用户可感知的拖尾，而不是把暂停标志等同于已暂停 SDL 设备。
3. **`AVChannelLayout` 所有权**：音频设备/源/滤镜参数间的复制、重配置与释放，特别是自定义布局。应逐处区分 `av_channel_layout_copy()` 与结构赋值的生命周期，不能单靠普通声道布局播放通过来消除此线索。
4. **边界值与实际入口**：SoundTouch 包装参数、网络超时/退避计算、`PacketQueue` 计数的极端值。现有 `ValidateMediaSource()`、偏好值整理和相关测试不能自动证明任意低层包装调用均安全，也不能把不合法直接调用推定为可从 UI 触发的故障。

相关定位文件：`sdk/core/src/audio_output.cpp`、`audio_render_queue.cpp`、`decoder_workers.cpp`、`soundtouch_wrap.cpp`、`network_input.cpp`、`packet_queue.cpp`、`video_state.h`，以及 `apps/qt_player/app_preferences.cpp`。

## 未由本记录提供的验证结论

- 可见 Qt 窗口下的真实音视频、seek/暂停/停止/EOF、字幕和不同设备的播放表现。
- D3D11VA 实机覆盖、硬件零拷贝方案以及转换前丢帧的性能收益。
- 多实例/多路监控长时运行与录制行为。
- 线程检测器、竞态复现或完整资源泄漏检测结果。
- Linux/macOS 的窗口系统、软件读回显示性能、实际声卡/字体环境及不同 SoundTouch 采样类型组合。

后续若开展专项验证，应记录使用的源码版本、输入媒体/协议、设备、运行条件与实际观察，再决定是否将某条线索转为缺陷或关闭。不能把本文措辞更新本身当作一次重新审计。
