/*
* @file 	clock.h
* @brief 	时钟类，管理播放时钟的同步与速度控制
* @note 	从 av_types.h 拆分，封装自 struct Clock
*/

#pragma once

#include <cmath>
#include <cstdint>

#include "av_compat.h"
#include "av_constants.h"

class Clock {
public:
	void init(int* queue_serial);
	double get() const;
	void set(double pts, int serial);
	void set_at(double pts, int serial, double time);
	void set_speed(double speed);
	void sync_to_slave(const Clock& slave);

public:
	double pts = 0.0;           /* clock base */
	double pts_drift = 0.0;     /* clock base minus time at which we updated the clock */
	double last_updated = 0.0;
	double speed = 1.0;
	int serial = -1;            /* clock is based on a packet with this serial */
	int paused = 0;
	int* queue_serial = nullptr; /* pointer to the current packet queue serial, used for obsolete clock detection */
};
