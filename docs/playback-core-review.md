# 播放核心评审记录与待验证线索

记录日期：2026-11-05。原记录范围：`play_core`，以及 Qt 播放器视频显示与最新帧邮箱调用链。

## 文档性质与当前适用范围

这是历史评审留下的优化候选与待验证线索，不是已确认缺陷清单、性能基准或完整安全/并发审计报告。本次仅对照当前实现修订说明，没有重新执行播放测试、GUI 手工验证、性能基准或线程检测器，也不据此宣告问题已修复或仍可复现。

当前架构入口见[开发指南](PROJECT_GUIDE.md)，构建/测试/打包命令统一见[根 README](../README.md)。测试集合与运行条件以[顶层 CMake](../CMakeLists.txt)及[监控 CMake](../apps/monitor/CMakeLists.txt)为准，不沿用旧构建目录或固定测试数量。

播放器 `Show` 与监控 `CoreVideoWidget` 是不同的显示实现；以下原有邮箱观察主要针对播放器 `PlaybackRuntimeBridge` / `LatestVideoFrameMailbox`，不能自动当成监控显示后端的审计结论。

## 优化候选（不是已测量的瓶颈）

| 历史编号 | 当前源码可确认的机制 | 适用边界 |
| --- | --- | --- |
| D-005 | `Decoder::decode_frame()` 对 D3D11VA 帧调用 `HardwareDecodeContext::transferToSoftware()`，经 `av_hwframe_transfer_data()` 下载；显示端再上传 SDL 纹理 | 当前不是硬件纹理零拷贝。是否构成瓶颈需要分辨解码、下载、转换、上传各段成本；不能未经测量直接推导应更换后端 |
| D-006 | 核心先执行 `VideoFrameConverter::convert()` / 派发，播放器桥接层之后才将新帧发布到最新帧邮箱并覆盖旧帧 | 邮箱能合并 GUI 投递，但不能消除已完成的转换/引用开销。偶数尺寸 YUV420P 已走保留 plane 引用的路径，并非每帧都转 BGRA；“转换前丢帧”需结合实际格式、同步与持有者评估 |
| D-007（剩余部分） | 转换器复用空闲 `VideoFrame`，硬件下载复用 `Decoder::transferFrame`；但 YUV420P 引用路径仍 `av_frame_alloc()` / `av_frame_ref()`，所有对象被持有时仍创建新的 `VideoFrame` | 不能再概括为每帧所有对象均重新分配，也不能宣称热路径已无分配。是否需要 AVFrame 池必须先测量，且维护跨线程引用生命周期 |

对应证据入口：

- `play_core/decoder.cpp`：`Decoder::decode_frame()`。
- `play_core/hardware_decode.cpp`：`HardwareDecodeContext::transferToSoftware()`。
- `play_core/video_frame_converter.cpp`：`VideoFrameConverter::convert()`、`acquireFrame()`。
- `apps/qt_player/playback_runtime_bridge.cpp`：`attach()`、`scheduleVideoFrameDelivery()`、`drainLatestVideoFrame()`。
- `apps/qt_player/latest_video_frame_mailbox.cpp`：最新帧发布/取出与投递合并。
- `apps/qt_player/show.cpp`、`apps/monitor/core_video_widget.cpp`：分别检查实际纹理上传/呈现，不能假设二者完全相同。

## 保留的待验证线索

下面均未在本次文档更新中确认是实际播放故障；接口层异常条件与播放入口可达性需要分开判断。

1. **倍速与 EOF 尾部**：`AudioOutput::DecodeFrame()` 的 SoundTouch 路径，以及 `DecoderWorkers` 中可选音视频滤镜的排空。默认 `CONFIG_AVFILTER` 为 0；不能仅凭滤镜分支存在，就宣称默认播放已通过滤镜 EOF 验证。读取线程当前等待 codec serial、帧队列以及 `AudioRenderQueue::drained()`，这与处理库内部尾部完整排空不是同一保证。
2. **暂停/恢复与已缓冲音频**：`VideoCtl::stream_toggle_pause()` 更新 paused 和时钟，SDL 回调继续消费已准备 PCM；需要验证设备缓冲、恢复时钟重锚定及用户可感知的拖尾，而不是把暂停标志等同于已暂停 SDL 设备。
3. **`AVChannelLayout` 所有权**：音频设备/源/滤镜参数间的复制、重配置与释放，特别是自定义布局。应逐处区分 `av_channel_layout_copy()` 与结构赋值的生命周期，不能单靠普通声道布局播放通过来消除此线索。
4. **边界值与实际入口**：SoundTouch 包装参数、网络超时/退避计算、`PacketQueue` 计数的极端值。现有 `ValidateMediaSource()`、偏好值整理和相关测试不能自动证明任意低层包装调用均安全，也不能把不合法直接调用推定为可从 UI 触发的故障。

相关定位文件：`play_core/audio_output.cpp`、`audio_render_queue.cpp`、`decoder_workers.cpp`、`soundtouch_wrap.cpp`、`network_input.cpp`、`packet_queue.cpp`、`video_state.h`，以及 `apps/qt_player/app_preferences.cpp`。

## 未由本记录提供的验证结论

- 可见 Qt 窗口下的真实音视频、seek/暂停/停止/EOF、字幕和不同设备的播放表现。
- D3D11VA 实机覆盖、硬件零拷贝方案以及转换前丢帧的性能收益。
- 多实例/多路监控长时运行与录制行为。
- 线程检测器、竞态复现或完整资源泄漏检测结果。

后续若开展专项验证，应记录使用的源码版本、输入媒体/协议、设备、运行条件与实际观察，再决定是否将某条线索转为缺陷或关闭。不能把本文措辞更新本身当作一次重新审计。
