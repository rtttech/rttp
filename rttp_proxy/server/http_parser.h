#pragma once

#include <string>
#include <map>

// Simple HTTP request parser for basic GET requests
class http_parser {
public:
    http_parser();
    
    // Parse incoming data, returns true if request is complete
    bool parse(const char* data, size_t len);
    
    // Check if parsing is complete
    bool is_complete() const { return complete_; }
    
    // Get parsed request information
    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& version() const { return version_; }
    
    // Reset parser for next request
    void reset();
    
private:
    enum State {
        REQUEST_LINE,
        HEADERS,
        COMPLETE
    };
    
    State state_;
    std::string buffer_;
    std::string method_;
    std::string path_;
    std::string version_;
    bool complete_;
    
    bool parse_request_line(const std::string& line);
};
