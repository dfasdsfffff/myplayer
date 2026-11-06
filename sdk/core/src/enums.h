#pragma once
enum VideoLoopPolicy {
	LOOP_NONE = 0,      // 不循环
	LOOP_SINGLE = 1,    // 循环单个视频
	LOOP_ALL = 2,       // 循环全部
	LOOP_RANDOM = 3,    // 随机播放
	LOOP_MAX = 4,
};