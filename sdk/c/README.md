# PlayerSDK C ABI v1

C ABI 是现有 `play_core` 的独立共享库适配层，不依赖 Qt；原 C++ 和 Qt 接口不变。
公开头文件为 `include/player_sdk_c/player.h`，CMake 目标为 `PlayerSDK::C`，Windows 产物为
`player_sdk_c.dll` 和导入库 `player_sdk_c.lib`。内部实现仍用 C++20，但 C 调用方不需要 C++ 类型或头文件。

## 构建与链接

默认 `PLAYER_SDK_BUILD_C=ON`；只需要原 C++ SDK 时可关闭。
沿用 SDK 的 FFmpeg、SDL2、SoundTouch 和 sigslot 依赖，详见上级 README。

```cmake
cmake_minimum_required(VERSION 3.25)
project(c_host LANGUAGES C CXX) # CXX 用于编译 SDK 内部实现
set(PLAYER_SDK_BUILD_C ON CACHE BOOL "Build C ABI")
add_subdirectory(third_party/PlayerSDK)
add_executable(c_host main.c)
target_link_libraries(c_host PRIVATE PlayerSDK::C)
```

仅使用已编译的 Windows SDK 时，调用方只需 C 编译器、该头文件和导入库，不需要构建内部 C++。
部署需携带 `player_sdk_c.dll` 及其匹配的 FFmpeg、SDL2、SoundTouch 等运行时依赖；不能只复制主 DLL。
Windows x64/MSVC Release 可安装独立二进制 SDK。在已配置的构建目录执行：

```sh
cmake --build build --config Release --target player_sdk_c
cmake --install build --config Release --component PlayerSDK --prefix dist/PlayerSDK
```

安装目录包含 `bin/` 下的 DLL 和可解析的非系统依赖、`lib/` 下的导入库、
`include/` 下的公开头文件，以及 `examples/` 下的纯 C 冒烟示例。
CMake 宿主可通过 `-DCMAKE_PREFIX_PATH=<安装目录>` 使用
`find_package(PlayerSDK CONFIG REQUIRED)` 和 `target_link_libraries(app PRIVATE PlayerSDK::C)`。
部署时将 `bin/` 的 DLL 放在应用程序旁，并安装与 SDK 构建工具链兼容的 x64 MSVC Redistributable。
依赖版本发生变化时，请一起替换运行库。关闭程序后再更新 DLL，然后重新启动。
独立构建可在 `sdk/` 作为根目录时启用 `PLAYER_SDK_BUILD_TESTS=ON`，执行
`ctest --test-dir <build-dir> -C Debug -R player_sdk_c --output-on-failure`。
`tests/api/player_c_smoke.c` 由真正的 C 编译器编译，不是以 C++ 模式模拟 C 消费者。

## 最小使用示例

```c
#include <player_sdk_c/player.h>
#include <stdio.h>

static void PS_CALL on_event(void* user_data, const ps_event* event)
{
    ps_event_info_v1 info;
    (void)user_data;
    if (ps_event_get_info_v1(event, &info) != PS_OK) return;
    if (info.type == PS_EVENT_OPERATION) {
        printf("operation=%llu status=%d error=%s\n",
            (unsigned long long)info.operation,
            (int)info.operation_status, info.operation_error.message);
    }
    if (info.type == PS_EVENT_VIDEO) {
        ps_video_frame_v1 frame;
        if (ps_event_get_video_v1(event, &frame) == PS_OK) {
            /* 在此使用 BGRA 或 YUV420P 平面；不需要释放平面指针。
               若交给其他线程异步渲染，应先 ps_event_retain。 */
        }
    }
}

int main(void)
{
    ps_player_handle player = NULL;
    ps_error_v1 error;
    ps_source_v1 source;
    ps_request_v1 request;
    if (ps_abi_version() != PS_ABI_VERSION) return 1;
    if (ps_player_create_v1(NULL, on_event, NULL, &player, &error) != PS_OK) {
        fprintf(stderr, "%s\n", error.message);
        return 1;
    }
    ps_source_init_v1(&source);
    source.location = "D:/videos/example.mp4";
    if (ps_player_open_v1(player, &source, &request) == PS_OK) {
        /* 允许 open 完成前排队 play；两者结果分别由 Operation 事件通知。 */
        ps_player_play_v1(player, &request);
        puts("Press Enter to stop");
        getchar(); /* 仅此控制台示例等待；GUI 使用自己的事件循环。 */
    } else {
        fprintf(stderr, "%s\n", request.error.message);
    }
    /* destroy 等待工作线程/回调结束，不需要在回调内销毁。
       若需要显式 close 的终结通知，先 close 并等待对应 Operation/Closed。 */
    return ps_player_destroy(player) == PS_OK ? 0 : 1;
}
```

## 错误与异步语义

- 所有可失败函数返回 `PS_OK` 或 `PS_ERROR_*`；输出请求必须提供。
- `open/play/seek/...` 返回 `PS_OK` 只表示请求已受理，不表示播放操作已完成。
- 受理后用 `operation` 与 `session` 对应 Operation 事件的 `operation` / `operation_session`。
  操作可能成功、失败、取消或被替代；异步错误在 `operation_error`，独立 Error 事件在 `error`。
  事件可能早于提交函数返回到调用方，调用方不能依赖“先返回、后回调”的顺序。
- 同步拒绝时 `operation=0`，详情在 `ps_request_v1.error`。创建失败的错误由可选的 `ps_error_v1*` 返回。
- 错误消息为内联 UTF-8 缓冲，最多 511 字节，必要时在码点边界截断；无跨 DLL 释放问题。
- 缺少相应载荷的事件 getter 返回 `PS_ERROR_INVALID_STATE`；索引越界返回 `PS_ERROR_INVALID_ARGUMENT`。
- C++ 异常在 ABI 内部捕获；非法指针、悬空句柄等调用方内存错误不属于可恢复参数错误。

## 生命周期与线程

- 每个播放器一个固定回调及 `user_data`；回调在该实例的派发线程串行执行，不在 UI 线程。
  请快速返回，不要在回调中等待本播放器后续事件；不同实例可能并行回调。
- 命令输入（包括 URL、HTTP headers）在返回前复制。输入字符串均为 UTF-8，原生文件路径不是 `file://`。
  网络 timeout/delay 单位见字段名，`analyze_duration_us` 为微秒，`probe_size` 为字节。
- 回调中的 `const ps_event*` 只在回调期间有效；getter 返回的字符串/像素指针借用该事件的存储。
- 需要保留时调用 `ps_event_retain(event, &retained)`，之后通过新句柄读取，并最终
  `ps_event_release(retained)`。帧像素共享不可变存储，不因 retain 再次复制；事件字符串/元数据由 SDK 持有。
  保留的事件可以晚于播放器销毁，但不能晚于 DLL 卸载。
- 禁止释放借用的回调事件、重复释放、用 `free/delete` 释放 SDK 句柄，或修改帧内存。
- 销毁不能与该句柄上的其他调用并发；必须由调用方协调。`destroy` 返回后不会再有该实例回调，届时才能释放 `user_data`。
  在自身回调中 destroy 返回 `PS_ERROR_INVALID_STATE`，不会销毁或死锁。
- 多线程可对同一保留事件只读或 retain，但 release 不得与该句柄的读取/retain 并发。

## 能力边界

覆盖现有 Core 的全部控制方法，以及媒体能力、音轨/字幕轨、BGRA/YUV420P 视频、文本/ASS/位图字幕。
`track_count` 对应 Core 提供的可选择音轨/字幕轨；视频尺寸通过媒体信息提供，不保证视频出现在轨道列表中。
`select_subtitle_track(enabled=0, ...)` 关闭字幕。暂停必须传 0/1，音量 `[0,1]`，倍速 `[0.1,2]`。
硬解选择影响下一次 open。seek 完成位置是解封装目标，不是首个显示帧确认。

此层不导出 QWidget、SDL_Window 或窗口嵌入接口，也不增加新渲染器。C/其他语言消费者使用帧接口自行渲染，
Qt 用户继续使用 `PlayerSDK::QtWidgets`。字幕 getter 提供原始字幕数据，不承诺已经合成到视频像素。

## ABI 兼容约定

- `ps_version()` 是产品版本，`ps_abi_version()` 是 ABI 主版本。兼容更新保留
  `player_sdk_c.dll` 文件名、ABI 主版本和原有导出。首发时冻结 v1 基线。
- Windows MSVC x64 构建会将实际导出表与 `exports-v1.txt` 对照。新增导出时同步更新清单，
  并保留已发布的符号和签名。
- 客户端应忽略未知事件，对未知状态和错误码使用通用处理。兼容更新也需要保留现有
  请求完成、回调线程和对象生命周期的约定。

- ABI 主版本 `PS_ABI_VERSION=1`，使用 `extern "C"` 导出；Windows 明确 `__cdecl`。
- 仅固定宽度整数、double、指针、不透明句柄、C 函数指针，无 STL、Qt 类型、异常、C++ 类或虚表。
- v1 类型、字段顺序、常量值和函数签名冻结，不在原结构体尾部追加字段。
  新布局用 `_v2` 类型/函数，同时保留旧导出；输入 `struct_size` 必须等于对应 v1 类型大小。
  调用方使用 `ps_*_init_v1` 获取完整默认值，不要仅依靠清零。
- 使用平台默认对齐，不可用不同的 `#pragma pack` 或编译器结构体打包选项包含此头文件。
  当前验证目标为 Windows x64/MSVC；同架构、同平台调用约定和布局兼容才属于 ABI 兼容范围。
  不承诺跨操作系统/架构直接使用同一二进制，也不把未测试工具链组合视为验证通过。
- `player_c_layout.h` 固定 x64 结构体尺寸/关键偏移；内部枚举变化触发编译断言，不能静默改变 C 常量含义。
- 原 `PlayerSDK::Core` / `PlayerSDK::QtWidgets` 仍是 C++ 源码 API，不因新增此层而获得二进制兼容承诺。
