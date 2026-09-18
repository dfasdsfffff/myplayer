/*
* @file 	clock.h
* @brief 	时钟类，管理播放时钟的同步与速度控制
* @note 	从 av_types.h 拆分，封装自 struct Clock
*/

#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>

#include "av_compat.h"
#include "av_constants.h"

struct ClockSnapshot {
	double pts = NAN;
	double ptsDrift = NAN;
	double lastUpdated = 0.0;
	double speed = 1.0;
	int serial = -1;
	bool paused = false;
};

class Clock {
public:
	void init(const std::atomic<int>* queue_serial);
	double get() const noexcept;
	ClockSnapshot snapshot() const noexcept;
	void set(double pts, int serial);
	void set_at(double pts, int serial, double time);
	void set_speed(double speed);
	void setPaused(bool paused);
	double lastUpdated() const noexcept;
	double speed() const noexcept;
	int serial() const noexcept;
	const std::atomic<int>* serialStorage() const noexcept;
	void sync_to_slave(const Clock& slave);

	private:
	void writeSnapshot(const ClockSnapshot& value);

	std::atomic<double> m_pts{NAN};
	std::atomic<double> m_ptsDrift{NAN};
	std::atomic<double> m_lastUpdated{0.0};
	std::atomic<double> m_speed{1.0};
	std::atomic<int> m_serial{-1};
	std::atomic<bool> m_paused{false};
	std::atomic<const std::atomic<int>*> m_queueSerial{nullptr};
	mutable std::mutex m_writeMutex;
	std::atomic<uint64_t> m_sequence{0};
};
