#include "http_monitor.h"
#include "http_parser.h"
#include "rttp_server.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <iomanip>
#include <chrono>

http_monitor::http_monitor(unsigned short port, stats_manager& stats_mgr, rttp_server& server)
    : port_(port)
    , stats_mgr_(stats_mgr)
    , rttp_server_(server)
    , running_(false)
{
}

http_monitor::~http_monitor()
{
    stop();
}

void http_monitor::start()
{
    if (running_) {
        return;
    }
    
    running_ = true;
    thread_ = std::thread(thread_func, this);
}

void http_monitor::stop()
{
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // Signal the event loop to stop
    uv_async_send(&async_stop_);
    
    // Wait for thread to finish
    if (thread_.joinable()) {
        thread_.join();
    }
}

void http_monitor::post_task(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(task);
    }
    // Notify HTTP thread (non-blocking, very fast ~10-20ns)
    uv_async_send(&async_task_);
}

void http_monitor::process_tasks()
{
    // Batch process: swap queues to minimize lock time
    std::queue<std::function<void()>> local_queue;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        local_queue.swap(task_queue_);  // Fast swap, O(1)
    }  // Lock released immediately
    
    // Process all tasks without holding any lock
    while (!local_queue.empty()) {
        auto& task = local_queue.front();
        if (task) {
            task();
        }
        local_queue.pop();
    }
}

void http_monitor::thread_func(void* arg)
{
    http_monitor* monitor = static_cast<http_monitor*>(arg);
    
    // Initialize event loop
    uv_loop_init(&monitor->loop_);
    
    // Initialize async handle for stopping
    uv_async_init(&monitor->loop_, &monitor->async_stop_, on_async_stop);
    monitor->async_stop_.data = monitor;
    
    // Initialize async handle for tasks
    uv_async_init(&monitor->loop_, &monitor->async_task_, on_async_task);
    monitor->async_task_.data = monitor;
    
    // Initialize TCP server
    uv_tcp_init(&monitor->loop_, &monitor->tcp_server_);
    monitor->tcp_server_.data = monitor;
    
    // Bind to address
    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", monitor->port_, &addr);
    
    int result = uv_tcp_bind(&monitor->tcp_server_, (const struct sockaddr*)&addr, 0);
    if (result != 0) {
        std::cerr << "HTTP monitor bind failed: " << uv_strerror(result) << std::endl;
        uv_loop_close(&monitor->loop_);
        return;
    }
    
    // Start listening
    result = uv_listen((uv_stream_t*)&monitor->tcp_server_, 128, on_new_connection);
    if (result != 0) {
        std::cerr << "HTTP monitor listen failed: " << uv_strerror(result) << std::endl;
        uv_loop_close(&monitor->loop_);
        return;
    }
    
    std::cout << "HTTP monitor listening on port " << monitor->port_ << std::endl;
    
    // Run event loop
    uv_run(&monitor->loop_, UV_RUN_DEFAULT);
    
    // Cleanup
    uv_loop_close(&monitor->loop_);
}

void http_monitor::on_async_stop(uv_async_t* handle)
{
    http_monitor* monitor = static_cast<http_monitor*>(handle->data);
    
    // Close server
    uv_close((uv_handle_t*)&monitor->tcp_server_, nullptr);
    
    // Close async handles
    uv_close((uv_handle_t*)&monitor->async_stop_, nullptr);
    uv_close((uv_handle_t*)&monitor->async_task_, nullptr);
}

void http_monitor::on_async_task(uv_async_t* handle)
{
    http_monitor* monitor = static_cast<http_monitor*>(handle->data);
    monitor->process_tasks();
}

void http_monitor::on_new_connection(uv_stream_t* server, int status)
{
    if (status < 0) {
        std::cerr << "HTTP new connection error: " << uv_strerror(status) << std::endl;
        return;
    }
    
    http_monitor* monitor = static_cast<http_monitor*>(server->data);
    
    // Allocate new connection
    http_connection* conn = new http_connection();
    uv_tcp_init(&monitor->loop_, &conn->tcp);
    conn->tcp.data = conn;
    conn->monitor = monitor;
    conn->read_buffer = new char[4096];
    conn->parsing_complete = false;
    
    if (uv_accept(server, (uv_stream_t*)&conn->tcp) == 0) {
        uv_read_start((uv_stream_t*)&conn->tcp, alloc_buffer, on_read);
    } else {
        uv_close((uv_handle_t*)&conn->tcp, on_close);
    }
}

void http_monitor::alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    http_connection* conn = static_cast<http_connection*>(handle->data);
    buf->base = conn->read_buffer;
    buf->len = 4096;
}

void http_monitor::on_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf)
{
    http_connection* conn = static_cast<http_connection*>(client->data);
    
    if (nread > 0) {
        // Append to request data
        conn->request_data.append(buf->base, static_cast<size_t>(nread));
        
        // Try to parse HTTP request
        http_parser parser;
        if (parser.parse(conn->request_data.c_str(), conn->request_data.size())) {
            conn->parsing_complete = true;
            
            // Stop reading
            uv_read_stop(client);
            
            // Handle the request
            conn->monitor->handle_request(conn);
        }
    } else if (nread < 0) {
        if (nread != UV_EOF) {
            std::cerr << "HTTP read error: " << uv_strerror(nread) << std::endl;
        }
        conn->monitor->close_connection(conn);
    }
}

void http_monitor::handle_request(http_connection* conn)
{
    // Parse HTTP request to get the path
    http_parser parser;
    parser.parse(conn->request_data.c_str(), conn->request_data.size());
    
    std::string path = parser.path();
    std::string body;
    std::string content_type = "text/html";
    
    // Route based on path
    if (path == "/health") {
        // Health check endpoint - always returns healthy if server is running
        content_type = "application/json";
        std::ostringstream json;
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&time_t_now));
        
        json << "{\n";
        json << "  \"status\": \"healthy\",\n";
        json << "  \"timestamp\": \"" << time_buf << "\",\n";
        json << "  \"uptime_seconds\": " << rttp_server_.get_uptime_seconds() << "\n";
        json << "}";
        body = json.str();
        
    } else if (path == "/ready") {
        // Readiness check - returns ready if server is accepting connections
        content_type = "application/json";
        std::ostringstream json;
        bool is_ready = rttp_server_.is_ready();
        
        json << "{\n";
        json << "  \"status\": \"" << (is_ready ? "ready" : "not_ready") << "\",\n";
        json << "  \"accepting_connections\": " << (is_ready ? "true" : "false") << "\n";
        json << "}";
        body = json.str();
        
    } else if (path == "/metrics") {
        // Prometheus metrics endpoint
        content_type = "text/plain; version=0.0.4";
        body = generate_prometheus_metrics();
        
    } else if (path == "/stats-active") {
        body = stats_mgr_.generate_stats_active_html();
    } else if (path == "/stats-history") {
        body = stats_mgr_.generate_stats_history_html();
    } else if (path == "/stats") {
        // Default stats redirects to active
        body = stats_mgr_.generate_stats_active_html();
    } else if (path == "/connections") {
        body = stats_mgr_.generate_connections_html();
    } else {
        // Default to homepage for "/" or any other path
        body = stats_mgr_.generate_homepage_html();
    }
    
    // Send response
    send_response(conn, body, content_type);
}

void http_monitor::send_response(http_connection* conn, const std::string& body, const std::string& content_type)
{
    // Build HTTP response
    std::ostringstream response;
    response << "HTTP/1.0 200 OK\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;
    
    std::string response_str = response.str();
    
    // Allocate write request and buffer
    write_request* write_req = new write_request();
    write_req->buffer = new char[response_str.size()];
    write_req->conn = conn;
    memcpy(write_req->buffer, response_str.c_str(), response_str.size());
    
    write_req->req.data = write_req;
    
    uv_buf_t buf;
    buf.base = write_req->buffer;
    buf.len = static_cast<unsigned long>(response_str.size());
    
    uv_write(&write_req->req, (uv_stream_t*)&conn->tcp, &buf, 1, on_write);
}

void http_monitor::on_write(uv_write_t* req, int status)
{
    write_request* write_req = static_cast<write_request*>(req->data);
    http_connection* conn = write_req->conn;
    
    // Free the write buffer
    delete[] write_req->buffer;
    delete write_req;
    
    if (status < 0) {
        std::cerr << "HTTP write error: " << uv_strerror(status) << std::endl;
    }
    
    // Close connection after writing
    conn->monitor->close_connection(conn);
}

void http_monitor::close_connection(http_connection* conn)
{
    uv_close((uv_handle_t*)&conn->tcp, on_close);
}

void http_monitor::on_close(uv_handle_t* handle)
{
    http_connection* conn = static_cast<http_connection*>(handle->data);
    delete[] conn->read_buffer;
    delete conn;
}

std::string http_monitor::generate_prometheus_metrics()
{
    std::ostringstream metrics;
    
    // HELP and TYPE declarations
    metrics << "# HELP rttp_active_connections Current number of active connections\n";
    metrics << "# TYPE rttp_active_connections gauge\n";
    metrics << "rttp_active_connections " << stats_mgr_.get_active_connection_count() << "\n\n";
    
    metrics << "# HELP rttp_total_connections Total number of connections in history\n";
    metrics << "# TYPE rttp_total_connections counter\n";
    metrics << "rttp_total_connections " << stats_mgr_.get_history_connection_count() << "\n\n";
    
    // RTT distribution for active connections
    auto rtt_dist = stats_mgr_.calculate_rtt_distribution(true);
    metrics << "# HELP rttp_rtt_distribution_active RTT distribution for active connections\n";
    metrics << "# TYPE rttp_rtt_distribution_active gauge\n";
    for (const auto& bucket : rtt_dist) {
        metrics << "rttp_rtt_distribution_active{range=\"" << bucket.label << "\"} " << bucket.count << "\n";
    }
    metrics << "\n";
    
    // Loss rate distribution for active connections
    auto loss_dist = stats_mgr_.calculate_loss_rate_distribution(true);
    metrics << "# HELP rttp_loss_rate_distribution_active Loss rate distribution for active connections\n";
    metrics << "# TYPE rttp_loss_rate_distribution_active gauge\n";
    for (const auto& bucket : loss_dist) {
        metrics << "rttp_loss_rate_distribution_active{range=\"" << bucket.label << "\"} " << bucket.count << "\n";
    }
    metrics << "\n";
    
    return metrics.str();
}
