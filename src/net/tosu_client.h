#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "core/dan.h"

class TosuClient {
public:
    using Handler = std::function<void(const TosuSample* sample, std::string& body)>;

    void start(const std::string& host, int port, Handler onSample);
    void stop();

private:
    void run(std::wstring host, int port);

    Handler onSample_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
