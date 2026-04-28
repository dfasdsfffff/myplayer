/*
* @file 	av_compat.h
* @brief 	FFmpeg/SDL/SoundTouch C库兼容头文件
* @note 	从 globalhelper.h 拆分，供需要直接使用 FFmpeg/SDL API 的文件包含
*			UI 层文件（ctrlbar, title, playlist 等）无需包含此文件
*/

#pragma once

// 必须加以下内容,否则编译不能通过,为了兼容C和C99标准
#ifndef INT64_C
#define INT64_C
#define UINT64_C
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/avfft.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavformat/avformat.h>
#include <libavutil/avstring.h>
#include <libavutil/bprint.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/display.h>
#include <libavutil/eval.h>
#include <libavutil/fifo.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/parseutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <SDL2/SDL.h>
}

#include <SoundTouchDLL.h>