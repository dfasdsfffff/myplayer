/*
* @file 	frame_queue.h
* @brief 	帧队列类
* @note 	从 datactl.h 拆分，由 C 结构体+自由函数 封装为 class
*/

#pragma once

#include "av_compat.h"
#include "av_constants.h"
#include "packet_queue.h"

//解码后的帧
typedef struct Frame {
	AVFrame* frame = nullptr;
	AVSubtitle sub = {};
	int serial = 0;
	double pts = 0.0;           /* presentation timestamp for the frame */
	double duration = 0.0;      /* estimated duration of the frame */
	int64_t pos = 0;            /* byte position of the frame in the input file */
	int width = 0;
	int height = 0;
	int format = 0;
	AVRational sar = {0, 0};
} Frame;

struct FrameQueueSnapshot {
    int remaining{0};
    int serial{0};
    bool aborted{true};
};

//帧队列
class FrameQueue {
public:
	FrameQueue() = default;
	~FrameQueue();
	FrameQueue(const FrameQueue&) = delete;
	FrameQueue& operator=(const FrameQueue&) = delete;

	int init(PacketQueue* pktq, int max_size, int keep_last);
	void destroy();
	void signal();
	Frame* peek();
	Frame* peek_next();
	Frame* peek_last();
	Frame* peek_writable();
	Frame* peek_readable();
	int wait_readable_for(Uint32 timeout_ms);
	void push();
	void next();
	int nb_remaining();
	int64_t last_pos();
	FrameQueueSnapshot snapshot() const;
	bool hasShown() const;
	void lock();
	void unlock();

	// Frame payloads are accessed through peek methods; queue bookkeeping is private.
private:
	Frame queue[FRAME_QUEUE_SIZE];
	int rindex = 0;
	int windex = 0;
	int size = 0;
	int max_size = 0;
	int keep_last = 0;
	int rindex_shown = 0;
	SDL_mutex* mutex = nullptr;
	SDL_cond* cond = nullptr;
	PacketQueue* pktq = nullptr;
	void unref_item(Frame* vp);
};

// 向后兼容的自由函数包装器
// inline int frame_queue_init(FrameQueue* f, PacketQueue* pktq, int max_size, int keep_last) { return f->init(pktq, max_size, keep_last); }
// inline void frame_queue_destory(FrameQueue* f) { f->destroy(); }
// inline void frame_queue_signal(FrameQueue* f) { f->signal(); }
// inline Frame* frame_queue_peek(FrameQueue* f) { return f->peek(); }
// inline Frame* frame_queue_peek_next(FrameQueue* f) { return f->peek_next(); }
// inline Frame* frame_queue_peek_last(FrameQueue* f) { return f->peek_last(); }
// inline Frame* frame_queue_peek_writable(FrameQueue* f) { return f->peek_writable(); }
// inline Frame* frame_queue_peek_readable(FrameQueue* f) { return f->peek_readable(); }
// inline void frame_queue_push(FrameQueue* f) { f->push(); }
// inline void frame_queue_next(FrameQueue* f) { f->next(); }
// inline int frame_queue_nb_remaining(FrameQueue* f) { return f->nb_remaining(); }
// inline int64_t frame_queue_last_pos(FrameQueue* f) { return f->last_pos(); }
