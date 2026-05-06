/*
* @file 	decoder.cpp
* @brief 	解码器类实现
* @note 	从 datactl.h 拆分出的 Decoder 类成员函数实现
*/

#include "av_compat.h"

#include "decoder.h"
#include "packet_queue.h"
#include "frame_queue.h"

// 解码器重排序pts，-1表示自动，0表示不重排序，1表示重排序
int decoder_reorder_pts = -1;

//解码器初始化（绑定解码上下文、数据包队列、信号量，初始化pts）
int Decoder::init(AVCodecContext* avctx, PacketQueue* queue, SDL_cond* empty_queue_cond)
{
    pkt = av_packet_alloc();
    if (!pkt)
        return AVERROR(ENOMEM);
    this->avctx = avctx;
    this->queue = queue;
    this->empty_queue_cond = empty_queue_cond;
    start_pts = AV_NOPTS_VALUE;
    pkt_serial = -1;
    finished = 0;
    packet_pending = 0;
    next_pts = 0;
    next_pts_tb = {0, 0};
    return 0;
}

//解码一帧数据
Decoder::~Decoder()
{
    destroy();
}

int Decoder::decode_frame(AVFrame* frame, AVSubtitle* sub)
{
    int ret = AVERROR(EAGAIN);

    for (;;) {
        if (queue->serial == pkt_serial) {
            do {
                if (queue->abort_request)
                    return -1;

                switch (avctx->codec_type) {
                case AVMEDIA_TYPE_VIDEO:
                    ret = avcodec_receive_frame(avctx, frame);
                    if (ret >= 0) {
                        if (decoder_reorder_pts == -1) {
                            frame->pts = frame->best_effort_timestamp;
                        }
                        else if (!decoder_reorder_pts) {
                            frame->pts = frame->pkt_dts;
                        }
                    }
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    ret = avcodec_receive_frame(avctx, frame);
                    if (ret >= 0) {
                        AVRational tb = { 1, frame->sample_rate };
                        if (frame->pts != AV_NOPTS_VALUE)
                            frame->pts = av_rescale_q(frame->pts, avctx->pkt_timebase, tb);
                        else if (next_pts != AV_NOPTS_VALUE)
                            frame->pts = av_rescale_q(next_pts, next_pts_tb, tb);
                        if (frame->pts != AV_NOPTS_VALUE) {
                            next_pts = frame->pts + frame->nb_samples;
                            next_pts_tb = tb;
                        }
                    }
                    break;
                }
                if (ret == AVERROR_EOF) {
                    finished = pkt_serial;
                    avcodec_flush_buffers(avctx);
                    return 0;
                }
                if (ret >= 0)
                    return 1;
            } while (ret != AVERROR(EAGAIN));
        }

        do {
            if (queue->nb_packets == 0)
                SDL_CondSignal(empty_queue_cond);
            if (packet_pending) {
                packet_pending = 0;
            }
            else {
                int old_serial = pkt_serial;
                if (queue->get(pkt, 1, &pkt_serial) < 0)
                    return -1;
                if (old_serial != pkt_serial) {
                    avcodec_flush_buffers(avctx);
                    finished = 0;
                    next_pts = start_pts;
                    next_pts_tb = start_pts_tb;
                }
            }
            if (queue->serial == pkt_serial)
                break;
            av_packet_unref(pkt);
        } while (1);

        if (avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            int got_frame = 0;
            ret = avcodec_decode_subtitle2(avctx, sub, &got_frame, pkt);
            if (ret < 0) {
                ret = AVERROR(EAGAIN);
            }
            else {
                if (got_frame && !pkt->data) {
                    packet_pending = 1;
                }
                ret = got_frame ? 0 : (pkt->data ? AVERROR(EAGAIN) : AVERROR_EOF);
            }
            av_packet_unref(pkt);
        }
        else {
            if (avcodec_send_packet(avctx, pkt) == AVERROR(EAGAIN)) {
                av_log(avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                packet_pending = 1;
            }
            else {
                av_packet_unref(pkt);
            }
        }
    }
}

//解码器销毁
void Decoder::destroy()
{
    if (decode_thread.joinable()) {
        av_log(avctx, AV_LOG_WARNING, "Decoder destroyed while decode thread is still running; joining as a safety fallback.\n");
        decode_thread.join();
    }
    av_packet_free(&pkt);
    avcodec_free_context(&avctx);
    queue = nullptr;
    empty_queue_cond = nullptr;
}

void Decoder::abort(FrameQueue* fq)
{
    if (!queue)
        return;

    queue->abort();
    if (fq)
        fq->signal();
    if (decode_thread.joinable())
        decode_thread.join();
    queue->flush();
}
