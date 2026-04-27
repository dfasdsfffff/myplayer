/*
* @file 	decoder.cpp
* @brief 	解码器操作实现
* @note 	从 datactl.h 拆分出的 Decoder 函数实现
*/

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
#include <libavutil/frame.h>
}

#include <SDL2/SDL.h>

#include "decoder.h"
#include "packet_queue.h"
#include "frame_queue.h"

// 解码器重排序pts，-1表示自动，0表示不重排序，1表示重排序
int decoder_reorder_pts = -1;

//解码器初始化（绑定解码结构体、数据包队列、信号量，初始化pts）
int decoder_init(Decoder* d, AVCodecContext* avctx, PacketQueue* queue, SDL_cond* empty_queue_cond)
{
    memset(d, 0, sizeof(Decoder));
    d->pkt = av_packet_alloc();
    if (!d->pkt)
        return AVERROR(ENOMEM);
    d->avctx = avctx;
    d->queue = queue;
    d->empty_queue_cond = empty_queue_cond;
    d->start_pts = AV_NOPTS_VALUE;
    d->pkt_serial = -1;
    return 0;
}

//解码一帧数据
int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub)
{
    int ret = AVERROR(EAGAIN);

    for (;;) {
        if (d->queue->serial == d->pkt_serial) {
            do {
                if (d->queue->abort_request)
                    return -1;

                switch (d->avctx->codec_type) {
                case AVMEDIA_TYPE_VIDEO:
                    ret = avcodec_receive_frame(d->avctx, frame);
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
                    ret = avcodec_receive_frame(d->avctx, frame);
                    if (ret >= 0) {
                        AVRational tb = { 1, frame->sample_rate };
                        if (frame->pts != AV_NOPTS_VALUE)
                            frame->pts = av_rescale_q(frame->pts, d->avctx->pkt_timebase, tb);
                        else if (d->next_pts != AV_NOPTS_VALUE)
                            frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
                        if (frame->pts != AV_NOPTS_VALUE) {
                            d->next_pts = frame->pts + frame->nb_samples;
                            d->next_pts_tb = tb;
                        }
                    }
                    break;
                }
                if (ret == AVERROR_EOF) {
                    d->finished = d->pkt_serial;
                    avcodec_flush_buffers(d->avctx);
                    return 0;
                }
                if (ret >= 0)
                    return 1;
            } while (ret != AVERROR(EAGAIN));
        }

        do {
            if (d->queue->nb_packets == 0)
                SDL_CondSignal(d->empty_queue_cond);
            if (d->packet_pending) {
                d->packet_pending = 0;
            }
            else {
                int old_serial = d->pkt_serial;
                if (packet_queue_get(d->queue, d->pkt, 1, &d->pkt_serial) < 0)
                    return -1;
                if (old_serial != d->pkt_serial) {
                    avcodec_flush_buffers(d->avctx);
                    d->finished = 0;
                    d->next_pts = d->start_pts;
                    d->next_pts_tb = d->start_pts_tb;
                }
            }
            if (d->queue->serial == d->pkt_serial)
                break;
            av_packet_unref(d->pkt);
        } while (1);

        if (d->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            int got_frame = 0;
            ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, d->pkt);
            if (ret < 0) {
                ret = AVERROR(EAGAIN);
            }
            else {
                if (got_frame && !d->pkt->data) {
                    d->packet_pending = 1;
                }
                ret = got_frame ? 0 : (d->pkt->data ? AVERROR(EAGAIN) : AVERROR_EOF);
            }
            av_packet_unref(d->pkt);
        }
        else {
            if (avcodec_send_packet(d->avctx, d->pkt) == AVERROR(EAGAIN)) {
                av_log(d->avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                d->packet_pending = 1;
            }
            else {
                av_packet_unref(d->pkt);
            }
        }
    }
}

//解码器销毁
void decoder_destroy(Decoder *d)
{
    av_packet_free(&d->pkt);
    avcodec_free_context(&d->avctx);
}

void decoder_abort(Decoder* d, FrameQueue* fq)
{
    packet_queue_abort(d->queue);
    frame_queue_signal(fq);
    d->decode_thread.join();
    packet_queue_flush(d->queue);
}
