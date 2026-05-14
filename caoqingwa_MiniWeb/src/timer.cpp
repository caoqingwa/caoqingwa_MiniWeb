#include "timer.h"

void TimerManager::touch(int fd) {
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(mutex);
    last_active[fd] = now;
}

void TimerManager::remove(int fd) {
    std::lock_guard<std::mutex> lock(mutex);
    last_active.erase(fd);
}

std::vector<int> TimerManager::get_expired(std::chrono::seconds timeout) {
    const auto now = Clock::now();
    std::vector<int> expired;

    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = last_active.begin(); it != last_active.end();) {
        if (now - it->second >= timeout) {
            expired.push_back(it->first);
            it = last_active.erase(it);
        }
        else {
            ++it;
        }
    }
    return expired;
}
