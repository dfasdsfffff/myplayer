/*
* @file 	datactl.h
* @brief 	数据处理控制 - 伞形头文件
* @note 	原 datactl.h 已拆分为以下模块，此文件保持向后兼容：
*         - av_constants.h  常量定义
*         - av_types.h      基础数据类型
*         - packet_queue.h  数据包队列操作
*         - frame_queue.h   帧队列操作
*         - decoder.h       解码器操作
*         - video_state.h   视频状态结构体
*/

#pragma once

#include "av_constants.h"
#include "av_types.h"
#include "packet_queue.h"
#include "frame_queue.h"
#include "decoder.h"
#include "video_state.h"
