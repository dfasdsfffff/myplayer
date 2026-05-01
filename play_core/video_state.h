/*
* @file 	video_state.h
* @brief 	视频状态数据结构
* @note 	从 datactl.h 拆分出的 VideoState 结构体
*/

#pragma once

#include "av_types.h"
#include "decoder.h"

//视频状态，管理所有的视频信息及数据
typedef struct VideoState {
    void* soundTouchHandle;
    short* audio_new_buf;  /* soundtouch buf */
    unsigned int audio_new_buf_size;
    double play_rate;	/* 播放速度默认1.0 */

    std::thread read_tid; //读取线程
    AVInputFormat *iformat;
    int abort_request; //停止读取标志
    int force_refresh;
    int paused;
    int last_paused;
    int queue_attachments_req;
    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;
    int read_pause_return;
    AVFormatContext *ic;
    int realtime;

    Clock audclk;
    Clock vidclk;
    Clock extclk;

    FrameQueue pictq;
    FrameQueue subpq;
    FrameQueue sampq;

    Decoder aud_decoder;
    Decoder vid_decoder;
    Decoder sub_decoder;
     
    int audio_stream;

    int av_sync_type;

    double audio_clock;    //最新解码并送入音频缓冲区的音频帧的结束时间
    int audio_clock_serial;
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    AVStream *audio_st;
    PacketQueue audioq;
    int audio_hw_buf_size;
	uint8_t* audio_buf;// 如果有重采样，存储重采样前的音频数据；如果有倍速播放则指向audio_new_buf；否则存储解码后的音频数据
	uint8_t* audio_buf1;// 存储重采样后的音频数据
    unsigned int audio_buf_size; /* in bytes */
    unsigned int audio_buf1_size;
    int audio_buf_index; /* in bytes */
    int audio_write_buf_size;
    int audio_volume;

    struct AudioParams audio_src;

    struct AudioParams audio_filter_src;

    struct AudioParams audio_tgt;
    struct SwrContext *swr_ctx;
    int frame_drops_early;
    int frame_drops_late;

    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;
    RDFTContext *rdft;
    int rdft_bits;
    FFTSample *rdft_data;
    int xpos;
    double last_vis_time;

    SDL_Texture* vis_texture;
    SDL_Texture* sub_texture;
    SDL_Texture* vid_texture;

    int subtitle_stream;
    AVStream *subtitle_st;
    PacketQueue subtitleq;

    double frame_timer;
    double frame_last_returned_time;
    double frame_last_filter_delay;
    int video_stream;
    AVStream *video_st;
    PacketQueue videoq;
    double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
    struct SwsContext *img_convert_ctx;
    struct SwsContext *sub_convert_ctx;
    int eof;

    char *filename;
    int width, height, xleft, ytop;
    int step;

    int vfilter_idx;
    AVFilterContext* in_video_filter;  // 视频链中的第一个滤镜
    AVFilterContext* out_video_filter; // 视频链中的最后一个滤镜
    AVFilterContext* in_audio_filter;  // 音频链中的第一个滤镜
    AVFilterContext* out_audio_filter; // 音频链中的最后一个滤镜
    AVFilterGraph* agraph;             // 音频滤镜图

    int last_video_stream, last_audio_stream, last_subtitle_stream;

    SDL_mutex* read_wait_mutex = nullptr;
    SDL_cond *continue_read_thread = nullptr;

} VideoState;
