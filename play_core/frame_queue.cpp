/*
* @file 	frame_queue.cpp
* @brief 	帧队列类实现
* @note 	从 datactl.h 拆分，由 C 结构体+自由函数 封装为 class
*/

#include "frame_queue.h"

void FrameQueue::unref_item(Frame* vp)
{
	av_frame_unref(vp->frame);
	avsubtitle_free(&vp->sub);
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

Frame* FrameQueue::peek()
{
	return &queue[(rindex + rindex_shown) % max_size];
}

Frame* FrameQueue::peek_next()
{
	return &queue[(rindex + rindex_shown + 1) % max_size];
}

Frame* FrameQueue::peek_last()
{
	return &queue[rindex];
}

Frame* FrameQueue::peek_writable()
{
	/* wait until we have space to put a new frame */
	SDL_LockMutex(mutex);
	while (size >= max_size &&
		!pktq->abort_request) {
		SDL_CondWait(cond, mutex);
	}
	SDL_UnlockMutex(mutex);

	if (pktq->abort_request)
		return nullptr;

	return &queue[windex];
}

Frame* FrameQueue::peek_readable()
{
	/* wait until we have a readable a new frame */
	SDL_LockMutex(mutex);
	while (size - rindex_shown <= 0 &&
		!pktq->abort_request) {
		SDL_CondWait(cond, mutex);
	}
	SDL_UnlockMutex(mutex);

	if (pktq->abort_request)
		return nullptr;

	return &queue[(rindex + rindex_shown) % max_size];
}

void FrameQueue::push()
{
	if (++windex == max_size)
		windex = 0;
	SDL_LockMutex(mutex);
	size++;
	SDL_CondSignal(cond);
	SDL_UnlockMutex(mutex);
}

void FrameQueue::next()
{
	if (keep_last && !rindex_shown) {
		rindex_shown = 1;
		return;
	}
	unref_item(&queue[rindex]);
	if (++rindex == max_size)
		rindex = 0;
	SDL_LockMutex(mutex);
	size--;
	SDL_CondSignal(cond);
	SDL_UnlockMutex(mutex);
}

/* return the number of undisplayed frames in the queue */
int FrameQueue::nb_remaining()
{
	return size - rindex_shown;
}

/* return last shown position */
int64_t FrameQueue::last_pos()
{
	Frame* fp = &queue[rindex];
	if (rindex_shown && fp->serial == pktq->serial)
		return fp->pos;
	else
		return -1;
}
