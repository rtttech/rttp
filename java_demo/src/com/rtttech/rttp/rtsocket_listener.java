package com.rtttech.rttp;

/**
 * The rtsocket_listener interface defines callback methods for RTTP events.
 */
public interface rtsocket_listener {
    /**
     * Handle socket event.
     * @param engine Engine handle
     * @param socket Socket handle
     * @param event Event type (RTTP_EVENT_CONNECT, RTTP_EVENT_READ, RTTP_EVENT_WRITE, RTTP_EVENT_ERROR)
     */
    void on_socket_event(long engine, long socket, int event);

    /**
     * Handle send data event.
     * @param engine Engine handle
     * @param socket Socket handle
     * @param buffer Data buffer
     * @param len Data length
     * @param addr Address info
     * @param addr_len Address length
     */
    void on_send_data(long engine, long socket, byte[] buffer, int len, byte[] addr, int addr_len);
}
