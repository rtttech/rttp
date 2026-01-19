using System;
using System.Runtime.InteropServices;

namespace rtttech
{
    // 常量定义
    internal static class RtStubDllConstants
    {
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        public const string DLL_NAME = "RttpStubDLL.dll";
#elif UNITY_IOS || UNITY_EDITOR_OSX || UNITY_STANDALONE_OSX
        public const string DLL_NAME = "libRttpStubDLL.dylib";
#elif UNITY_ANDROID
        public const string DLL_NAME = "libRttpStubDLL.so";
#else
        public const string DLL_NAME = "libRttpStubDLL";
#endif

        public const CallingConvention CALLING_CONVENTION = CallingConvention.Cdecl;
    }

    public class RTStubAPI
    {
        /// <summary>
        /// Initialize the RTTP Stub library.
        /// This initializes the underlying RTTP engine and network resources.
        /// </summary>
        /// <returns>0 on success, non-zero on failure.</returns>
        [DllImport(RtStubDllConstants.DLL_NAME, CallingConvention = RtStubDllConstants.CALLING_CONVENTION)]
        public static extern int rt_stub_init();

        /// <summary>
        /// Add a port mapping.
        /// The stub will listen on 127.0.0.1:local_port.
        /// Connections to this port will be forwarded to remote_host:remote_port via RTTP.
        /// </summary>
        /// <param name="local_port">The local TCP port to listen on.</param>
        /// <param name="remote_host">The remote host address (IP or hostname) of the destination.</param>
        /// <param name="remote_port">The remote port of the destination.</param>
        /// <returns>0 on success, non-zero on failure.</returns>
        [DllImport(RtStubDllConstants.DLL_NAME, CallingConvention = RtStubDllConstants.CALLING_CONVENTION)]
        public static extern int rt_stub_add_map(ushort local_port, string remote_host, ushort remote_port);

        /// <summary>
        /// Start the stub service.
        /// This starts the background thread that handles network events.
        /// </summary>
        /// <returns>0 on success, non-zero on failure.</returns>
        [DllImport(RtStubDllConstants.DLL_NAME, CallingConvention = RtStubDllConstants.CALLING_CONVENTION)]
        public static extern int rt_stub_start();

        /// <summary>
        /// Stop the stub service.
        /// This stops the background thread and closes all connections.
        /// </summary>
        /// <returns>0 on success, non-zero on failure.</returns>
        [DllImport(RtStubDllConstants.DLL_NAME, CallingConvention = RtStubDllConstants.CALLING_CONVENTION)]
        public static extern int rt_stub_stop();

        /// <summary>
        /// Uninitialize the RTTP Stub library.
        /// Releases all resources.
        /// </summary>
        [DllImport(RtStubDllConstants.DLL_NAME, CallingConvention = RtStubDllConstants.CALLING_CONVENTION)]
        public static extern void rt_stub_uninit();

        /// <summary>
        /// Read one log line from the circular log buffer.
        /// </summary>
        /// <param name="buffer">Buffer to store the log line. Can be IntPtr.Zero to query required size.</param>
        /// <param name="buffer_size">Pointer to buffer size. On input: size of buffer. On output: actual bytes copied on success, or required size if buffer is too small.</param>
        /// <returns>Number of bytes copied (excluding null terminator) on success, 0 if no more logs available, -1 if buffer too small (required size returned in buffer_size).</returns>
        [DllImport(RtStubDllConstants.DLL_NAME, CallingConvention = RtStubDllConstants.CALLING_CONVENTION)]
        public static extern int rt_stub_read_log(IntPtr buffer, ref int buffer_size);

        /// <summary>
        /// Clear all logs from the circular log buffer.
        /// </summary>
        [DllImport(RtStubDllConstants.DLL_NAME, CallingConvention = RtStubDllConstants.CALLING_CONVENTION)]
        public static extern void rt_stub_clear_logs();

        /// <summary>
        /// Get connection states as JSON string (active + historical).
        /// </summary>
        /// <param name="buffer">Buffer to store the JSON string. Can be IntPtr.Zero to query required size.</param>
        /// <param name="buffer_size">Pointer to buffer size. On input: size of buffer. On output: actual bytes copied on success, or required size if buffer is too small.</param>
        /// <returns>Number of bytes written (excluding null terminator) on success, -1 if buffer too small (required size returned in buffer_size).</returns>
        [DllImport(RtStubDllConstants.DLL_NAME, CallingConvention = RtStubDllConstants.CALLING_CONVENTION)]
        public static extern int rt_stub_get_state(IntPtr buffer, ref int buffer_size);

        // 辅助方法，用于简化API调用
        public static string GetState()
        {
            // 先查询所需的缓冲区大小
            int bufferSize = 0;
            int result = rt_stub_get_state(IntPtr.Zero, ref bufferSize);
            if (result == -1 && bufferSize > 0)
            {
                // 分配足够的缓冲区
                IntPtr buffer = Marshal.AllocHGlobal(bufferSize);
                try
                {
                    // 获取状态信息
                    result = rt_stub_get_state(buffer, ref bufferSize);
                    if (result > 0)
                    {
                        // 转换为字符串
                        return Marshal.PtrToStringAnsi(buffer, result);
                    }
                }
                finally
                {
                    // 释放缓冲区
                    Marshal.FreeHGlobal(buffer);
                }
            }
            return null;
        }

        public static string ReadLog()
        {
            // 先查询所需的缓冲区大小
            int bufferSize = 0;
            int result = rt_stub_read_log(IntPtr.Zero, ref bufferSize);
            if (result == -1 && bufferSize > 0)
            {
                // 分配足够的缓冲区
                IntPtr buffer = Marshal.AllocHGlobal(bufferSize);
                try
                {
                    // 读取日志
                    result = rt_stub_read_log(buffer, ref bufferSize);
                    if (result > 0)
                    {
                        // 转换为字符串
                        return Marshal.PtrToStringAnsi(buffer, result);
                    }
                }
                finally
                {
                    // 释放缓冲区
                    Marshal.FreeHGlobal(buffer);
                }
            }
            return null;
        }
    }
}