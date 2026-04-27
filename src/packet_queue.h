/*
* @file 	packet_queue.h
* @brief 	数据包队列操作接口声明
* @note 	从 datactl.h 拆分出的 PacketQueue 函数声明
*/

#pragma once

#include "av_types.h"

//数据包队列初始化
int packet_queue_init(PacketQueue *q);

//数据包队列销毁
void packet_queue_destroy(PacketQueue *q);

//数据包队列开始使用
void packet_queue_start(PacketQueue *q);

//数据包队列停用
void packet_queue_abort(PacketQueue *q);

//数据包队列清空
void packet_queue_flush(PacketQueue *q);

//增加序列号
void packet_queue_add_serial(PacketQueue *q);

//数据包队列存放数据包
int packet_queue_put(PacketQueue *q, AVPacket *pkt);

//数据包队列存放空数据包
int packet_queue_put_nullpacket(PacketQueue* q, AVPacket* pkt, int stream_index);

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
//从数据包队列中获取数据包
int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block, int* serial);
