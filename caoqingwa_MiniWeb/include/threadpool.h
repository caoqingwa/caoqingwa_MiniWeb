#pragma once
#include <cstddef>
#include <thread>
#include <queue>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <utility>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);

    template<class T>
    void enqueue(T&& t);

    ~ThreadPool();

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop{ false };
};

template<class T>
void ThreadPool::enqueue(T&& t) {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        tasks.emplace(std::forward<T>(t));
    }
    condition.notify_one();
}
