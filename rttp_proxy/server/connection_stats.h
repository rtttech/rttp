#pragma once

#include <string>
#include <chrono>
#include <cstdint>

struct ConnectionInfo {
    std::string id;
    std::string rttp_remote_addr;
    std::string tcp_remote_addr;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    
    // Real-time stats
    double send_speed_kbps = 0.0; // KB/s
    double recv_speed_kbps = 0.0; // KB/s
    double rtt = 0.0; // ms
    double loss_rate = 0.0; // percentage
    
    bool is_active = true;
};
