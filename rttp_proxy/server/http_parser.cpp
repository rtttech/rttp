#include "http_parser.h"
#include <sstream>
#include <algorithm>

http_parser::http_parser()
    : state_(REQUEST_LINE), complete_(false)
{
}

bool http_parser::parse(const char* data, size_t len)
{
    buffer_.append(data, len);
    
    while (!complete_) {
        // Find end of line
        size_t pos = buffer_.find("\r\n");
        if (pos == std::string::npos) {
            // Need more data
            return false;
        }
        
        std::string line = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 2);
        
        if (state_ == REQUEST_LINE) {
            if (!parse_request_line(line)) {
                return false;
            }
            state_ = HEADERS;
        }
        else if (state_ == HEADERS) {
            if (line.empty()) {
                // Empty line marks end of headers
                state_ = COMPLETE;
                complete_ = true;
                return true;
            }
            // We don't need to parse headers for our simple use case
        }
    }
    
    return complete_;
}

bool http_parser::parse_request_line(const std::string& line)
{
    std::istringstream iss(line);
    if (!(iss >> method_ >> path_ >> version_)) {
        return false;
    }
    
    // Convert method to uppercase for consistency
    std::transform(method_.begin(), method_.end(), method_.begin(), ::toupper);
    
    return true;
}

void http_parser::reset()
{
    state_ = REQUEST_LINE;
    buffer_.clear();
    method_.clear();
    path_.clear();
    version_.clear();
    complete_ = false;
}
