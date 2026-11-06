/*
* @file 	frame_queue.cpp
* @brief 	帧队列类实现
* @note 	从 datactl.h 拆分，由 C 结构体+自由函数 封装为 class
*/

#include "frame_queue.h"

FrameQueue::~FrameQueue()
{
	destroy();
}

void FrameQueue::unref_item(Frame* vp)
{
	if (vp->frame)
		av_frame_unref(vp->frame);
	vp->subtitle.reset();
}

//帧队列初始化（绑定数据包队列，初始化最大值）
int FrameQueue::init(PacketQueue* _pktq, int _max_size, int _keep_last)
{
	int i;
	rindex = 0;
	windex = 0;
	size = 0;
	max_size = FFMIN(_max_size, FRAME_QUEUE_SIZE);
	keep_last = !!_keep_last;
	rindex_shown = 0;
	pktq = _pktq;

	if (!(mutex = SDL_CreateMutex())) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	if (!(cond = SDL_CreateCond())) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	for (i = 0; i < max_size; i++)
		if (!(queue[i].frame = av_frame_alloc()))
			return AVERROR(ENOMEM);
	return 0;
}

//帧队列销毁
void FrameQueue::destroy()
{
	int i;
	for (i = 0; i < max_size; i++) {
		Frame* vp = &queue[i];
		unref_item(vp);
		av_frame_free(&vp->frame);
	}
	max_size = 0;
	if (mutex) {
		SDL_DestroyMutex(mutex);
		mutex = nullptr;
	}
	if (cond) {
		SDL_DestroyCond(cond);
		cond = nullptr;
	}
}

//帧队列信号
void FrameQueue::signal()
{
	if (!mutex)
		return;

	SDL_LockMutex(mutex);
	if (cond)
		SDL_CondSignal(cond);
	SDL_UnlockMutex(mutex);
}

FrameQueueSnapshot FrameQueue::snapshot() const
{
	FrameQueueSnapshot result;
	if (!mutex)
		return result;
	SDL_LockMutex(mutex);
	result.remaining = size - rindex_shown;
	result.serial = pktq ? pktq->serial() : 0;
	result.aborted = !pktq || pktq->isAborted();
	SDL_UnlockMutex(mutex);
	return result;
}

bool FrameQueue::hasShown() const
{
	if (!mutex)
		return false;
	SDL_LockMutex(mutex);
	const bool shown = rindex_shown != 0;
	SDL_UnlockMutex(mutex);
	return shown;
}

void FrameQueue::lock()
{
	SDL_LockMutex(mutex);
}

void FrameQueue::unlock()
{
	SDL_UnlockMutex(mutex);
}

Frame* FrameQueue::peek()
{
	SDL_LockMutex(mutex);
	Frame* result = &queue[(rindex + rindex_shown) % max_size];
	SDL_UnlockMutex(mutex);
	return result;
}

Frame* FrameQueue::peek_next()
{
	SDL_LockMutex(mutex);
	Frame* result = &queue[(rindex + rindex_shown + 1) % max_size];
	SDL_UnlockMutex(mutex);
	return result;
}

Frame* FrameQueue::peek_last()
{
	SDL_LockMutex(mutex);
	Frame* result = &queue[rindex];
	SDL_UnlockMutex(mutex);
	return result;
}

Frame* FrameQueue::peek_writable()
{
	/* wait until we have space to put a new frame */
	SDL_LockMutex(mutex);
	while (size >= max_size &&
		!pktq->isAborted()) {
		SDL_CondWait(cond, mutex);
	}
	const bool aborted = pktq->isAborted();
	Frame* result = aborted ? nullptr : &queue[windex];
	SDL_UnlockMutex(mutex);
	return result;
}

Frame* FrameQueue::peek_readable()
{
	/* wait until we have a readable a new frame */
	SDL_LockMutex(mutex);
	while (size - rindex_shown <= 0 &&
		!pktq->isAborted()) {
		SDL_CondWait(cond, mutex);
	}
	const bool aborted = pktq->isAborted();
	Frame* result = aborted ? nullptr : &queue[(rindex + rindex_shown) % max_size];
	SDL_UnlockMutex(mutex);
	return result;
}

int FrameQueue::wait_readable_for(Uint32 timeout_ms)
{
	SDL_LockMutex(mutex);
	int ret = 0;
	while (size - rindex_shown <= 0 && !pktq->isAborted() && ret == 0) {
		ret = SDL_CondWaitTimeout(cond, mutex, timeout_ms);
	}
	const int readable = size - rindex_shown > 0;
	SDL_UnlockMutex(mutex);

	return readable ? 1 : 0;
}

void FrameQueue::push()
{
	SDL_LockMutex(mutex);
	if (++windex == max_size)
		windex = 0;
	size++;
	SDL_CondSignal(cond);
	SDL_UnlockMutex(mutex);
}

void FrameQueue::next()
{
	SDL_LockMutex(mutex);
	if (keep_last && !rindex_shown) {
		rindex_shown = 1;
		SDL_UnlockMutex(mutex);
		return;
	}
	unref_item(&queue[rindex]);
	if (++rindex == max_size)
		rindex = 0;
	size--;
	SDL_CondSignal(cond);
	SDL_UnlockMutex(mutex);
}

/* return the number of undisplayed frames in the queue */
int FrameQueue::nb_remaining()
{
	if (!mutex)
		return 0;
	SDL_LockMutex(mutex);
	const int remaining = size - rindex_shown;
	SDL_UnlockMutex(mutex);
	return remaining;
}

/* return last shown position */
int64_t FrameQueue::last_pos()
{
	if (!mutex)
		return -1;
	SDL_LockMutex(mutex);
	Frame* fp = &queue[rindex];
	const int64_t pos = rindex_shown && fp->serial == pktq->serial() ? fp->pos : -1;
	SDL_UnlockMutex(mutex);
	return pos;
}
