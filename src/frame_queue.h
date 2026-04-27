/*
* @file 	frame_queue.h
* @brief 	帧队列操作接口声明
* @note 	从 datactl.h 拆分出的 FrameQueue 函数声明
*/

#pragma once

#include "av_types.h"

//帧队列初始化（绑定数据包队列，初始化最大值）
int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last);

//帧队列销毁
void frame_queue_destory(FrameQueue *f);

//帧队列信号
void frame_queue_signal(FrameQueue *f);

Frame* frame_queue_peek(FrameQueue* f);

Frame* frame_queue_peek_next(FrameQueue* f);

Frame* frame_queue_peek_last(FrameQueue* f);

Frame* frame_queue_peek_writable(FrameQueue* f);

Frame* frame_queue_peek_readable(FrameQueue* f);

void frame_queue_push(FrameQueue* f);

void frame_queue_next(FrameQueue* f);

/* return the number of undisplayed frames in the queue */
int frame_queue_nb_remaining(FrameQueue* f);

/* return last shown position */
int64_t frame_queue_last_pos(FrameQueue* f);
