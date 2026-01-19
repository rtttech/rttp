import Foundation
import Darwin

// RTTP event constants
public let RTTP_EVENT_CONNECT: Int32 = 1
public let RTTP_EVENT_READ: Int32 = 2
public let RTTP_EVENT_WRITE: Int32 = 3
public let RTTP_EVENT_ERROR: Int32 = 4

// Socket mode constants
public let RTSM_LOW_LATENCY: Int32 = 0
public let RTSM_HIGH_THROUGHPUT: Int32 = 1

// Socket option constants
public let RTSO_MTU: Int32 = 0x10001
public let RTSO_FEC: Int32 = 0x10002
public let RTSO_FAST_ACK: Int32 = 0x10003
public let RTSO_RCVBUF: Int32 = 0x10004
public let RTSO_MODE: Int32 = 0x30001

// Info option constants
public let RTSO_RTT: Int32 = 0x20001
public let RTSO_LOST_RATE: Int32 = 0x20002
public let RTSO_RECENT_LOST_RATE: Int32 = 0x20003

// Error constants
public let RTTP_EWOULDBLOCK: Int32 = -1
public let RTTP_EINVAL: Int32 = -100
public let RTTP_ENOTCONN: Int32 = -101
public let RTTP_ECONNABORTED: Int32 = -102
public let RTTP_ETIMEDOUT: Int32 = -103
public let RTTP_ECONNRESET: Int32 = -104
public let RTTP_EINVALID_SOCKET: Int32 = -105
public let RTTP_EINSUFFICIENT_BUFFER: Int32 = -106
public let RTTP_ESTATE_ERROR: Int32 = -107

// MARK: - Type Aliases

public typealias RTEngine = UnsafeMutableRawPointer?
public typealias RTSOCKET = UnsafeMutableRawPointer?

public typealias SocketEventCallback = @convention(c) (RTEngine, RTSOCKET, Int32) -> Void
public typealias SendDataCallback = @convention(c) (RTEngine, RTSOCKET, UnsafePointer<Int8>?, Int32, UnsafePointer<sockaddr>?, Int32) -> Void

// MARK: - C Function Declarations

// Declare the C functions using @_silgen_name attribute
@_silgen_name("rt_init")
func rt_init_c(_ init_str: UnsafePointer<Int8>?) -> RTEngine

@_silgen_name("rt_set_callback")
func rt_set_callback_c(_ engine: RTEngine, _ event_callback: SocketEventCallback?, _ sendproc: SendDataCallback?)

@_silgen_name("rt_uninit")
func rt_uninit_c(_ engine: RTEngine)

@_silgen_name("rt_socket")
func rt_socket_c(_ engine: RTEngine, _ mode: Int32) -> RTSOCKET

@_silgen_name("rt_connect")
func rt_connect_c(_ engine: RTEngine, _ socket: RTSOCKET, _ to: UnsafePointer<sockaddr>, _ tolen: Int32) -> Int32

@_silgen_name("rt_recv")
func rt_recv_c(_ engine: RTEngine, _ socket: RTSOCKET, _ buffer: UnsafeMutablePointer<Int8>, _ len: Int32, _ flag: Int32) -> Int32

@_silgen_name("rt_send")
func rt_send_c(_ engine: RTEngine, _ socket: RTSOCKET, _ buffer: UnsafePointer<Int8>, _ len: Int32, _ flag: Int32) -> Int32

@_silgen_name("rt_close")
func rt_close_c(_ engine: RTEngine, _ socket: RTSOCKET)

@_silgen_name("rt_incoming_packet")
func rt_incoming_packet_c(_ engine: RTEngine, _ buffer: UnsafePointer<Int8>, _ len: Int32, _ from: UnsafePointer<sockaddr>, _ from_len: Int32) -> RTSOCKET

@_silgen_name("rt_accept")
func rt_accept_c(_ engine: RTEngine, _ socket: RTSOCKET) -> Int32

@_silgen_name("rt_tick")
func rt_tick_c(_ engine: RTEngine) -> Int32

@_silgen_name("rt_getpeername")
func rt_getpeername_c(_ engine: RTEngine, _ socket: RTSOCKET, _ name: UnsafeMutablePointer<sockaddr>, _ len: Int32) -> Int32

@_silgen_name("rt_get_error")
func rt_get_error_c(_ engine: RTEngine, _ socket: RTSOCKET) -> Int32

@_silgen_name("rt_connected")
func rt_connected_c(_ engine: RTEngine, _ socket: RTSOCKET) -> Int32

@_silgen_name("rt_writable")
func rt_writable_c(_ engine: RTEngine, _ socket: RTSOCKET) -> Int32

@_silgen_name("rt_readable")
func rt_readable_c(_ engine: RTEngine, _ socket: RTSOCKET) -> Int32

@_silgen_name("rt_setsockopt")
func rt_setsockopt_c(_ engine: RTEngine, _ socket: RTSOCKET, _ optname: Int32, _ optval: UnsafeMutableRawPointer, _ optlen: Int32) -> Int32

@_silgen_name("rt_getsockopt")
func rt_getsockopt_c(_ engine: RTEngine, _ socket: RTSOCKET, _ optname: Int32, _ optval: UnsafeMutableRawPointer, _ optlen: Int32) -> Int32

@_silgen_name("rt_get_userdata")
func rt_get_userdata_c(_ engine: RTEngine, _ socket: RTSOCKET, _ index: Int32) -> UnsafeMutableRawPointer?

@_silgen_name("rt_set_userdata")
func rt_set_userdata_c(_ engine: RTEngine, _ socket: RTSOCKET, _ index: Int32, _ userdata: UnsafeMutableRawPointer?)

@_silgen_name("rt_state_desc")
func rt_state_desc_c(_ engine: RTEngine, _ socket: RTSOCKET, _ desc: UnsafeMutablePointer<Int8>, _ len: Int32) -> Int32

@_silgen_name("rt_get_version")
func rt_get_version_c() -> UnsafePointer<Int8>?

// MARK: - RTTP C API Wrapper

public class RTSocketAPI {
    
    // MARK: - Engine Management
    
    /// Initialize RTTP engine
    public static func rt_init(_ init_str: String?) -> RTEngine {
        if let init_str = init_str {
            return init_str.withCString { cString in
                return rt_init_c(cString)
            }
        } else {
            return rt_init_c(nil)
        }
    }
    
    /// Uninitialize RTTP engine
    public static func rt_uninit(_ engine: RTEngine) {
        guard let engine = engine else { return }
        rt_uninit_c(engine)
    }
    
    /// Get RTTP version string
    public static func rt_get_version() -> String? {
        guard let cString = rt_get_version_c() else { return nil }
        return String(cString: cString)
    }
    
    // MARK: - Callback Management
    
    /// Set callback functions
    public static func rt_set_callback(_ engine: RTEngine, 
                                      event_callback: SocketEventCallback?, 
                                      sendproc: SendDataCallback?) {
        guard let engine = engine else { return }
        rt_set_callback_c(engine, event_callback, sendproc)
    }
    
    // MARK: - Socket Operations
    
    /// Create socket
    public static func rt_socket(_ engine: RTEngine, _ mode: Int32) -> RTSOCKET {
        guard let engine = engine else { return nil }
        return rt_socket_c(engine, mode)
    }
    
    /// Connect to server
    public static func rt_connect(_ engine: RTEngine, _ socket: RTSOCKET, 
                                 _ to: UnsafePointer<sockaddr>, _ tolen: Int32) -> Int32 {
        guard let engine = engine, let socket = socket else { return RTTP_EINVAL }
        return rt_connect_c(engine, socket, to, tolen)
    }
    
    /// Receive data
    public static func rt_recv(_ engine: RTEngine, _ socket: RTSOCKET, 
                              _ buffer: UnsafeMutablePointer<Int8>, _ len: Int32, _ flag: Int32) -> Int32 {
        guard let engine = engine, let socket = socket else { return RTTP_EINVAL }
        return rt_recv_c(engine, socket, buffer, len, flag)
    }
    
    /// Send data
    public static func rt_send(_ engine: RTEngine, _ socket: RTSOCKET, 
                              _ buffer: UnsafePointer<Int8>, _ len: Int32, _ flag: Int32) -> Int32 {
        guard let engine = engine, let socket = socket else { return RTTP_EINVAL }
        return rt_send_c(engine, socket, buffer, len, flag)
    }
    
    /// Close socket
    public static func rt_close(_ engine: RTEngine, _ socket: RTSOCKET) {
        guard let engine = engine, let socket = socket else { return }
        rt_close_c(engine, socket)
    }
    
    // MARK: - Packet Processing
    
    /// Process incoming packet
    public static func rt_incoming_packet(_ engine: RTEngine, 
                                         _ buffer: UnsafePointer<Int8>, _ len: Int32,
                                         _ from: UnsafePointer<sockaddr>, _ from_len: Int32) -> RTSOCKET {
        guard let engine = engine else { return nil }
        return rt_incoming_packet_c(engine, buffer, len, from, from_len)
    }
    
    /// Accept connection
    public static func rt_accept(_ engine: RTEngine, _ socket: RTSOCKET) -> Int32 {
        guard let engine = engine, let socket = socket else { return RTTP_EINVAL }
        return rt_accept_c(engine, socket)
    }
    
    // MARK: - Engine Tick
    
    /// Process engine events
    public static func rt_tick(_ engine: RTEngine) -> Int32 {
        guard let engine = engine else { return RTTP_EINVAL }
        return rt_tick_c(engine)
    }
    
    // MARK: - Socket Information
    
    /// Get peer address
    public static func rt_getpeername(_ engine: RTEngine, _ socket: RTSOCKET,
                                     _ name: UnsafeMutablePointer<sockaddr>, _ len: Int32) -> Int32 {
        guard let engine = engine, let socket = socket else { return RTTP_EINVAL }
        return rt_getpeername_c(engine, socket, name, len)
    }
    
    /// Get error code
    public static func rt_get_error(_ engine: RTEngine, _ socket: RTSOCKET) -> Int32 {
        guard let engine = engine, let socket = socket else { return RTTP_EINVAL }
        return rt_get_error_c(engine, socket)
    }
    
    /// Check if connected
    public static func rt_connected(_ engine: RTEngine, _ socket: RTSOCKET) -> Int32 {
        guard let engine = engine, let socket = socket else { return 0 }
        return rt_connected_c(engine, socket)
    }
    
    /// Check if writable
    public static func rt_writable(_ engine: RTEngine, _ socket: RTSOCKET) -> Int32 {
        guard let engine = engine, let socket = socket else { return 0 }
        return rt_writable_c(engine, socket)
    }
    
    /// Check if readable
    public static func rt_readable(_ engine: RTEngine, _ socket: RTSOCKET) -> Int32 {
        guard let engine = engine, let socket = socket else { return 0 }
        return rt_readable_c(engine, socket)
    }
    
    // MARK: - Socket Options
    
    /// Set socket option
    public static func rt_setsockopt(_ engine: RTEngine, _ socket: RTSOCKET,
                                    _ optname: Int32, _ optval: UnsafeMutableRawPointer, _ optlen: Int32) -> Int32 {
        guard let engine = engine, let socket = socket else { return RTTP_EINVAL }
        return rt_setsockopt_c(engine, socket, optname, optval, optlen)
    }
    
    /// Get socket option
    public static func rt_getsockopt(_ engine: RTEngine, _ socket: RTSOCKET,
                                    _ optname: Int32, _ optval: UnsafeMutableRawPointer, _ optlen: Int32) -> Int32 {
        guard let engine = engine, let socket = socket else { return RTTP_EINVAL }
        return rt_getsockopt_c(engine, socket, optname, optval, optlen)
    }
    
    // MARK: - User Data Management
    
    /// Get user data
    public static func rt_get_userdata(_ engine: RTEngine, _ socket: RTSOCKET, _ index: Int32) -> UnsafeMutableRawPointer? {
        guard let engine = engine else { return nil }
        return rt_get_userdata_c(engine, socket, index)
    }
    
    /// Set user data
    public static func rt_set_userdata(_ engine: RTEngine, _ socket: RTSOCKET,
                                      _ index: Int32, _ userdata: UnsafeMutableRawPointer?) {
        guard let engine = engine else { return }
        rt_set_userdata_c(engine, socket, index, userdata)
    }
    
    // MARK: - State Description
    
    /// Get state description
    public static func rt_state_desc(_ engine: RTEngine, _ socket: RTSOCKET,
                                    _ desc: UnsafeMutablePointer<Int8>, _ len: Int32) -> Int32 {
        guard let engine = engine, let socket = socket else { return RTTP_EINVAL }
        return rt_state_desc_c(engine, socket, desc, len)
    }
    
    // MARK: - Convenience Methods
    
    /// Create socket address from IP and port
    public static func createSocketAddress(ip: String, port: UInt16) -> sockaddr_in? {
        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = port.bigEndian
        
        if inet_pton(AF_INET, ip, &addr.sin_addr) == 1 {
            return addr
        }
        
        return nil
    }
}
