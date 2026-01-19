#include "stats_manager.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iostream>

stats_manager::stats_manager(size_t max_history_size)
    : max_history_size_(max_history_size)
{
}

void stats_manager::on_connection_opened(const std::string& id, const std::string& rttp_addr, const std::string& tcp_addr)
{

    ConnectionInfo info;
    info.id = id;
    info.rttp_remote_addr = rttp_addr;
    info.tcp_remote_addr = tcp_addr;
    info.start_time = std::chrono::system_clock::now();
    info.is_active = true;
    active_connections_[id] = info;
}

void stats_manager::on_connection_closed(const std::string& id)
{

    auto it = active_connections_.find(id);
    if (it != active_connections_.end()) {
        it->second.end_time = std::chrono::system_clock::now();
        it->second.is_active = false;
        
        // Move to history
        history_.push_front(it->second);
        if (history_.size() > max_history_size_) {
            history_.pop_back();
        }
        
        active_connections_.erase(it);
    }
}

void stats_manager::on_data_transferred(const std::string& id, uint64_t sent_delta, uint64_t received_delta)
{

    auto it = active_connections_.find(id);
    if (it != active_connections_.end()) {
        it->second.bytes_sent += sent_delta;
        it->second.bytes_received += received_delta;
        
        // Calculate simple average speed for now (total bytes / total time)
        // A better approach would be a moving window, but this is a start.
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.start_time).count();
        if (duration > 0) {
            it->second.send_speed_kbps = (double)it->second.bytes_sent / duration * 1000.0 / 1024.0;
            it->second.recv_speed_kbps = (double)it->second.bytes_received / duration * 1000.0 / 1024.0;
        }
    }
}

void stats_manager::update_rttp_stats(const std::string& id, double rtt, double loss_rate)
{

    auto it = active_connections_.find(id);
    if (it != active_connections_.end()) {
        it->second.rtt = rtt;
        it->second.loss_rate = loss_rate;
    }
}

size_t stats_manager::get_active_connection_count() const
{

    return active_connections_.size();
}

size_t stats_manager::get_history_connection_count() const
{

    return history_.size();
}

std::vector<DistributionBucket> stats_manager::calculate_rtt_distribution(bool active) const
{
    // Note: mutex already locked by caller
    
    // RTT buckets: <1ms, 1-5ms, 5-10ms, 10-20ms, 20-50ms, 50-100ms, 100-200ms, 200-500ms, >500ms
    std::vector<DistributionBucket> distribution = {
        {"< 1ms", 0},
        {"1-5ms", 0},
        {"5-10ms", 0},
        {"10-20ms", 0},
        {"20-50ms", 0},
        {"50-100ms", 0},
        {"100-200ms", 0},
        {"200-500ms", 0},
        {"> 500ms", 0}
    };
    
    if (active) {
        for (const auto& pair : active_connections_) {
            double rtt = pair.second.rtt;
            if (rtt < 1.0) distribution[0].count++;
            else if (rtt < 5.0) distribution[1].count++;
            else if (rtt < 10.0) distribution[2].count++;
            else if (rtt < 20.0) distribution[3].count++;
            else if (rtt < 50.0) distribution[4].count++;
            else if (rtt < 100.0) distribution[5].count++;
            else if (rtt < 200.0) distribution[6].count++;
            else if (rtt < 500.0) distribution[7].count++;
            else distribution[8].count++;
        }
    } else {
        for (const auto& info : history_) {
            double rtt = info.rtt;
            if (rtt < 1.0) distribution[0].count++;
            else if (rtt < 5.0) distribution[1].count++;
            else if (rtt < 10.0) distribution[2].count++;
            else if (rtt < 20.0) distribution[3].count++;
            else if (rtt < 50.0) distribution[4].count++;
            else if (rtt < 100.0) distribution[5].count++;
            else if (rtt < 200.0) distribution[6].count++;
            else if (rtt < 500.0) distribution[7].count++;
            else distribution[8].count++;
        }
    }
    
    return distribution;
}

std::vector<DistributionBucket> stats_manager::calculate_loss_rate_distribution(bool active) const
{
    // Note: mutex already locked by caller
    
    // Loss rate buckets: <0.1%, 0.1-1%, 1-5%, 5-10%, 10-20%, 20-30%, >30%
    std::vector<DistributionBucket> distribution = {
        {"< 0.1%", 0},
        {"0.1-1%", 0},
        {"1-5%", 0},
        {"5-10%", 0},
        {"10-20%", 0},
        {"20-30%", 0},
        {"> 30%", 0}
    };
    
    if (active) {
        for (const auto& pair : active_connections_) {
            double loss = pair.second.loss_rate;
            if (loss < 0.1) distribution[0].count++;
            else if (loss < 1.0) distribution[1].count++;
            else if (loss < 5.0) distribution[2].count++;
            else if (loss < 10.0) distribution[3].count++;
            else if (loss < 20.0) distribution[4].count++;
            else if (loss < 30.0) distribution[5].count++;
            else distribution[6].count++;
        }
    } else {
        for (const auto& info : history_) {
            double loss = info.loss_rate;
            if (loss < 0.1) distribution[0].count++;
            else if (loss < 1.0) distribution[1].count++;
            else if (loss < 5.0) distribution[2].count++;
            else if (loss < 10.0) distribution[3].count++;
            else if (loss < 20.0) distribution[4].count++;
            else if (loss < 30.0) distribution[5].count++;
            else distribution[6].count++;
        }
    }
    
    return distribution;
}

std::string format_time(const std::chrono::system_clock::time_point& tp) {
    auto in_time_t = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string stats_manager::generate_homepage_html()
{

    
    std::stringstream ss;
    
    ss << "<!DOCTYPE html><html><head><title>RTTP Proxy Monitor</title>";
    ss << "<meta charset='UTF-8'>";
    ss << "<style>";
    ss << "body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }";
    ss << "h1 { color: #333; }";
    ss << ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 4px; }";
    ss << ".category { border-bottom: 1px solid #ddd; padding: 15px 0; }";
    ss << ".category:last-child { border-bottom: none; }";
    ss << ".category a { text-decoration: none; color: #2196F3; font-size: 18px; font-weight: bold; }";
    ss << ".category a:hover { text-decoration: underline; }";
    ss << ".category-desc { color: #666; margin-top: 5px; font-size: 14px; }";
    ss << "</style>";
    ss << "</head><body>";
    
    ss << "<div class='container'>";
    ss << "<h1>RTTP Proxy Monitor</h1>";
    
    ss << "<div class='category'>";
    ss << "<a href='/health'>Health Check</a>";
    ss << "<div class='category-desc'>/health - Liveness probe, returns server health status (JSON)</div>";
    ss << "</div>";
    
    ss << "<div class='category'>";
    ss << "<a href='/ready'>Readiness Check</a>";
    ss << "<div class='category-desc'>/ready - Readiness probe, returns if server is accepting connections (JSON)</div>";
    ss << "</div>";
    
    ss << "<div class='category'>";
    ss << "<a href='/metrics'>Prometheus Metrics</a>";
    ss << "<div class='category-desc'>/metrics - Prometheus format metrics for monitoring and alerting</div>";
    ss << "</div>";
    
    ss << "<div class='category'>";
    ss << "<a href='/stats-active'>Active Connection Statistics</a>";
    ss << "<div class='category-desc'>/stats-active - RTT and loss rate distributions for currently active connections</div>";
    ss << "</div>";
    
    ss << "<div class='category'>";
    ss << "<a href='/stats-history'>Historical Connection Statistics</a>";
    ss << "<div class='category-desc'>/stats-history - RTT and loss rate distributions for historical connections</div>";
    ss << "</div>";
    
    ss << "<div class='category'>";
    ss << "<a href='/connections'>Connection Details</a>";
    ss << "<div class='category-desc'>/connections - Detailed information about active and historical connections</div>";
    ss << "</div>";
    
    ss << "</div>";
    ss << "</body></html>";
    
    return ss.str();
}

std::string stats_manager::generate_stats_html()
{
    // This method now redirects to active stats by default
    return generate_stats_active_html();
}

std::string stats_manager::generate_stats_active_html()
{

    
    std::stringstream ss;
    
    ss << "<!DOCTYPE html><html><head><title>Active Connection Statistics</title>";
    ss << "<meta charset='UTF-8'>";
    ss << "<style>";
    ss << "body { font-family: Arial, sans-serif; margin: 20px; }";
    ss << ".back-btn { display: inline-block; padding: 8px 16px; background: #2196F3; color: white; text-decoration: none; border-radius: 4px; margin-bottom: 20px; }";
    ss << ".back-btn:hover { background: #1976D2; }";
    ss << "h1, h2 { color: #333; }";
    ss << ".stat-box { background: #e3f2fd; padding: 15px; border-radius: 4px; margin-bottom: 10px; }";
    ss << ".stat-number { font-size: 24px; font-weight: bold; color: #1976D2; }";
    ss << ".dist-item { padding: 8px 0; border-bottom: 1px solid #eee; }";
    ss << ".dist-item:last-child { border-bottom: none; }";
    ss << ".dist-label { display: inline-block; min-width: 120px; }";
    ss << ".dist-count { font-weight: bold; margin-left: 20px; }";
    ss << ".dist-percent { color: #666; margin-left: 10px; }";
    ss << "</style>";
    ss << "<meta http-equiv='refresh' content='5'>";
    ss << "</head><body>";
    
    ss << "<a href='/' class='back-btn'>Back to Home</a>";
    ss << "<h1>Active Connection Statistics</h1>";
    
    // Connection count
    int total = active_connections_.size();
    ss << "<div class='stat-box'>";
    ss << "<div class='stat-number'>" << total << "</div>";
    ss << "<div>Active Connections</div>";
    ss << "</div>";
    
    // RTT distribution
    auto rtt_dist = calculate_rtt_distribution(true);
    ss << "<h2>RTT Distribution</h2>";
    for (const auto& bucket : rtt_dist) {
        double percent = total > 0 ? (bucket.count * 100.0 / total) : 0.0;
        ss << "<div class='dist-item'>";
        ss << "<span class='dist-label'>" << bucket.label << "</span>";
        ss << "<span class='dist-count'>" << bucket.count << "</span>";
        ss << "<span class='dist-percent'>(" << std::fixed << std::setprecision(1) << percent << "%)</span>";
        ss << "</div>";
    }
    
    // Loss rate distribution
    auto loss_dist = calculate_loss_rate_distribution(true);
    ss << "<h2>Loss Rate Distribution</h2>";
    for (const auto& bucket : loss_dist) {
        double percent = total > 0 ? (bucket.count * 100.0 / total) : 0.0;
        ss << "<div class='dist-item'>";
        ss << "<span class='dist-label'>" << bucket.label << "</span>";
        ss << "<span class='dist-count'>" << bucket.count << "</span>";
        ss << "<span class='dist-percent'>(" << std::fixed << std::setprecision(1) << percent << "%)</span>";
        ss << "</div>";
    }
    
    ss << "</body></html>";
    
    return ss.str();
}

std::string stats_manager::generate_stats_history_html()
{

    
    std::stringstream ss;
    
    ss << "<!DOCTYPE html><html><head><title>Historical Connection Statistics</title>";
    ss << "<meta charset='UTF-8'>";
    ss << "<style>";
    ss << "body { font-family: Arial, sans-serif; margin: 20px; }";
    ss << ".back-btn { display: inline-block; padding: 8px 16px; background: #2196F3; color: white; text-decoration: none; border-radius: 4px; margin-bottom: 20px; }";
    ss << ".back-btn:hover { background: #1976D2; }";
    ss << "h1, h2 { color: #333; }";
    ss << ".stat-box { background: #e3f2fd; padding: 15px; border-radius: 4px; margin-bottom: 10px; }";
    ss << ".stat-number { font-size: 24px; font-weight: bold; color: #1976D2; }";
    ss << ".dist-item { padding: 8px 0; border-bottom: 1px solid #eee; }";
    ss << ".dist-item:last-child { border-bottom: none; }";
    ss << ".dist-label { display: inline-block; min-width: 120px; }";
    ss << ".dist-count { font-weight: bold; margin-left: 20px; }";
    ss << ".dist-percent { color: #666; margin-left: 10px; }";
    ss << "</style>";
    ss << "<meta http-equiv='refresh' content='5'>";
    ss << "</head><body>";
    
    ss << "<a href='/' class='back-btn'>Back to Home</a>";
    ss << "<h1>Historical Connection Statistics</h1>";
    
    // Connection count
    int total = history_.size();
    ss << "<div class='stat-box'>";
    ss << "<div class='stat-number'>" << total << "</div>";
    ss << "<div>Historical Connections</div>";
    ss << "</div>";
    
    // RTT distribution
    auto rtt_dist = calculate_rtt_distribution(false);
    ss << "<h2>RTT Distribution</h2>";
    for (const auto& bucket : rtt_dist) {
        double percent = total > 0 ? (bucket.count * 100.0 / total) : 0.0;
        ss << "<div class='dist-item'>";
        ss << "<span class='dist-label'>" << bucket.label << "</span>";
        ss << "<span class='dist-count'>" << bucket.count << "</span>";
        ss << "<span class='dist-percent'>(" << std::fixed << std::setprecision(1) << percent << "%)</span>";
        ss << "</div>";
    }
    
    // Loss rate distribution
    auto loss_dist = calculate_loss_rate_distribution(false);
    ss << "<h2>Loss Rate Distribution</h2>";
    for (const auto& bucket : loss_dist) {
        double percent = total > 0 ? (bucket.count * 100.0 / total) : 0.0;
        ss << "<div class='dist-item'>";
        ss << "<span class='dist-label'>" << bucket.label << "</span>";
        ss << "<span class='dist-count'>" << bucket.count << "</span>";
        ss << "<span class='dist-percent'>(" << std::fixed << std::setprecision(1) << percent << "%)</span>";
        ss << "</div>";
    }
    
    ss << "</body></html>";
    
    return ss.str();
}

std::string stats_manager::generate_connections_html()
{

    
    std::stringstream ss;
    
    ss << "<!DOCTYPE html><html><head><title>Connections - RTTP Proxy</title>";
    ss << "<meta charset='UTF-8'>";
    ss << "<style>";
    ss << "body { font-family: Arial, sans-serif; margin: 20px; }";
    ss << ".back-btn { display: inline-block; padding: 8px 16px; background: #2196F3; color: white; text-decoration: none; border-radius: 4px; margin-bottom: 20px; }";
    ss << ".back-btn:hover { background: #1976D2; }";
    ss << "table { border-collapse: collapse; width: 100%; margin-bottom: 20px; }";
    ss << "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }";
    ss << "th { background-color: #f2f2f2; }";
    ss << "tr:nth-child(even) { background-color: #f9f9f9; }";
    ss << "h1, h2 { color: #333; }";
    ss << "</style>";
    ss << "<meta http-equiv='refresh' content='5'>";
    ss << "</head><body>";
    
    ss << "<a href='/' class='back-btn'>Back to Home</a>";
    ss << "<h1>Connection Details</h1>";
    
    ss << "<h2>Active Connections (" << active_connections_.size() << ")</h2>";
    ss << "<table>";
    ss << "<tr><th>ID</th><th>RTTP Remote</th><th>TCP Remote</th><th>Start Time</th><th>Sent (Bytes)</th><th>Recv (Bytes)</th><th>Send Speed (KB/s)</th><th>Recv Speed (KB/s)</th><th>RTT (ms)</th><th>Loss Rate (%)</th></tr>";
    
    for (const auto& pair : active_connections_) {
        const auto& info = pair.second;
        ss << "<tr>";
        ss << "<td>" << info.id << "</td>";
        ss << "<td>" << info.rttp_remote_addr << "</td>";
        ss << "<td>" << info.tcp_remote_addr << "</td>";
        ss << "<td>" << format_time(info.start_time) << "</td>";
        ss << "<td>" << info.bytes_sent << "</td>";
        ss << "<td>" << info.bytes_received << "</td>";
        ss << "<td>" << std::fixed << std::setprecision(2) << info.send_speed_kbps << "</td>";
        ss << "<td>" << std::fixed << std::setprecision(2) << info.recv_speed_kbps << "</td>";
        ss << "<td>" << std::fixed << std::setprecision(2) << info.rtt << "</td>";
        ss << "<td>" << std::fixed << std::setprecision(2) << info.loss_rate << "</td>";
        ss << "</tr>";
    }
    ss << "</table>";
    
    ss << "<h2>Connection History (Last " << history_.size() << ")</h2>";
    ss << "<table>";
    ss << "<tr><th>ID</th><th>RTTP Remote</th><th>TCP Remote</th><th>Start Time</th><th>End Time</th><th>Sent (Bytes)</th><th>Recv (Bytes)</th><th>RTT (ms)</th><th>Loss Rate (%)</th></tr>";
    
    for (const auto& info : history_) {
        ss << "<tr>";
        ss << "<td>" << info.id << "</td>";
        ss << "<td>" << info.rttp_remote_addr << "</td>";
        ss << "<td>" << info.tcp_remote_addr << "</td>";
        ss << "<td>" << format_time(info.start_time) << "</td>";
        ss << "<td>" << format_time(info.end_time) << "</td>";
        ss << "<td>" << info.bytes_sent << "</td>";
        ss << "<td>" << info.bytes_received << "</td>";
        ss << "<td>" << std::fixed << std::setprecision(2) << info.rtt << "</td>";
        ss << "<td>" << std::fixed << std::setprecision(2) << info.loss_rate << "</td>";
        ss << "</tr>";
    }
    ss << "</table>";
    
    ss << "</body></html>";
    
    return ss.str();
}
