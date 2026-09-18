/*
* @file 	clock.cpp
* @brief 	时钟类实现
*/

#include "clock.h"

void Clock::init(const std::atomic<int>* queue_serial)
{
	m_queueSerial.store(queue_serial, std::memory_order_release);
	writeSnapshot({NAN, NAN, 0.0, 1.0, -1, false});
	set(NAN, -1);
}

ClockSnapshot Clock::snapshot() const noexcept
{
	for (;;) {
		const uint64_t begin = m_sequence.load(std::memory_order_acquire);
		if (begin & 1)
			continue;
		ClockSnapshot value{
			m_pts.load(std::memory_order_relaxed),
			m_ptsDrift.load(std::memory_order_relaxed),
			m_lastUpdated.load(std::memory_order_relaxed),
			m_speed.load(std::memory_order_relaxed),
			m_serial.load(std::memory_order_relaxed),
			m_paused.load(std::memory_order_relaxed)};
		if (begin == m_sequence.load(std::memory_order_acquire))
			return value;
	}
}

double Clock::get() const noexcept
{
	const ClockSnapshot value = snapshot();
	const auto* queueSerial = m_queueSerial.load(std::memory_order_acquire);
	if (!queueSerial || queueSerial->load(std::memory_order_acquire) != value.serial)
		return NAN;
	if (value.paused)
		return value.pts;
	const double time = av_gettime_relative() / 1000000.0;
	return value.ptsDrift + time - (time - value.lastUpdated) * (1.0 - value.speed);
}

void Clock::writeSnapshot(const ClockSnapshot& value)
{
	std::lock_guard<std::mutex> lock(m_writeMutex);
	m_sequence.fetch_add(1, std::memory_order_release);
	m_pts.store(value.pts, std::memory_order_relaxed);
	m_ptsDrift.store(value.ptsDrift, std::memory_order_relaxed);
	m_lastUpdated.store(value.lastUpdated, std::memory_order_relaxed);
	m_speed.store(value.speed, std::memory_order_relaxed);
	m_serial.store(value.serial, std::memory_order_relaxed);
	m_paused.store(value.paused, std::memory_order_relaxed);
	m_sequence.fetch_add(1, std::memory_order_release);
}

void Clock::set_at(double pts, int serial, double time)
{
	const ClockSnapshot old = snapshot();
	writeSnapshot({pts, pts - time, time, old.speed, serial, old.paused});
}

void Clock::set(double pts, int serial)
{
	double time = av_gettime_relative() / 1000000.0;
	set_at(pts, serial, time);
}

void Clock::set_speed(double speed)
{
	const ClockSnapshot old = snapshot();
	const double current = get();
	const double time = av_gettime_relative() / 1000000.0;
	writeSnapshot({current, current - time, time, speed, old.serial, old.paused});
}

void Clock::setPaused(bool paused)
{
	const ClockSnapshot old = snapshot();
	writeSnapshot({old.pts, old.ptsDrift, old.lastUpdated, old.speed, old.serial, paused});
}

double Clock::lastUpdated() const noexcept
{
	return snapshot().lastUpdated;
}

double Clock::speed() const noexcept
{
	return snapshot().speed;
}

int Clock::serial() const noexcept
{
	return snapshot().serial;
}

const std::atomic<int>* Clock::serialStorage() const noexcept
{
	return &m_serial;
}

void Clock::sync_to_slave(const Clock& slave)
{
	double clock = get();
	double slave_clock = slave.get();
	if (!std::isnan(slave_clock) && (std::isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
		set(slave_clock, slave.serial());
}
