/*
* @file 	av_types.h
* @brief 	音视频基础数据类型定义
* @note 	从 datactl.h 拆分出的数据结构声明
*/

#pragma once

#include <thread>

#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <assert.h>

#include "av_constants.h"
#include "av_compat.h"
#include "clock.h"
#include "packet_queue.h"
#include "frame_queue.h"

// 队列大小常量统一在 av_constants.h 中定义
// PacketQueue 已移至 packet_queue.h
// Frame/FrameQueue 已移至 frame_queue.h

//音频参数
typedef struct AudioParams {
    int freq;
    AVChannelLayout ch_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

// Clock 已移至 clock.h

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};
