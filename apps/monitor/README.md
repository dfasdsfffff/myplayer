# MyPlayer 多路监控

本应用位于 `apps/monitor`，是当前仓库中的独立 Qt Widgets 应用，不修改外部参考项目。默认九宫格，支持 1/4/9/16 路预览。

## 实现

- 每个预览窗格和录像回放窗口分别持有一个 `PlaybackRuntime`，复用本仓库 `play_core`，不使用旧 XCodec/XPlayer。
- 工作线程只发布数据；GUI 线程以 33ms 定时消费有界的最新帧槽并通过 SDL 渲染，YUV420P 直接上传 YUV 纹理。慢 GUI 不积压逐帧 Qt 消息。
- 预览通过 `MediaSource::enableAudio=false` 禁用音频解码及设备；录像回放支持声音、暂停、按比例 seek 和 0.2–2.0 倍速。
- 网络源使用核心的超时及重连机制。拖拽相机到窗格切换辅码流；缩小布局和关窗会停止并等待各自会话。
- 主码流录像与预览独立：直接封装压缩包到 MP4，不解码重编码；默认 120 秒分片，在阈值后的关键帧切片。兼容的音频会一起保存，否则回退为视频。
- 录像输出沿用 `<保存目录>/<相机索引>/cam_yyyy_MM_dd_hh_mm_ss.mp4`。同秒冲突使用数字后缀，检索可识别。相机索引仍是旧格式身份，删除相机可能改变后续目录对应关系。
- ONVIF 没有接入：参考项目的 ONVIF 也只是独立发现实验，本应用不会伪称已有发现或 PTZ 功能。

## 构建

常规依赖环境可以通过仓库根目录启用 `-DMYPLAYER_BUILD_MONITOR=ON`，目标为 `myplayer_monitor`。默认关闭，不影响原 `myplayer`。

也支持独立构建，避免依赖原单播放器的 UI/字幕构建配置：

```powershell
cmake -S apps/monitor -B build/monitor -G "Visual Studio 18 2026" -A x64 `
  -DMONITOR_USE_BUNDLED_DEPS=ON `
  -DMONITOR_BUILD_TESTS=ON `
  -DCMAKE_PREFIX_PATH="D:/Qt/Qt6/6.9.3/msvc2022_64"
cmake --build build/monitor --config Debug
ctest --test-dir build/monitor -C Debug --output-on-failure
```

`MONITOR_USE_BUNDLED_DEPS` 仅用于 Windows x64，显式选择仓库备份的 FFmpeg 7.1.1、SDL2 和 SoundTouch 2.3.3。关闭此选项则由 CMake/toolchain 查找依赖（推荐正式部署使用受维护的依赖构建）。编译后复制运行库，并使用当前 Qt 的 windeployqt 部署插件；不依赖外部参考项目的 FFmpeg 4.3。

## 运行

```powershell
./build/monitor/Debug/myplayer_monitor.exe
./build/monitor/Debug/myplayer_monitor.exe --layout 4 --source "D:/video/example.mp4"
./build/monitor/Debug/myplayer_monitor.exe --layout 4 --source "rtsp://camera-a/stream" --source "rtsp://camera-b/stream"
```

`--source` 可重复，数量不超过布局格数。不需要命令行参数也可通过 UI 添加相机并拖入窗格。命令行 URL 不写入相机配置，但带凭据 URL 可能被操作系统进程列表记录。

相机配置保存在当前工作目录的 `monitor-cams.db`，保持参考项目原始定长二进制布局与本地编码，但使用原子保存并检查长度。若需要迁移旧配置，手工复制旧 `cams.db` 为新工作目录的 `monitor-cams.db`，原文件不被自动修改。配置中的 URL/凭据是明文，应限制文件访问权限。输入无效文件不应当作迁移成功。

右键菜单切换布局、开始/停止全部录像；回放页选择相机、日期、时间后双击打开文件。只预览已显式配置的设备，不会自动扫描网络或连接参考项目中的摄像头。

## 验证范围

自动集成测试生成本地视频，验证 16 路独立核心同时收到帧并成功调用 SDL 显示、换源、销毁部分窗格、停止剩余会话，以及分片录像重新打开和读取。另有主窗口 16 分屏启动/自动关闭测试。测试不依赖真实摄像头。

尚不能由这些测试证明真实 RTSP 摄像头兼容性、16 路 4K 性能、长时间录像可靠性或所有音视频编码组合。应使用实际设备继续验收。核心的硬件帧仍经过系统内存，并非零拷贝显示。

最终验证记录：包含 `enableAudio` 预览禁音频选项、对应测试及配置加载保护调整的源码已重新编译。监控应用 Debug 和 Release 构建均成功，各自的集成测试与主窗口测试均通过（各 2/2）；重新编译后的 Debug 播放核心回归测试也全部通过（22/22）。验证使用上述独立构建及仓库备份依赖，不代表已经完成真实摄像头或长时间负载验收。

## 来源及许可

界面、日历、相机配置及图标改编自用户提供的 `h265-ffmpeg/2_project/xviewer`（Apache-2.0）。对应许可证保留在 `REFERENCE-LICENSE.txt`。播放链已替换为本仓库核心，录像链已重写为独立 FFmpeg 封装线程；原项目文件保持不变。原有图标仅作为参考界面资源保留，不表示本应用是任何厂商的官方产品。
