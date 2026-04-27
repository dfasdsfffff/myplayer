/*
* @file 	decoder.h
* @brief 	解码器数据结构与操作接口声明
* @note 	从 datactl.h 拆分出的 Decoder 结构体和函数声明
*/

#pragma once

#include "av_types.h"
#include "frame_queue.h"
#include "packet_queue.h"

//解码器，管理数据队列
typedef struct Decoder {
    AVPacket* pkt;
    PacketQueue* queue;
    AVCodecContext* avctx;
    int pkt_serial;
    int finished;
    int packet_pending;
    SDL_cond* empty_queue_cond;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
    std::thread decode_thread;
} Decoder;

// 解码器重排序pts，-1表示自动，0表示不重排序，1表示重排序
extern int decoder_reorder_pts;

//解码器初始化（绑定解码结构体、数据包队列、信号量，初始化pts）
int decoder_init(Decoder* d, AVCodecContext* avctx, PacketQueue* queue, SDL_cond* empty_queue_cond);

//解码一帧数据
int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub);

//解码器销毁
void decoder_destroy(Decoder *d);

void decoder_abort(Decoder* d, FrameQueue* fq);
