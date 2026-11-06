#pragma once

#include <mutex>

#include "av_compat.h"

/*
 * 全局 SDL/FFmpeg 运行时初始化管理器。
 *
 * 旧实现使用无保护的 std::atomic<int> 引用计数，且“先计数、后初始化”：
 *  1. SDL_Init 失败时在 Init() 内回退一次计数，随后 MakeInstance() 中临时
 *     对象析构又会无条件再减一次，计数会从 0 变成 -1，后续实例可能跳过
 *     SDL_Init 却仍被当作初始化成功；
 *  2. 多个实例并发创建时，无法保证后创建的实例等待前一个实例完成初始化。
 *
 * 本管理器使用互斥量保护初始化状态和活跃引用数，并显式记录每个实例是否
 * 真正持有初始化引用（由 VideoCtl 的成员标志保存）。失败的初始化不会增加
 * 引用计数，后续实例可安全重试。
 */
class RuntimeManager {
public:
    static RuntimeManager& instance()
    {
        static RuntimeManager mgr;
        return mgr;
    }

    // 获取 SDL 初始化引用。失败返回 false 且不改变计数。
    bool acquireSdl()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_sdlCount == 0) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
                av_log(nullptr, AV_LOG_FATAL, "Could not initialize SDL audio subsystem - %s\n", SDL_GetError());
                av_log(nullptr, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
                return false;
            }
        }
        ++m_sdlCount;
        return true;
    }

    void releaseSdl()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_sdlCount <= 0)
            return;
        --m_sdlCount;
        if (m_sdlCount == 0)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    // 获取 FFmpeg 网络初始化引用。失败返回 false 且不改变计数。
    bool acquireNetwork()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_networkCount == 0) {
            if (avformat_network_init() != 0) {
                av_log(nullptr, AV_LOG_ERROR, "Could not initialize FFmpeg network support\n");
                return false;
            }
        }
        ++m_networkCount;
        return true;
    }

    void releaseNetwork()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_networkCount <= 0)
            return;
        --m_networkCount;
        if (m_networkCount == 0)
            avformat_network_deinit();
    }

private:
    RuntimeManager() = default;

    std::mutex m_mutex;
    int m_sdlCount = 0;
    int m_networkCount = 0;
};
