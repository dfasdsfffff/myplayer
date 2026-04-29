/*
* @file 	signal.h
* @brief 	Signal 类型别名，基于 sigslot 库
* @note 	替代 Qt signals/slots，供 VideoCtl 使用
*/

#pragma once

#include <sigslot/signal.hpp>

// 线程安全的 Signal，直接使用 sigslot 库
// 用法与手写 Signal 一致：
//   Signal<int, double> sig;
//   sig.connect([](int a, double b) { ... });
//   sig(42, 3.14);
template <typename... Args>
using Signal = sigslot::signal<Args...>;
