# RTTP Stub Demo

This demo program demonstrates how to use the RTTP Stub API.

## Features Demonstrated

1. **Initialization**: `rt_stub_init()`
2. **Port Mapping**: `rt_stub_add_map()`
3. **Service Control**: `rt_stub_start()` and `rt_stub_stop()`
4. **Logging**: `rt_stub_read_log()` and `rt_stub_clear_logs()`
5. **State Query**: `rt_stub_get_state()`
6. **Cleanup**: `rt_stub_uninit()`

## Building

```bash
cd cpp_demo/rttp_stub_demo
mkdir build
cd build
cmake ..
make
```

## Running

```bash
./rttp_stub_demo
```

The demo will:
1. Initialize the RTTP Stub library
2. Add two port mappings (8080 and 8443)
3. Start the service
4. Run for 10 seconds (you can connect to test)
5. Display logs and connection state
6. Clean up and exit

## Testing

While the demo is running, you can test the RTTP stub by connecting to:
- `127.0.0.1:8080` (forwards to example.com:80)
- `127.0.0.1:8443` (forwards to example.com:443)

For example:
```bash
# In another terminal
curl http://127.0.0.1:8080
```

## API Usage Examples

### Reading Logs
```cpp
char buffer[1024];
int size = sizeof(buffer);
int result = rt_stub_read_log(buffer, &size);

if (result > 0) {
    // Log line read successfully, 'result' is bytes copied
    printf("%s\n", buffer);
} else if (result == 0) {
    // No more logs
} else {
    // Buffer too small, 'size' contains required size
    char* large_buffer = new char[size];
    rt_stub_read_log(large_buffer, &size);
    delete[] large_buffer;
}
```

### Querying State
```cpp
// First, query required size
int size = 0;
rt_stub_get_state(NULL, &size);

// Allocate buffer and get state
char* buffer = new char[size];
rt_stub_get_state(buffer, &size);
printf("%s\n", buffer); // JSON output
delete[] buffer;
```
