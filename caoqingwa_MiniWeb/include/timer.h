#pragma once

#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

class TimerManager {
public:
    using Clock = std::chrono::steady_clock;

    void touch(int fd);
    void remove(int fd);
    std::vector<int> get_expired(std::chrono::seconds timeout);

private:
    std::unordered_map<int, Clock::time_point> last_active;
    std::mutex mutex;
};
