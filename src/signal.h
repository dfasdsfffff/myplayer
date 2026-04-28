/*
* @file 	signal.h
* @brief 	轻量 Signal 模板，替代 Qt signals/slots
* @note 	基于 std::function + std::vector，无第三方依赖
*          connect() 必须在工作线程启动前完成（无线程安全保护）
*/

#pragma once

#include <functional>
#include <vector>
#include <algorithm>

// 轻量 Signal：可观察的回调列表
// 用法：
//   Signal<int, double> sig;
//   sig.connect([](int a, double b) { ... });
//   sig.emit(42, 3.14);  // 或 sig(42, 3.14);
template <typename... Args>
class Signal {
public:
	using SlotType = std::function<void(Args...)>;

	Signal() = default;
	Signal(const Signal&) = delete;
	Signal& operator=(const Signal&) = delete;

	// 注册回调，返回连接 ID（用于后续 disconnect）
	int connect(SlotType slot)
	{
		int id = m_nextId++;
		m_slots.push_back({ id, std::move(slot) });
		return id;
	}

	// 按 ID 移除回调
	void disconnect(int id)
	{
		m_slots.erase(
			std::remove_if(m_slots.begin(), m_slots.end(),
				[id](const Entry& e) { return e.id == id; }),
			m_slots.end());
	}

	// 调用所有已注册的回调
	void emit(Args... args) const
	{
		// 拷贝后遍历，防止回调中修改列表
		auto copy = m_slots;
		for (const auto& entry : copy) {
			entry.slot(args...);
		}
	}

	// 便捷调用运算符
	void operator()(Args... args) const
	{
		emit(args...);
	}

private:
	struct Entry {
		int id;
		SlotType slot;
	};
	std::vector<Entry> m_slots;
	int m_nextId = 0;
};
