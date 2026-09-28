#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

// Runs queued tasks one by one on a single worker thread.
class xTaskQueue {
public:
	xTaskQueue() : m_thread([this](std::stop_token st) { Run(st); }) {}
	~xTaskQueue() {
		m_thread.request_stop();
		m_cv.notify_all();
	}

	void Push(std::function<void()> task) {
		{
			std::scoped_lock lock(m_mtx);
			m_tasks.push_back(std::move(task));
		}
		m_cv.notify_one();
	}

protected:
	std::mutex m_mtx;
	std::condition_variable_any m_cv;
	std::deque<std::function<void()>> m_tasks;
	std::jthread m_thread;	// last : destroyed (joined) first

	void Run(std::stop_token st) {
		while (true) {
			std::function<void()> task;
			{
				std::unique_lock lock(m_mtx);
				if (!m_cv.wait(lock, st, [this] { return !m_tasks.empty(); }))
					return;
				task = std::move(m_tasks.front());
				m_tasks.pop_front();
			}
			task();
		}
	}
};
