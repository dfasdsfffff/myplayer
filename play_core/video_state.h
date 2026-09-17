#pragma once

#include <atomic>
#include <mutex>

#include "av_types.h"
#include "decoder.h"
#include "network_input.h"
#include "soundtouch_wrap.h"

struct SessionState {
    ~SessionState();

    std::thread read_tid;
    AVInputFormat* iformat = nullptr;
    // 由读取线程、播放循环和 UI 控制线程并发访问，使用原子变量消除数据竞争
    std::atomic<int> abort_request{0};
    int force_refresh = 0;
    std::atomic<int> paused{0};
    int last_paused = 0;
    int queue_attachments_req = 0;
    // seek 命令由控制线程写入、读取线程消费，必须作为一个整体读取。
    std::mutex seek_mutex;
    bool seek_req = false;
    int seek_flags = 0;
    int64_t seek_pos = 0;
    int64_t seek_rel = 0;
    int read_pause_return = 0;
    AVFormatContext* ic = nullptr;
    int realtime = 0;
    int eof = 0;
    char* filename = nullptr;
    int last_video_stream = 0;
    int last_audio_stream = 0;
    int last_subtitle_stream = 0;
    SDL_mutex* read_wait_mutex = nullptr;
    SDL_cond* continue_read_thread = nullptr;
    MediaSource source;
    MediaInfo mediaInfo;
    IoControl io;
    std::atomic<int> readResult{0};
    std::atomic<PlaybackError> readError{PlaybackError::None};
    bool unlimitedBuffer{false};
};

struct MediaClockState {
    Clock audclk;
    Clock vidclk;
    Clock extclk;
    int av_sync_type = 0;
};

struct AudioState {
    ~AudioState();

    void* soundTouchHandle = nullptr;
    short* audio_new_buf = nullptr;
    unsigned int audio_new_buf_size = 0;
    std::atomic<double> play_rate{1.0};

    FrameQueue sampq;
    Decoder aud_decoder;
    int audio_stream = 0;
    double audio_clock = 0;
    int audio_clock_serial = 0;
    double audio_diff_cum = 0;
    double audio_diff_avg_coef = 0;
    double audio_diff_threshold = 0;
    int audio_diff_avg_count = 0;
    AVStream* audio_st = nullptr;
    PacketQueue audioq;
    int audio_hw_buf_size = 0;
    uint8_t* audio_buf = nullptr;
    uint8_t* audio_buf1 = nullptr;
    unsigned int audio_buf_size = 0;
    unsigned int audio_buf1_size = 0;
    int audio_buf_index = 0;
    int audio_write_buf_size = 0;
    // 控制线程写入、SDL 音频回调读取，使用原子变量消除数据竞争
    std::atomic<int> audio_volume{0};

    AudioParams audio_src{};
    AudioParams audio_filter_src{};
    AudioParams audio_tgt{};
    SwrContext* swr_ctx = nullptr;

    int16_t sample_array[SAMPLE_ARRAY_SIZE]{};
    int sample_array_index = 0;
    int last_i_start = 0;
    int xpos = 0;
    double last_vis_time = 0;
};

struct VideoTrackState {
    FrameQueue pictq;
    Decoder vid_decoder;
    int frame_drops_early = 0;
    int frame_drops_late = 0;
    double frame_timer = 0;
    double frame_last_returned_time = 0;
    double frame_last_filter_delay = 0;
    int video_stream = 0;
    AVStream* video_st = nullptr;
    PacketQueue videoq;
    double max_frame_duration = 0;
    int width = 0;
    int height = 0;
    int xleft = 0;
    int ytop = 0;
    int step = 0;
};

struct SubtitleState {
    ~SubtitleState();

    FrameQueue subpq;
    Decoder sub_decoder;
    int subtitle_stream = 0;
    AVStream* subtitle_st = nullptr;
    PacketQueue subtitleq;
    SwsContext* sub_convert_ctx = nullptr;
};

struct FilterState {
    int vfilter_idx = 0;
    AVFilterContext* in_video_filter = nullptr;
    AVFilterContext* out_video_filter = nullptr;
    AVFilterContext* in_audio_filter = nullptr;
    AVFilterContext* out_audio_filter = nullptr;
    AVFilterGraph* agraph = nullptr;
};

typedef struct VideoState {
    MediaClockState clocks;
    AudioState audio;
    VideoTrackState video;
    SubtitleState subtitle;
    FilterState filters;
    SessionState session;
} VideoState;

inline SessionState::~SessionState()
{
    if (read_tid.joinable()) {
        abort_request = 1;
        io.cancelled.store(true, std::memory_order_release);
        if (continue_read_thread)
            SDL_CondSignal(continue_read_thread);
        read_tid.join();
    }

    av_freep(&filename);

    if (continue_read_thread) {
        SDL_DestroyCond(continue_read_thread);
        continue_read_thread = nullptr;
    }
    if (read_wait_mutex) {
        SDL_DestroyMutex(read_wait_mutex);
        read_wait_mutex = nullptr;
    }

    avformat_close_input(&ic);
}

inline AudioState::~AudioState()
{
    if (soundTouchHandle) {
        soundtouch_destroy(soundTouchHandle);
        soundTouchHandle = nullptr;
    }
    av_freep(&audio_new_buf);
    av_freep(&audio_buf1);

    swr_free(&swr_ctx);

}

inline SubtitleState::~SubtitleState()
{
    sws_freeContext(sub_convert_ctx);
    sub_convert_ctx = nullptr;
}
