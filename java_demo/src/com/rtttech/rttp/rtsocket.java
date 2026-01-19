package com.rtttech.rttp;

/**
 * The rtsocket class is the Java interface for the RTTP library,
 * mapping directly to the JNI functions defined in jni_rtsocket.h.
 */
public class rtsocket {
    // Load JNI library
    static {
        System.loadLibrary("JniRttpDLL");
    }

    // Event constants
    public static final int RTTP_EVENT_CONNECT = 1;
    public static final int RTTP_EVENT_READ = 2;
    public static final int RTTP_EVENT_WRITE = 3;
    public static final int RTTP_EVENT_ERROR = 4;

    /**
     * Set callback object.
     * @param engine Engine handle
     * @param listener Callback listener object
     */
    public static native void rt_set_callback(long engine, rtsocket_listener listener);

    /**
     * Unset callback object.
     * @param engine Engine handle
     */
    public static native void rt_unset_callback(long engine);

    /**
     * Initialize RTTP engine.
     * @param config Configuration string
     * @return Engine handle
     */
    public static native long rt_init(String config);

    /**
     * Create socket.
     * @param engine Engine handle
     * @param type Socket type
     * @return Socket handle
     */
    public static native long rt_socket(long engine, int type);

    /**
     * Connect to server.
     * @param engine Engine handle
     * @param socket Socket handle
     * @param addr Address byte array
     * @param addr_len Address length
     * @return Connection result, 0 means success
     */
    public static native int rt_connect(long engine, long socket, byte[] addr, int addr_len);

    /**
     * Receive data.
     * @param engine Engine handle
     * @param socket Socket handle
     * @param buffer 接收数据的buffer
     * @param len 本次要接收的字节数
     * @param flag 标志位，暂未使用，必须填0
     * @return Number of bytes received, -1 means failure
     */
    public static native int rt_recv(long engine, long socket, byte[] buffer, int len, int flag);

    /**
     * Send data.
     * @param engine Engine handle
     * @param socket Socket handle
     * @param buffer 要发送的数据buffer
     * @param len 本次要发送的字节数
     * @param flag 标志位，暂未使用，必须填0
     * @return Number of bytes sent, -1 means failure
     */
    public static native int rt_send(long engine, long socket, byte[] buffer, int len, int flag);

    /**
     * Close socket.
     * @param engine Engine handle
     * @param socket Socket handle
     */
    public static native void rt_close(long engine, long socket);

    /**
     * Process incoming packet.
     * @param engine Engine handle
     * @param data Data byte array
     * @param len Data length
     * @param addr Address byte array
     * @param addr_len Address length
     * @return Result
     */
    public static native long rt_incoming_packet(long engine, byte[] data, int len, byte[] addr, int addr_len);

    /**
     * Process engine event.
     * @param engine Engine handle
     * @return Result
     */
    public static native int rt_tick(long engine);

    /**
     * Get peer address.
     * @param engine Engine handle
     * @param socket Socket handle
     * @param addr Address buffer
     * @param addr_len Address length
     * @return Result
     */
    public static native int rt_getpeername(long engine, long socket, byte[] addr, int addr_len);

    /**
     * Get error code.
     * @param engine Engine handle
     * @param socket Socket handle
     * @return Error code
     */
    public static native int rt_get_error(long engine, long socket);

    /**
     * Check if connected.
     * @param engine Engine handle
     * @param socket Socket handle
     * @return Connection status, 0 means not connected
     */
    public static native int rt_connected(long engine, long socket);

    /**
     * Check if writable.
     * @param engine Engine handle
     * @param socket Socket handle
     * @return Writable status, 0 means not writable
     */
    public static native int rt_writable(long engine, long socket);

    /**
     * Check if readable.
     * @param engine Engine handle
     * @param socket Socket handle
     * @return Readable status, 0 means not readable
     */
    public static native int rt_readable(long engine, long socket);

    /**
     * Accept connection.
     * @param engine Engine handle
     * @param socket Listening socket handle
     * @return Accept result
     */
    public static native int rt_accept(long engine, long socket);

    /**
     * Set socket option.
     * @param engine Engine handle
     * @param socket Socket handle
     * @param opt Option type
     * @param val Value buffer
     * @param len Value length
     * @return Set result
     */
    public static native int rt_setsockopt(long engine, long socket, int opt, byte[] val, int len);

    /**
     * Get socket option.
     * @param engine Engine handle
     * @param socket Socket handle
     * @param opt Option type
     * @param val Value buffer
     * @param len Value length
     * @return Get result
     */
    public static native int rt_getsockopt(long engine, long socket, int opt, byte[] val, int len);

    /**
     * Get user data.
     * @param engine Engine handle
     * @param socket Socket handle
     * @param index User data index
     * @return User data value
     */
    public static native long rt_get_userdata(long engine, long socket, int index);

    /**
     * Set user data.
     * @param engine Engine handle
     * @param socket Socket handle
     * @param index User data index
     * @param value User data value
     */
    public static native void rt_set_userdata(long engine, long socket, int index, long value);

    /**
     * Get state description.
     * @param engine Engine handle
     * @param socket Socket handle
     * @param buffer Buffer
     * @param len Buffer length
     * @return State description length
     */
    public static native int rt_state_desc(long engine, long socket, byte[] buffer, int len);

    /**
     * Get RTTP version string.
     * @return Version string
     */
    public static native String rt_get_version();

    /**
     * Uninitialize RTTP engine.
     * @param engine Engine handle
     */
    public static native void rt_uninit(long engine);
}
