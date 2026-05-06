/*
* @file 	packet_queue.h
* @brief 	数据包队列类
* @note 	从 datactl.h 拆分，由 C 结构体+自由函数 封装为 class
*/

#pragma once

#include "av_compat.h"

//数据包列表
typedef struct MyAVPacketList {
	AVPacket* pkt;
	int serial;
} MyAVPacketList;

//数据包队列
class PacketQueue {
public:
	PacketQueue() = default;
	~PacketQueue();
	PacketQueue(const PacketQueue&) = delete;
	PacketQueue& operator=(const PacketQueue&) = delete;

	int init();
	void destroy();
	void start();
	void abort();
	void flush();
	void add_serial();
	int put(AVPacket* pkt);
	int put_nullpacket(AVPacket* pkt, int stream_index);
	int get(AVPacket* pkt, int block, int* serial);

	// 过渡期间保持 public，供 Decoder/FrameQueue 直接访问
	AVFifo* pkt_list = nullptr;
	int nb_packets = 0;
	int size = 0;
	int64_t duration = 0;
	int abort_request = 1;
	int serial = 0;
	SDL_mutex* mutex = nullptr;
	SDL_cond* cond = nullptr;

private:
	int put_private(AVPacket* pkt);
};

// 向后兼容的自由函数包装器（过渡期使用，新代码请用成员函数）
inline int packet_queue_init(PacketQueue* q) { return q->init(); }
inline void packet_queue_destroy(PacketQueue* q) { q->destroy(); }
inline void packet_queue_start(PacketQueue* q) { q->start(); }
inline void packet_queue_abort(PacketQueue* q) { q->abort(); }
inline void packet_queue_flush(PacketQueue* q) { q->flush(); }
inline void packet_queue_add_serial(PacketQueue* q) { q->add_serial(); }
inline int packet_queue_put(PacketQueue* q, AVPacket* pkt) { return q->put(pkt); }
inline int packet_queue_put_nullpacket(PacketQueue* q, AVPacket* pkt, int stream_index) { return q->put_nullpacket(pkt, stream_index); }
inline int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block, int* serial) { return q->get(pkt, block, serial); }
