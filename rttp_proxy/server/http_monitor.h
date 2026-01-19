#pragma once

#include <uv.h>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include "stats_manager.h"
#include <functional>

class rttp_server; // Forward declaration

class http_monitor {
public:
    http_monitor(unsigned short port, stats_manager& stats_mgr, rttp_server& server);
    ~http_monitor();
    
    void start();
    void stop();
    
    // Post a task to be executed on the HTTP monitor thread
    void post_task(std::function<void()> task);

private:
    // HTTP connection structure
    struct http_connection {
        uv_tcp_t tcp;
        http_monitor* monitor;
        char* read_buffer;
        std::string request_data;
        bool parsing_complete;
    };
    
    // Write request structure to hold buffer
    struct write_request {
        uv_write_t req;
        char* buffer;
        http_connection* conn;
    };
    
    // Thread function
    static void thread_func(void* arg);
    
    // libuv callbacks
    static void on_new_connection(uv_stream_t* server, int status);
    static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void on_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
    static void on_write(uv_write_t* req, int status);
    static void on_close(uv_handle_t* handle);
    static void on_async_stop(uv_async_t* handle);
    static void on_async_task(uv_async_t* handle);
    
    // Helper methods
    void handle_request(http_connection* conn);
    void send_response(http_connection* conn, const std::string& body, const std::string& content_type = "text/html");
    void close_connection(http_connection* conn);
    std::string generate_prometheus_metrics();
    void process_tasks();
    
    unsigned short port_;
    stats_manager& stats_mgr_;
    rttp_server& rttp_server_;
    
    // Thread and event loop
    std::thread thread_;
    uv_loop_t loop_;
    uv_tcp_t tcp_server_;
    uv_async_t async_stop_;
    uv_async_t async_task_;
    
    // Task queue
    std::queue<std::function<void()>> task_queue_;
    std::mutex queue_mutex_;
    
    bool running_;
};
