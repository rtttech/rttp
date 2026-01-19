#include "rttp_stub.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

using namespace std;

void print_logs() {
    
    char buffer[1024];
    int size = sizeof(buffer);
    int count = 0;
    
    while (true) {
        size = sizeof(buffer);
        int result = rt_stub_read_log(buffer, &size);
        
        if (result > 0) {
            cout << buffer << endl;
            count++;
        } else if (result == 0) {
            break; // No more logs
        } else {
            // Buffer too small, allocate larger buffer
            char* large_buffer = new char[size];
            result = rt_stub_read_log(large_buffer, &size);
            if (result > 0) {
                cout << large_buffer << endl;
                count++;
            }
            delete[] large_buffer;
        }
    }
}

void print_state() {
    cout << "\n=== RTTP Stub State ===" << endl;
    
    // First, query required size
    int size = 0;
    rt_stub_get_state(NULL, &size);
    
    if (size > 0) {
        char* buffer = new char[size];
        int result = rt_stub_get_state(buffer, &size);
        
        if (result > 0) {
            cout << buffer << endl;
        } else {
            cout << "Failed to get state" << endl;
        }
        
        delete[] buffer;
    } else {
        cout << "(No state available)" << endl;
    }
}

int main(int argc, char* argv[]) {
    cout << "RTTP Stub Demo" << endl;
    cout << "==============" << endl;
    
    // Initialize RTTP Stub
    cout << "\n1. Initializing RTTP Stub..." << endl;
    if (rt_stub_init() != 0) {
        cerr << "Failed to initialize RTTP Stub" << endl;
        return 1;
    }
    
    // Add port mappings
    cout << "\n2. Adding port mappings..." << endl;
    cout << "   Mapping 127.0.0.1:8000 -> 192.168.123.200:7777" << endl;
    if (rt_stub_add_map(8000, "192.168.123.200", 7777) != 0) {
        cerr << "Failed to add port mapping" << endl;
        rt_stub_uninit();
        return 1;
    }
    
    cout << "   Mapping 127.0.0.1:8001 -> 192.168.123.200:8888" << endl;
    if (rt_stub_add_map(8001, "192.168.123.200", 9999) != 0) {
        cerr << "Failed to add port mapping" << endl;
        rt_stub_uninit();
        return 1;
    }

    cout << "   Mapping 127.0.0.1:80 -> 65.49.202.23:7777" << endl;
    if (rt_stub_add_map(80, "65.49.202.23", 7777) != 0) {
        cerr << "Failed to add port mapping" << endl;
        rt_stub_uninit();
        return 1;
    }
    
    // Start the stub service
    cout << "\n3. Starting RTTP Stub service..." << endl;
    if (rt_stub_start() != 0) {
        cerr << "Failed to start RTTP Stub" << endl;
        rt_stub_uninit();
        return 1;
    }
    
    // Print initial logs
    print_logs();
    
    // Print initial state
    print_state();
    
    // Run for a while to allow connections
    // Run indefinitely to allow connections
    cout << "\n4. Running indefinitely..." << endl;
    cout << "   You can connect to 127.0.0.1:8000 or 127.0.0.1:8001" << endl;
    cout << "   to test the RTTP stub functionality." << endl;
    cout << "   Press Ctrl+C to stop." << endl;
    
    while (true) {
        this_thread::sleep_for(chrono::seconds(1));
        print_state();
        print_logs();
    }
    
    // Print final state
    cout << "\n5. Final state:" << endl;
    print_state();
    
    // Print all remaining logs
    cout << "\n6. All logs:" << endl;
    rt_stub_clear_logs(); // Clear to reset cursor
    print_logs();
    
    // Stop the service
    cout << "\n7. Stopping RTTP Stub..." << endl;
    rt_stub_stop();
    
    // Uninitialize
    cout << "\n8. Uninitializing RTTP Stub..." << endl;
    rt_stub_uninit();
    
    cout << "\nDemo completed successfully!" << endl;
    return 0;
}
