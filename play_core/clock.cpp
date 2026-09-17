/*
* @file 	clock.cpp
* @brief 	时钟类实现
*/

#include "clock.h"

void Clock::init(const std::atomic<int>* queue_serial)
{
	speed = 1.0;
	paused = 0;
	this->queue_serial = queue_serial;
	set(NAN, -1);
}

double Clock::get() const
{
	if (queue_serial->load(std::memory_order_acquire) != serial.load(std::memory_order_acquire))
		return NAN;
	if (paused) {
		return pts;
	}
	else {
		double time = av_gettime_relative() / 1000000.0;
		return pts_drift + time - (time - last_updated) * (1.0 - speed);
	}
}

void Clock::set_at(double pts, int serial, double time)
{
	this->pts = pts;
	last_updated = time;
	pts_drift = pts - time;
	this->serial.store(serial, std::memory_order_release);
}

void Clock::set(double pts, int serial)
{
	double time = av_gettime_relative() / 1000000.0;
	set_at(pts, serial, time);
}

void Clock::set_speed(double speed)
{
	set(get(), serial.load(std::memory_order_acquire));
	this->speed = speed;
}

void Clock::sync_to_slave(const Clock& slave)
{
	double clock = get();
	double slave_clock = slave.get();
	if (!std::isnan(slave_clock) && (std::isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
		set(slave_clock, slave.serial.load(std::memory_order_acquire));
}
