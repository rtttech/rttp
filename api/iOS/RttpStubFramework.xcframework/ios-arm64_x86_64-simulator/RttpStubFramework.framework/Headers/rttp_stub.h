#ifndef RTTP_STUB_H
#define RTTP_STUB_H

#if defined(RT_STUB_EXPORTS)
    #if defined(_MSC_VER)
        #define RT_STUB_API __declspec(dllexport)
    #else
        #define RT_STUB_API __attribute__((visibility("default")))
    #endif
#else
    #define RT_STUB_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Initialize the RTTP Stub library.
     * This initializes the underlying RTTP engine and network resources.
     * @return 0 on success, non-zero on failure.
     */
    RT_STUB_API int rt_stub_init();

    /**
     * Add a port mapping.
     * The stub will listen on 127.0.0.1:local_port.
     * Connections to this port will be forwarded to remote_host:remote_port via RTTP.
     * @param local_port The local TCP port to listen on.
     * @param remote_host The remote host address (IP or hostname) of the destination.
     * @param remote_port The remote port of the destination.
     * @return 0 on success, non-zero on failure.
     */
    RT_STUB_API int rt_stub_add_map(unsigned short local_port, const char* remote_host, unsigned short remote_port);

    /**
     * Start the stub service.
     * This starts the background thread that handles network events.
     * @return 0 on success, non-zero on failure.
     */
    RT_STUB_API int rt_stub_start();

    /**
     * Stop the stub service.
     * This stops the background thread and closes all connections.
     * @return 0 on success, non-zero on failure.
     */
    RT_STUB_API int rt_stub_stop();

    /**
     * Uninitialize the RTTP Stub library.
     * Releases all resources.
     */
    RT_STUB_API void rt_stub_uninit();

    /**
     * Read one log line from the circular log buffer.
     * @param buffer Buffer to store the log line. Can be NULL to query required size.
     * @param buffer_size Pointer to buffer size. On input: size of buffer. On output: actual bytes copied on success, or required size if buffer is too small.
     * @return Number of bytes copied (excluding null terminator) on success, 0 if no more logs available, -1 if buffer too small (required size returned in buffer_size).
     */
    RT_STUB_API int rt_stub_read_log(char* buffer, int* buffer_size);

    /**
     * Clear all logs from the circular log buffer.
     */
    RT_STUB_API void rt_stub_clear_logs();

    /**
     * Get connection states as JSON string (active + historical).
     * @param buffer Buffer to store the JSON string. Can be NULL to query required size.
     * @param buffer_size Pointer to buffer size. On input: size of buffer. On output: actual bytes copied on success, or required size if buffer is too small.
     * @return Number of bytes written (excluding null terminator) on success, -1 if buffer too small (required size returned in buffer_size).
     */
    RT_STUB_API int rt_stub_get_state(char* buffer, int* buffer_size);

#ifdef __cplusplus
}
#endif

#endif // RTTP_STUB_H
