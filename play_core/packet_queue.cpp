/*
* @file 	packet_queue.cpp
* @brief 	数据包队列类实现
* @note 	从 datactl.h 拆分，由 C 结构体+自由函数 封装为 class
*/

#include "packet_queue.h"

//数据包队列存放数据包(供队列内部使用)
PacketQueue::~PacketQueue()
{
	destroy();
}

int PacketQueue::put_private(AVPacket* pkt)
{
	MyAVPacketList pkt1;
	int ret;

	if (abort_request)
		return -1;

	pkt1.pkt = pkt;
	pkt1.serial = serial;

	ret = av_fifo_write(pkt_list, &pkt1, 1);
	if (ret < 0)
		return ret;
	nb_packets++;
	size += pkt1.pkt->size + sizeof(pkt1);
	duration += pkt1.pkt->duration;
	SDL_CondSignal(cond);
	return 0;
}

//数据包队列存放数据包
int PacketQueue::put(AVPacket* pkt)
{
	AVPacket* pkt1;
	int ret;

	pkt1 = av_packet_alloc();
	if (!pkt1) {
		av_packet_unref(pkt);
		return -1;
	}
	av_packet_move_ref(pkt1, pkt);

	SDL_LockMutex(mutex);
	ret = put_private(pkt1);
	SDL_UnlockMutex(mutex);

	if (ret < 0)
		av_packet_free(&pkt1);

	return ret;
}

//数据包队列存放空数据包
int PacketQueue::put_nullpacket(AVPacket* pkt, int stream_index)
{
	pkt->stream_index = stream_index;
	return put(pkt);
}

//数据包队列初始化
int PacketQueue::init()
{
	pkt_list = av_fifo_alloc2(1, sizeof(MyAVPacketList), AV_FIFO_FLAG_AUTO_GROW);
	if (!pkt_list)
		return AVERROR(ENOMEM);
	mutex = SDL_CreateMutex();
	if (!mutex) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		av_fifo_freep2(&pkt_list);
		return AVERROR(ENOMEM);
	}
	cond = SDL_CreateCond();
	if (!cond) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		SDL_DestroyMutex(mutex);
		mutex = nullptr;
		av_fifo_freep2(&pkt_list);
		return AVERROR(ENOMEM);
	}
	abort_request = 1;
	nb_packets = 0;
	size = 0;
	duration = 0;
	serial = 0;
	return 0;
}

//增加序列号
void PacketQueue::add_serial()
{
	SDL_LockMutex(mutex);
	serial++;
	SDL_UnlockMutex(mutex);
}

//数据包队列清空
void PacketQueue::flush()
{
	MyAVPacketList pkt1;

	if (!pkt_list || !mutex)
		return;

	SDL_LockMutex(mutex);
	while (av_fifo_read(pkt_list, &pkt1, 1) >= 0)
		av_packet_free(&pkt1.pkt);
	nb_packets = 0;
	size = 0;
	duration = 0;
	serial++;
	SDL_UnlockMutex(mutex);
}

//数据包队列销毁
void PacketQueue::destroy()
{
	flush();
	if (pkt_list)
		av_fifo_freep2(&pkt_list);
	if (mutex) {
		SDL_DestroyMutex(mutex);
		mutex = nullptr;
	}
	if (cond) {
		SDL_DestroyCond(cond);
		cond = nullptr;
	}
}

//数据包队列停用
void PacketQueue::abort()
{
	if (!mutex)
		return;

	SDL_LockMutex(mutex);
	abort_request = 1;
	if (cond)
		SDL_CondSignal(cond);
	SDL_UnlockMutex(mutex);
}

//数据包队列开始使用
void PacketQueue::start()
{
	if (!mutex)
		return;

	SDL_LockMutex(mutex);
	abort_request = 0;
	serial++;
	SDL_UnlockMutex(mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
//从数据包队列中获取数据包
int PacketQueue::get(AVPacket* pkt, int block, int* serial)
{
	MyAVPacketList pkt1;
	int ret;

	SDL_LockMutex(mutex);

	for (;;) {
		if (abort_request) {
			ret = -1;
			break;
		}

		if (av_fifo_read(pkt_list, &pkt1, 1) >= 0) {
			nb_packets--;
			size -= pkt1.pkt->size + sizeof(pkt1);
			duration -= pkt1.pkt->duration;
			av_packet_move_ref(pkt, pkt1.pkt);
			if (serial)
				*serial = pkt1.serial;
			av_packet_free(&pkt1.pkt);
			ret = 1;
			break;
		}
		else if (!block) {
			ret = 0;
			break;
		}
		else {
			SDL_CondWait(cond, mutex);
		}
	}
	SDL_UnlockMutex(mutex);
	return ret;
}
