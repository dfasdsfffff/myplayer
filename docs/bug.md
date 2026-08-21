当前项目的整体架构方向是合理的：已经将播放核心从 Qt UI 中拆出，具备 RAII 封装、网络播放配置、基础测试和发布脚本。但仍有一些需要优先处理的正确性和并发问题，建议先修复这些问题，再进行性能和结构优化。

## 优先级最高：正确性与并发

### P0：停止或退出时可能卡死

`VideoCtl` 析构时只停止播放循环并等待线程：

- `play_core/videoctl.cpp:2424`
- `play_core/videoctl.cpp:2428`

但没有取消网络重连，也没有唤醒重连条件变量。播放线程可能在析构期间重新打开媒体，并再次把 `m_bPlayLoop` 设为 `true`：

- `play_core/videoctl.cpp:2103`
- `play_core/videoctl.cpp:2109`
- `play_core/videoctl.cpp:2119`

这可能导致应用退出卡住，尤其是在网络断开并进入重连阶段时。

建议统一所有停止路径：

1. 设置 `m_reconnectCancelled = true`。
2. 调用 `m_reconnectCv.notify_all()`。
3. 中止当前媒体 I/O。
4. 最后再 `join()`。
5. 为 `wait_for` 添加取消谓词。

### P0：SDL 初始化失败会破坏引用计数

`VideoCtl::Init()` 在 `SDL_Init` 失败时已经回退了一次计数：

- `play_core/videoctl.cpp:2393`
- `play_core/videoctl.cpp:2398`

随后 `MakeInstance()` 中的临时对象析构，又会无条件减一次：

- `play_core/videoctl.cpp:2416`
- `play_core/videoctl.cpp:2445`

计数会从 `0` 变成 `-1`，后续实例可能跳过 `SDL_Init`，却被认为初始化成功。

此外，当前“先增加计数，再初始化”的方式无法保证多个实例并发创建时，第二个实例一定等到第一个实例初始化完成。

建议引入独立的全局运行时管理器，以互斥量保护：

- 初始化状态；
- 活跃实例数；
- 初始化失败回滚；
- 每个实例是否真正持有初始化引用。

同时应检查 `avformat_network_init()` 的返回值。

### P0：播放状态存在多处数据竞争

以下字段由 UI、读取线程、播放循环或 SDL 音频回调并发访问，但仍是普通变量：

- `SessionState::abort_request`：`play_core/video_state.h:13`
- `SessionState::paused` 和 seek 字段：`play_core/video_state.h:15`
- `AudioState::audio_volume`：`play_core/video_state.h:73`
- `VideoCtl::m_loopPolicy`：`play_core/videoctl.h:179`
- `audio_callback_time`：`play_core/videoctl.cpp:33`

例如，音量在控制线程中写入，但 SDL 回调不持有 `m_streamMutex`：

- 写入：`play_core/videoctl.cpp:2157`
- 读取：`play_core/videoctl.cpp:113`

`PacketQueue` 的成员函数内部虽然使用 SDL mutex，但调用方会直接无锁读取公开的 `abort_request`、`serial`、`size` 和 `nb_packets`，因此仍存在数据竞争。

建议不要简单地把所有字段全部改成原子变量，而是明确线程所有权：

- 简单标志和标量可使用 `std::atomic`；
- seek 等多字段命令应封装为受同一互斥量保护的整体状态；
- `PacketQueue` 字段改为私有，通过加锁的查询接口访问；
- 切换音轨、字幕轨时，需要与读取和刷新线程建立真正的同步协议。

## 明确的用户可见问题

### P1：切换媒体后音量会逐步漂移

`startup_volume` 同时表示“百分比”和“SDL 音量值”。

构造时它是百分比：

```cpp
startup_volume(30)
```

打开媒体时被原地转换为 SDL 音量：

- `play_core/videoctl.cpp:1918`
- `play_core/videoctl.cpp:1923`

下一次打开媒体，又把转换后的 SDL 音量当成百分比再次转换。例如在 `SDL_MIX_MAXVOLUME == 128` 时：

```text
30% → 38
38% → 48
48% → 61
61% → 78
```

因此反复切换媒体后音量会不断增大。

`OnPlayVolume()` 也直接保存 SDL 标量：

- `play_core/videoctl.cpp:2157`

建议只保留一种稳定单位，例如始终保存 `[0.0, 1.0]` 的归一化音量，传给 SDL 前再临时转换并裁剪。

## 性能优化

### P1：每帧都进行大块分配、BGRA 转换和上传

当前每一帧都会：

1. 创建新的 `VideoFrame`；
2. 重新分配 `std::vector`；
3. 使用 `sws_scale` 转成 BGRA；
4. 再通过 `SDL_UpdateTexture` 上传。

相关位置：

- `play_core/videoctl.cpp:2233`
- `play_core/videoctl.cpp:2237`
- `play_core/videoctl.cpp:2239`
- `apps/qt_player/show.cpp:222`

一帧 1080p BGRA 约为 7.9 MiB，60 FPS 时仅帧缓冲写入就接近 475 MiB/s，还没有计算转换和纹理上传成本。

建议按投入从低到高处理：

1. 使用 2～3 个可复用帧缓冲，避免每帧重新分配。
2. 缓存 `VideoFrame` 容量，只有分辨率变化时扩容。
3. 支持 YUV 帧并使用 `SDL_UpdateYUVTexture`，避免不必要的 BGRA 转换。
4. 长期可以让渲染后端消费引用计数的 `AVFrame`，按后端需要转换。

### P2：调整窗口尺寸时反复销毁 SDL 渲染器

每次 `resizeEvent` 都会销毁 renderer、window 和 texture，然后立即重新创建：

- `apps/qt_player/show.cpp:239`
- `apps/qt_player/show.cpp:244`
- `apps/qt_player/show.cpp:246`

拖动窗口时可能连续触发大量 GPU 资源重建。通常只需要更新目标区域并重新渲染；只有原生窗口句柄变化时才需要重建 SDL window/renderer。

### P2：核心文件职责过重

当前主要热点：

- `play_core/videoctl.cpp`：2499 行
- `apps/qt_player/mainwid.cpp`：1012 行

尤其 `VideoCtl` 同时负责：

- FFmpeg 输入；
- 解码线程；
- SDL 音频；
- 音视频同步；
- 网络重连；
- 播放控制；
- 帧转换和派发；
- 全局依赖初始化。

建议在修复并发问题后逐步拆分，而不是立即大规模重写：

- `PlaybackLifecycle`
- `StreamReader`
- `AudioOutput`
- `TrackController`
- `ReconnectController`
- `VideoFrameConverter`

## 构建与工程化

### P1：声明的最低 CMake 版本无法使用 Preset

顶层要求 CMake 3.20：

- `CMakeLists.txt:1`
- `README.md:25`

但 `CMakePresets.json` 使用 schema version 6：

- `CMakePresets.json:2`

version 6 需要 CMake 3.25。实际使用 CMake 3.22.1 执行 `cmake --list-presets` 时已经失败：

```text
CMake Error: Unrecognized "version" field
```

应选择其一：

- 将最低 CMake 版本提升到 3.25；或
- 将 preset schema 降到旧版本，避免使用新版字段。

### P1：无法只构建播放核心

顶层 CMake 无条件查找 Qt，并始终构建应用及所有测试：

- `CMakeLists.txt:41`

这会让无 UI 的核心开发、CI 和跨平台验证变得困难。

建议增加：

```cmake
option(MYPLAYER_BUILD_QT_APP "Build Qt application" ON)
include(CTest)
```

并使用标准的 `BUILD_TESTING` 控制测试目标。这样可以单独构建和测试 `play_core`。

### P2：依赖和打包配置不完全可复现

`vcpkg.json` 没有固定 `builtin-baseline`，不同 vcpkg checkout 可能解析到不同版本。

打包脚本还存在独立的硬编码：

- 默认版本固定为 `1.0.0`：`scripts/package-portable.ps1:4`
- SoundTouch 路径固定为 `soundtouch-2.3.3`：`scripts/package-portable.ps1:148`
- 没有读取单独配置的 `PLAYERDEMO_FFMPEG_ROOT`、`PLAYERDEMO_SDL2_ROOT` 和 `PLAYERDEMO_SOUNDTOUCH_ROOT`

这与 CMake 对可覆盖依赖路径的承诺不一致。建议让打包流程读取 CMake install 结果或生成的部署清单，避免维护两套路径和版本信息。

### P2：仓库和本地构建体积较大

仓库直接跟踪约 179 MB 的第三方依赖，Git pack 大约 157 MB，其中主要文件包括：

- `avcodec-61.dll`：85 MB
- `avfilter-10.dll`：40 MB
- `avformat-61.dll`：18 MB

如果离线构建不是硬要求，可以考虑：

- vcpkg 管理全部依赖；
- Git LFS；
- 将预编译运行库放到 Release 附件；
- 仓库仅保留版本清单和下载校验值。

当前工作区约 4.7 GB，主要是可再生成内容：

| 目录 | 大小 |
|---|---:|
| `.vs` | 3.1 GB |
| `build` | 788 MB |
| `bin` | 270 MB |
| `dist` | 259 MB |

不再需要历史构建结果时，清理这些目录可以立即回收约 4.4 GB。建议在 `.gitignore` 中显式补充 `dist/`，让意图更清晰。

## 测试与质量保障

当前 CMake 实际注册了 4 个测试，但 `docs/PROJECT_GUIDE.md:113` 仍写 3 个，文档已经发生漂移。

另外：

- `show_widget_tests` 并不创建 Qt Widget，而是读取源文件并匹配字符串：`tests/show_widget_tests.cpp:27`
- 没有真实媒体解码测试；
- 没有停止/重连并发测试；
- 没有验证连续切换媒体时的音量；
- 没有发现 GitHub/Gitee CI 配置；
- 没有统一启用 `/W4`、`-Wall`、clang-tidy 或 sanitizer。

建议优先增加以下回归测试：

1. 网络重连期间调用 `stopAndWait()` 和析构。
2. 连续打开多个媒体后音量保持不变。
3. 快速 seek、暂停和停止压力测试。
4. 音轨/字幕轨切换压力测试。
5. 使用小型本地媒体 fixture 做实际解封装和解码。
6. 在支持的平台运行 AddressSanitizer 和 ThreadSanitizer。

## 建议实施顺序

1. 修复析构、停止和重连生命周期。
2. 重构 SDL/FFmpeg 全局初始化管理。
3. 明确共享状态的线程同步模型。
4. 修复音量单位漂移并补充回归测试。
5. 引入帧缓冲复用，优化 BGRA 转换和纹理上传。
6. 拆分 `VideoCtl` 和 `MainWid`。
7. 改进 CMake 组件开关、CI、依赖锁定和打包流程。

## 本次验证

- 未修改任何项目文件。
- Git 工作区当前干净。
- 编辑器诊断：0 个错误、0 个警告。
- 手动编译运行 `tests/show_widget_tests.cpp`：通过。
- 完整构建和 CTest 未运行；当前 CMake 3.22.1 无法解析 schema version 6 的 `CMakePresets.json`，这也确认了最低版本声明不一致的问题。
