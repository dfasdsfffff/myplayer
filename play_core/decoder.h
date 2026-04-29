/*
* @file 	decoder.h
* @brief 	解码器类
* @note 	从 datactl.h 拆分，由 C 结构体+自由函数 封装为 class
*/

#pragma once

#include "av_compat.h"
#include "frame_queue.h"
#include "packet_queue.h"
#include <thread>

// 解码器重排序pts，-1表示自动，0表示不重排序，1表示重排序
extern int decoder_reorder_pts;

//解码器，管理数据队列
class Decoder {
public:
	int init(AVCodecContext* avctx, PacketQueue* queue, SDL_cond* empty_queue_cond);
	int decode_frame(AVFrame* frame, AVSubtitle* sub);
	void destroy();
	void abort(FrameQueue* fq);

	// 过渡期间保持 public，供 VideoState/VideoCtl 直接访问
	AVPacket* pkt = nullptr;
	PacketQueue* queue = nullptr;
	AVCodecContext* avctx = nullptr;
	int pkt_serial = -1;
	int finished = 0;
	int packet_pending = 0;
	SDL_cond* empty_queue_cond = nullptr;
	int64_t start_pts = 0;
	AVRational start_pts_tb = {0, 0};
	int64_t next_pts = 0;
	AVRational next_pts_tb = {0, 0};
	std::thread decode_thread;
};
