#pragma once

#include "connection_stats.h"
#include <map>
#include <list>
#include <string>

#include <vector>

// Distribution bucket structure
struct DistributionBucket {
    std::string label;
    int count;
};

class stats_manager {
public:
    stats_manager(size_t max_history_size = 100);

    void on_connection_opened(const std::string& id, const std::string& rttp_addr, const std::string& tcp_addr);
    void on_connection_closed(const std::string& id);
    void on_data_transferred(const std::string& id, uint64_t sent_delta, uint64_t received_delta);
    void update_rttp_stats(const std::string& id, double rtt, double loss_rate);

    // Statistics methods
    size_t get_active_connection_count() const;
    size_t get_history_connection_count() const;
    std::vector<DistributionBucket> calculate_rtt_distribution(bool active) const;
    std::vector<DistributionBucket> calculate_loss_rate_distribution(bool active) const;

    // HTML generation methods
    std::string generate_homepage_html();
    std::string generate_stats_html();  // Redirects to active stats
    std::string generate_stats_active_html();
    std::string generate_stats_history_html();
    std::string generate_connections_html();

private:
    std::map<std::string, ConnectionInfo> active_connections_;
    std::list<ConnectionInfo> history_;
    size_t max_history_size_;
};
