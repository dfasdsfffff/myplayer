# PlayerSDK 1.0.0 源码接入指南

`play_core` 本身是 Core SDK（静态库），没有额外的核心包装库。公开 C++ 接口为 `core/include/play_core/`，Qt 接口为 `qt/include/player_sdk_qt/`；新增 C ABI 接口为 `c/include/player_sdk_c/`，使用与兼容约定见 [C ABI 指南](c/README.md)。`src/`、`PlaybackRuntime`、`VideoCtl` 和 controller 均是私有实现/回归测试接口，不属于源码兼容承诺。

## 源码目录

本目录是独立 SDK 根目录，可直接作为宿主的 CMake 子项目。`core/` 是核心源码，`c/` 是 C ABI 适配层，`qt/` 是 Qt 组件，`examples/` 是使用示例，`tests/api/` 是公开接口测试，`tests/consumer/` 是外部接入测试，`third_party/sigslot/` 是随包依赖。公开 include 和链接目标名不变。仓库根目录仍用于构建两个应用及私有回归测试。

## 1. 依赖与构建

首版目标是 Windows x64、C++20、CMake 3.25+、MSVC；可选组件要求 Qt Widgets 6.8+。Core 与 Qt 仍是源码 API，不提供稳定 C++ ABI。新增的 `PlayerSDK::C` 共享库以版本化 C 接口提供独立二进制边界，默认构建，可通过 `PLAYER_SDK_BUILD_C=OFF` 关闭。

当前开发环境依赖：FFmpeg 9.0.1（avcodec/avdevice/avfilter/avformat/avutil/swresample/swscale）、SDL 2.32.10、SoundTouch 2.4.1 的 SoundTouchDLL、sigslot 1.2.3、Qt 6.9.3、libass 0.17.5。实际构建验证记录见文末；最低版本之外的组合不自动视为验证通过。

- 随包提供：SDK 源码、CMake、sigslot 1.2.3 源码及 LICENSE、示例和公开 API 测试。
- 宿主提供：FFmpeg、SDL2、SoundTouch 二进制/开发文件；启用 Qt 时另提供 Qt 和 libass，以及其字体/文字处理传递依赖。
- 不包含依赖 DLL，不自动下载、安装依赖或修改工具链。可以复用兼容的 `SDL2::SDL2`、`SoundTouch::SoundTouchDLL` targets；FFmpeg 使用 `FFMPEG_INCLUDE_DIRS`、`FFMPEG_LIBRARIES`（如未预先查找则调用 `find_package(FFMPEG)`）。libass 通过 pkg-config 查找。
- 仓库根目录的 `vcpkg.json` 记录应用开发依赖。外部宿主可在自己的 vcpkg manifest 声明 `ffmpeg`（含 avdevice、avfilter、swscale、swresample）、`sdl2`、`soundtouch[soundtouchdll]`；Qt 组件再增加 `libass`。用宿主工具链在首次 CMake configure 时选定依赖；不要在已配置的构建目录中更换 triplet/CRT。
- sigslot 可通过 `PLAYERDEMO_SIGSLOT_ROOT` 指定兼容的自备副本。

### 无 Qt 宿主

```cmake
cmake_minimum_required(VERSION 3.25)
project(host LANGUAGES CXX)
add_subdirectory(third_party/PlayerSDK)
add_executable(host main.cpp)
target_link_libraries(host PRIVATE PlayerSDK::Core)
```

```cpp
#include <play_core/player.h>
play_core::Player player;
auto subscription = player.subscribe([](const play_core::PlayerEvent& e) {
    // 回调运行在实例的派发线程，不是 UI/解码线程。
    // 若需保留视频：保存 e.video 的 shared_ptr<const VideoFrame>。
});
auto opened = player.open({"D:/videos/示例.mp4"});
if (opened) player.play(); // 允许在 open 完成前排队 play
```

正常退出先 `close()` 并监听 `Operation`/`Closed`，然后在非回调线程注销、析构。完整帧消费和错误处理示例见 `examples/core/main.cpp`。

`VideoFrame::bgra` 是 `shared_ptr<const vector<uint8_t>>`（YUV 帧为空），YUV 平面由 `VideoPlane::data` 保持引用。像素在帧存活期间保持有效；手动构造 BGRA 帧时先填充独立的 vector，再以共享指针提交，提交后不要修改其内容。C 接口仍提供事件存活期间有效的只读像素指针。

### Qt Widgets 宿主

```cmake
set(PLAYER_SDK_BUILD_QT ON CACHE BOOL "Qt integration")
add_subdirectory(third_party/PlayerSDK)
target_link_libraries(my_qt_app PRIVATE PlayerSDK::QtWidgets)
```

```cpp
#include <player_sdk_qt/qt_player.h>
#include <player_sdk_qt/video_widget.h>
auto* player = new player_sdk_qt::QtPlayer(parent);
auto* view = new player_sdk_qt::VideoWidget(parent);
view->setPlayer(player);
layout->addWidget(view);
player->open(QUrl::fromLocalFile(filePath));
player->play();
```

`VideoWidget` 使用 SDL2 渲染到 Qt 管理的原生子窗口，优先硬件渲染器、失败时尝试 SDL 软件渲染器。普通 YUV420P 直接上传 IYUV 纹理，BGRA 直接上传 BGRA 纹理；仅 SDL2 无对应色彩转换模式的 BT.2020 YUV 路径使用 swscale 转 BGRA。画面、位图和 libass 字幕均由 SDL 合成，QPainter 只绘制无视频时的背景，不承担视频显示。`grabVideoFrame()` 显式通过 SDL 读回包含字幕的画面，正常播放不读回、不额外生成 QImage。offscreen/minimal 自动测试使用 SDL surface/software renderer，不将此结果冒充硬件渲染验证。

`QtPlayer` 所有方法仅能在 QObject 所在线程调用。所有 Qt 信号在该线程交付。Core 可跨线程提交命令的许可不适用于 QObject。一个 player 仅绑定一个 view；绑定新 view 会自动解绑旧 view。先销毁 view 或先销毁 player 均会安全解绑。`VideoWidget` 也支持手工提交公开 CPU 帧；字幕支持嵌入 ASS/文本/位图以及外部 ASS/SSA/SRT。

### 构建选项

| 选项 | 默认 | 含义 |
| --- | --- | --- |
| `PLAYER_SDK_BUILD_QT` | OFF | 可复用 Qt 组件 |
| `PLAYER_SDK_BUILD_EXAMPLES` | OFF | C++/可选 Qt 示例 |
| `PLAYER_SDK_BUILD_TESTS` | OFF | 仅公开 SDK 验收测试 |
| `MYPLAYER_BUILD_QT_APP` | OFF（仓库） | 原播放器；开发 presets 显式开启 |
| `MYPLAYER_BUILD_MONITOR` | OFF（仓库） | 原监控应用 |
| `BUILD_TESTING` | OFF（仓库） | 仓库私有白盒/应用回归，嵌入时不参与 |

作为子项目时，即使宿主 `BUILD_TESTING=ON`，也不引入 SDK 测试。SDK 不改变宿主 `BUILD_SHARED_LIBS`、输出目录、CRT、全局 C++ 标准/警告设置或安装前缀；只通过 Core target 要求 consumer 支持 C++20。静态库依赖以 CMake link-only 方式正确传递，第三方头文件和私有目录不传播给 Core consumer。Qt 的自动代码生成限定于 Qt 子目录。

## 2. 请求、状态与生命周期契约

### 请求与异常

- 每次成功受理返回非零 `OperationId` 和关联 `SessionId`。同步返回只表示受理；结果通过一次 `Operation` 事件给出 `Succeeded/Failed/Cancelled/Superseded`。
- 同步拒绝时 `operation=0` 且 `error` 描述原因，不产生终结事件。音量 `[0,1]`，倍速 `[0.1,2]`，毫秒定位非负。重连最多配置 32 次。
- 每实例最多 128 个尚未完成交付的请求、32 个订阅。慢回调或 Qt UI 卡顿时会同步拒绝超额请求；不要无限重试。帧、位置、字幕采用最新值邮箱，允许覆盖；操作结果不覆盖。
- `open` 复制输入并为其分配新 session；A→B→C 的旧请求仍携旧 session 结算，但旧画面/状态/错误不更新 C。新媒体在关闭旧工作线程后建立。
- 预期参数/播放错误不抛异常。构造、订阅与请求的数据分配可能抛 `std::bad_alloc`/标准库异常；构造初始化失败抛 `std::runtime_error`，订阅数超限抛 `std::length_error`。宿主回调异常被隔离，不逃逸到工作线程。工作线程有异常边界；不承诺进程资源彻底耗尽后仍能成功分配错误事件。
- `Player` 不可复制、不可移动。析构不抛异常，但会等待核心线程及在途回调，不能与其他 API 调用并发。

### 状态与合法命令

| 当前状态 | 合法媒体命令 | 说明 |
| --- | --- | --- |
| Closed | open、close | 重复 close 成功且产生自己的结果 |
| Opening | play/pause/seek/stop/轨道命令排队；open/close 可取消旧任务 | 打开完成后执行排队命令，不把 open 当成播放 |
| Ready | play、pause、seek、stop、轨道、open、close | 媒体已打开但保持暂停，不输出声音 |
| Playing / Paused / Stopped | play、pause、seek、stop、轨道、open、close | stop 不销毁解封装器、解码器和源会话 |
| Reconnecting | open、close；其他请求可能异步失败 | close 取消退避与阻塞 I/O；重连后恢复先前暂停/播放意图 |
| Ended / Failed | open、close | 自然结束后再次播放需要 open；应用播放列表/单曲循环由应用实现 |
| Closing | 已提交请求顺序排队 | close 完成后可重新 open |

`setVolume/setPlaybackRate/setHardwareDecode` 可无媒体调用。硬解偏好作用于下次打开；硬解仅 Windows D3D11VA，下载到 CPU，不是零拷贝。

- `stop()` 仅暂停推进并保留会话；随后 `play()` 对可定位媒体先定位到 0，再继续；不可定位直播恢复保留的会话（可能存在有界缓存延迟），不承诺回到直播边缘。释放资源应调用 `close()`。
- `seek()` 成功指解封装 `avformat_seek_file` 返回成功，`actualPositionMs` 是成功提交到解封装器的目标，不是目标附近首帧 PTS。请求与实际字段独立；实际解码关键帧可能更早。并发定位碰到已有解封装定位时可异步失败（busy），不伪报成功。时间来自核心毫秒时钟，不由整数秒乘以 1000。
- 音轨/字幕轨道 ID 使用 `MediaInfo::tracks[].streamIndex`，不是 UI 列表下标；`nullopt` 关闭字幕。轨道成功代表刷新线程已应用并成功打开对应解码器，不承诺已输出第一帧/采样。
- 未知时长/帧时间戳使用 `optional`；帧包含 CPU 格式、plane、stride、宽高比、旋转、颜色及 PTS。帧发布后不修改；拥有帧即可在播放器关闭或析构后继续读取。长时间保存帧占用内存由宿主承担。
- `MediaInfo` 是只读快照。借用 `PlayerEvent&` 仅在回调期间有效，需要保留时复制事件或保存其共享对象。
- UTF-8 本地文件路径直接传 native path，不传 `file://`；网络输入传 `scheme://` URL。Qt 包装接受 `QUrl`，自动转换本地路径。
- SDK 自身公开诊断不包含原始带凭据 URL；宿主直接使用 FFmpeg 或启用其诊断时，应同时审计第三方日志策略，不安装覆盖宿主的全局 FFmpeg 日志回调。

### 注销、关闭与回调重入

每实例有独立串行控制线程和独立串行事件线程，宿主回调不在解码/读取/刷新线程运行。回调中可提交异步控制（包括 close），不可在回调中销毁 Player、同步等待 close 或注销自身订阅。

`Subscription::reset()` 与析构会阻止新调用、取消该订阅排队交付并等待在途回调完成。返回后不会再访问该订阅捕获的宿主上下文。慢回调可延迟通知和 Player 析构，但不阻止核心线程取消网络/解码工作。主动注销或销毁后，不再承诺向该订阅交付剩余结果。宿主回调必须最终返回。

## 3. 部署、发行和迁移

Core 静态不代表依赖都静态。最终程序需要对应配置的 SDL、SoundTouch、FFmpeg DLL。Qt 程序还要部署 Qt Core/Gui/Widgets、`platforms/qwindows[d].dll`、libass 及字体/文字处理依赖。无界面 Qt 测试配置 `QT_QPA_PLATFORM=offscreen` 时，必须另外部署 `qoffscreen[d].dll`；`windeployqt` 默认可能只拷贝 windows 插件。

宿主负责自己的输出目录和部署。Windows 下可用 `windeployqt` 部署 Qt，用 vcpkg app-local 或 `$<TARGET_RUNTIME_DLLS:host>` 部署导入 targets 对应 DLL，并补齐第三方传递依赖。不同架构、CRT、Debug/Release 或同名不同版本 DLL 不可混用。

从仓库根目录生成仅包含 SDK 的源码归档（不包含 apps、build、开发机 DLL；脚本位于 SDK 目录之外）：

```powershell
cmake -DOUTPUT_DIR=build/sdk-source-1.0.0 -P scripts/package-sdk.cmake
```

输出目录必须不存在，脚本不覆盖已有发行物。归档直接使用 `sdk/CMakeLists.txt`，单独解压后可以独立或作为子项目构建。许可证见 `licenses/README.md`。外部 FFmpeg/Qt 等依赖的授权文本由最终发行方按实际版本补齐，不将本源码包当成最终商业授权审计报告。

### 从旧私有入口迁移

- `PlaybackRuntime::Create()/controller()` → `play_core::Player` 或 `player_sdk_qt::QtPlayer`。
- `controller.play(source)` 的打开+播放 → `open(source); play();`。
- toggle pause → `setPaused(bool)`；整数秒定位 → 64 位毫秒 `seek(ms)`。
- 原停止并释放 → `close()`；新 `stop()` 保留媒体会话。
- 原生 sigslot → `Subscription` 或 Qt 信号；所有帧为 `shared_ptr<const VideoFrame>`。
- FFmpeg 枚举/宽高比 → `MediaType`、`Rational`、`ColorSpace/ColorRange`。
- 播放列表、历史、设置、下一首/随机播放留在应用；两个应用的画面与字幕统一使用 `VideoWidget`。

源码版本政策：1.x 在稳定发布后尽量保持公开源码接口兼容；破坏变更升主版本并给迁移说明。升级后必须重编 SDK 及宿主。不承诺旧二进制兼容；没有 C ABI、`PlayerSDK::Cpp` 或独立 `player_sdk` Core 包装库。

## 4. 验证方式与边界

- `tests/consumer` 是可复制到仓库外的宿主：检查构建隔离与公开 include，构建并运行 close 示例。传 `PLAYER_SDK_SOURCE_DIR` 指向仓库的 `sdk/` 或解压后的 `PlayerSDK/`。
- `PLAYER_SDK_BUILD_TESTS=ON`：公开 API 生命周期、毫秒定位、会话替代、多实例、保留帧、UTF-8 路径、慢回调容量和回调异常；启用 Qt 后另测真实画面提交、UI 卡顿、字幕及销毁顺序。
- 仓库完整测试继续覆盖解码、同步、网络超时/重连/取消、字幕、播放列表和监控录制；网络集成额外覆盖 SDK 打开中与重连中关闭。
- 公开接口第一版不宣称跨平台、GPU 零拷贝、QML、独立 DLL 分发、无限实例或已完成长时性能验证。

具体 Debug/Release、外部宿主及源码归档验证结果见 `../docs/PLAYER_SDK_PLAN.md` 的实施记录（仓库文档，不随最小 SDK 包交付）。真实摄像机、长时间播放、HDR、完整媒体格式矩阵、干净机器及法律授权审计不能由本机短时自动测试替代。
