using System;
using System.Runtime.InteropServices;

namespace rtttech
{
    // 常量定义
    internal static class RttpDllConstants
    {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        public const string DLL_NAME = "RttpDLL.dll";
#elif UNITY_IOS  || UNITY_EDITOR_OSX || UNITY_STANDALONE_OSX
        public const string DLL_NAME = "libRttpDLL.dylib";
#elif UNITY_ANDROID
        public const string DLL_NAME = "libRttpDLL.so";
#else
        public const string DLL_NAME = "RttpDLL";
#endif

        public const CallingConvention CALLING_CONVENTION = CallingConvention.Cdecl;
    }
    // 模式定义
    public class RTSocketConstants
    {
        public const int RTSM_LOW_LATENCY = 0;
        public const int RTSM_HIGH_THROUGHPUT = 1;

        // Socket选项
        public const int RTSO_MTU = 0x10001;
        public const int RTSO_FEC = 0x10002;
        public const int RTSO_FAST_ACK = 0x10003;
        public const int RTSO_RCVBUF = 0x10004;

        // 获取参数
        public const int RTSO_RTT = 0x20001;
        public const int RTSO_LOST_RATE = 0x20002;
        public const int RTSO_RECENT_LOST_RATE = 0x20003;

        // 设置参数
        public const int RTSO_MODE = 0x30001;

        // 事件定义
        public const int RTTP_EVENT_CONNECT = 1;
        public const int RTTP_EVENT_READ = 2;
        public const int RTTP_EVENT_WRITE = 3;
        public const int RTTP_EVENT_ERROR = 4;

        // 错误码
        public const int RTTP_EWOULDBLOCK = -1;
        public const int RTTP_EINVAL = -100;
        public const int RTTP_ENOTCONN = -101;
        public const int RTTP_ECONNABORTED = -102;
        public const int RTTP_ETIMEDOUT = -103;
        public const int RTTP_ECONNRESET = -104;
        public const int RTTP_EINVALID_SOCKET = -105;
        public const int RTTP_EINSUFFICIENT_BUFFER = -106;
        public const int RTTP_ESTATE_ERROR = -107;

        public const int RTTP_INIT_RTTP = -1000;
        public const int RTTP_LOAD_LIB = -1001;
    }

    // Socket事件回调委托
    public delegate void SocketEventCallback(IntPtr engine, IntPtr socket, int eventType);
    public delegate void SendDataCallback(IntPtr engine, IntPtr socket, IntPtr buffer, int len, IntPtr addr, int addrLen);

    public class RTSocketAPI
    {
        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern IntPtr rt_init(string initStr);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern void rt_set_callback(IntPtr engine, SocketEventCallback eventCallback, SendDataCallback sendCallback);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern IntPtr rt_socket(IntPtr engine, int mode);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_connect(IntPtr engine, IntPtr socket, IntPtr to, int tolen);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_recv(IntPtr engine, IntPtr socket, IntPtr buffer, int len, int flag);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_send(IntPtr engine, IntPtr socket, IntPtr buffer, int len, int flag);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern void rt_close(IntPtr engine, IntPtr socket);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern IntPtr rt_incoming_packet(IntPtr engine, IntPtr buffer, int len, IntPtr from, int fromLen);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_accept(IntPtr engine, IntPtr socket);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_tick(IntPtr engine);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_getpeername(IntPtr engine, IntPtr socket, IntPtr name, int len);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_get_error(IntPtr engine, IntPtr socket);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_connected(IntPtr engine, IntPtr socket);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_writable(IntPtr engine, IntPtr socket);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_readable(IntPtr engine, IntPtr socket);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_setsockopt(IntPtr engine, IntPtr socket, int optname, IntPtr optval, int optlen);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_getsockopt(IntPtr engine, IntPtr socket, int optname, IntPtr optval, int optlen);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern IntPtr rt_get_userdata(IntPtr engine, IntPtr socket, int index);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern void rt_set_userdata(IntPtr engine, IntPtr socket, int index, IntPtr userdata);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern int rt_state_desc(IntPtr engine, IntPtr socket, IntPtr desc, int len);

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION, EntryPoint = "rt_get_version")]
        private static extern IntPtr _rt_get_version();

        public static string rt_get_version()
        {
            return Marshal.PtrToStringAnsi(_rt_get_version());
        }

        [DllImport(RttpDllConstants.DLL_NAME, CallingConvention = RttpDllConstants.CALLING_CONVENTION)]
        public static extern void rt_uninit(IntPtr engine);
    }
}