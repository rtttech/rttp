using UnityEngine;
using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using rtttech;
using RTSOCKET = System.IntPtr;
using RTEngine = System.IntPtr;
using System.Collections.Concurrent;
using System.Threading.Tasks;
using System.Diagnostics; // For Stopwatch

namespace RttpDemo
{
    // Platform-specific sockaddr_in layout:
// - BSD/macOS/iOS include a leading sin_len (1 byte) and sin_family is 1 byte.
// - Linux/Windows/Android use 2-byte sin_family and no sin_len field.

// Platform-specific sockaddr_in layout for P/Invoke calls
// - BSD/macOS/iOS include a leading sin_len (1 byte) and sin_family is 1 byte.
// - Linux/Windows/Android use 2-byte sin_family and no sin_len field.
// Note: This struct is only used for P/Invoke marshaling, not for Unity serialization.
#if UNITY_IOS || UNITY_STANDALONE_OSX || (UNITY_EDITOR_OSX && !UNITY_ANDROID)
    // BSD-style layout for iOS/OSX (with sin_len)
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    internal struct sockaddr_in
    {
        public byte sin_len;
        public byte sin_family;
        public ushort sin_port;
        public uint sin_addr;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
        public byte[] sin_zero;

        public static sockaddr_in Create(IPEndPoint endPoint)
        {
            sockaddr_in addr = new sockaddr_in
            {
                sin_len = (byte)Marshal.SizeOf(typeof(sockaddr_in)),
                sin_family = 2, // AF_INET
                sin_port = (ushort)IPAddress.HostToNetworkOrder((short)endPoint.Port),
                sin_zero = new byte[8]
            };
            addr.sin_addr = BitConverter.ToUInt32(endPoint.Address.GetAddressBytes(), 0);
            return addr;
        }
    }
#else
    // Linux-style layout for Android/Windows/Linux and Android builds in editor
    [StructLayout(LayoutKind.Sequential)]
    internal struct sockaddr_in
    {
        public ushort sin_family;
        public ushort sin_port;
        public uint sin_addr;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
        public byte[] sin_zero;

        public static sockaddr_in Create(IPEndPoint endPoint)
        {
            sockaddr_in addr = new sockaddr_in
            {
                sin_family = (ushort)2, // AF_INET
                sin_port = (ushort)IPAddress.HostToNetworkOrder((short)endPoint.Port),
                sin_zero = new byte[8]
            };
            addr.sin_addr = BitConverter.ToUInt32(endPoint.Address.GetAddressBytes(), 0);
            return addr;
        }
    }
#endif

    public class RttpClientConnection : BaseClientConnection
    {
        
        private Socket _udpSocket;
        private int _udpPort = 0; // UDP监听端口
        
        // Control socket for waking up the selector (simulating Java's Selector.wakeup())
        private Socket _controlSocket;
        private Socket _wakeupSender;
        private IPEndPoint _controlEndPoint;

        private readonly ConcurrentQueue<Func<int>> _apiCallQueue = new ConcurrentQueue<Func<int>>();
        
        private static SocketEventCallback _eventCallback;
        private static SendDataCallback _sendCallback;

        private bool _tickRunning = false;
        private Task _tickTask;
        private RTSOCKET _rttpSocket = (RTSOCKET)0;
        private RTEngine _rttpEngine = (RTEngine)0;
        private string _status = "idle";
        private const int MaxPacketSize = 2000; // 最大UDP数据包大小

        // 发送任务队列（类似Java版本的SendTask）
        private Queue<SendTask> _sendQueue = new Queue<SendTask>();
    
        // 发送任务结构（类似Java版本的SendTask）
        private class SendTask
        {
            public byte[] data;
            public int length;
            public int sentBytes = 0; // 当前已发送字节数
            public SendTask(byte[] data, int length)
            {
                this.data = data;
                this.length = length;
            }
        }
    
        // 接收任务队列
        private Queue<ReceiveTask> _receiveQueue = new Queue<ReceiveTask>();
    
        // 接收任务结构
        private class ReceiveTask
        {
            public int maxLen;
            public ReceiveTask(int maxLen)
            {
                this.maxLen = maxLen;
            }
        }
        
        public RttpClientConnection()
        {
            StartTickLoop();
        }

        ~RttpClientConnection()
        {
            // 析构时清理资源
            Shutdown();
        }

        public override void Connect(string host, int port)
        {
            State = ConnectionState.Connecting;
            Func<int> connectAction = () => {
                return DoConnect(host, port);
            };
            _apiCallQueue.Enqueue(connectAction);
            Wakeup();
        }

        public override void Close()
        {
            State = ConnectionState.Disconnected;
            Func<int> closeAction = () => {
                return DoClose();
            };
            _apiCallQueue.Enqueue(closeAction);
            Wakeup();
        }

        public override void Send(byte[] data)
        {
            if (data == null || data.Length == 0)
                return;

            Func<int> sendAction = () => {
                _sendQueue.Enqueue(new SendTask(data, data.Length));
                ProcessSendQueue();
                return 0;
            };
            _apiCallQueue.Enqueue(sendAction);
            Wakeup();
        }

        // 主动接收数据API（类似Java版本的receiveAsync）
        public void Receive(int maxLen)
        {
            if (maxLen <= 0)
                return;

            Func<int> receiveAction = () => {
                _receiveQueue.Enqueue(new ReceiveTask(maxLen));
                ProcessReceiveQueue();
                return 0;
            };
            _apiCallQueue.Enqueue(receiveAction);
            Wakeup();
        }
        
        private void Wakeup()
        {
            if (_wakeupSender != null && _controlEndPoint != null)
            {
                try
                {
                    // Send a single byte to wake up the selector
                    _wakeupSender.SendTo(new byte[] { 1 }, _controlEndPoint);
                }
                catch { }
            }
        }

        // 启动tick循环
        private void StartTickLoop()
        {
            if (!_tickRunning)
            {
                _tickRunning = true;
                _tickTask = Task.Run(() => TickLoop());
            }
        }

        [AOT.MonoPInvokeCallback(typeof(rtttech.SocketEventCallback))]
        public static void CallbackOnRttpSocketEvent(RTEngine engine, RTSOCKET socket, int evt)
        {
            IntPtr ptr = RTSocketAPI.rt_get_userdata(engine, socket, 0);
            if (ptr == IntPtr.Zero)
                return;
                
            GCHandle handle;
            try
            {
                handle = GCHandle.FromIntPtr(ptr);
                if (!handle.IsAllocated)
                    return;
                    
                RttpClientConnection rttpConn = handle.Target as RttpClientConnection;
                if (rttpConn == null)
                    return;
                    
                if (evt == RTSocketConstants.RTTP_EVENT_READ)
                {
                    rttpConn.OnRead(engine, socket);
                }
                else if(evt == RTSocketConstants.RTTP_EVENT_WRITE)
                {
                    rttpConn.OnWrite(engine, socket);
                }
                else if(evt == RTSocketConstants.RTTP_EVENT_CONNECT)
                {
                    rttpConn.OnConnected(engine, socket);
                }
                else if (evt == RTSocketConstants.RTTP_EVENT_ERROR)
                {
                    int err = RTSocketAPI.rt_get_error(engine, socket);
                    rttpConn.OnError(engine, socket, err);
                }
            }
            catch
            {
                // 忽略无效的指针或已释放的GCHandle
            }
        }

        // 移除不必要的new关键字
        public void OnConnected(RTEngine engine, RTSOCKET socket)
        {
            if (socket != _rttpSocket)
                return;

            _status = "connected";
            UnityEngine.Debug.Log("Connect server success");

            NotifyConnected();
        }

        public void OnRead(RTEngine engine, RTSOCKET socket)
        {
            if (socket != _rttpSocket)
                return;

            // 当有数据可读时，处理接收队列
            ProcessReceiveQueue();
        }

        public void OnWrite(RTEngine engine, RTSOCKET socket)    
        {
            if (socket != _rttpSocket)
                return;

            ProcessSendQueue();
        }


        // 移除不必要的new关键字
        public void OnError(RTEngine engine, RTSOCKET socket, int errcode)
        {
            if (socket != _rttpSocket)
                return;

            _status = string.Format("Error:{0}", errcode);
            // 使用基类的通知方法
            NotifyError(errcode);

            UnityEngine.Debug.Log(_status);
        }

        // 处理接收队列（类似Java版本的processReceiveQueue）
        private void ProcessReceiveQueue()
        {
            if (_rttpSocket == (RTSOCKET)0 || _receiveQueue.Count == 0)
                return;

            // 检查socket是否可读
            if (RTSocketAPI.rt_readable(_rttpEngine, _rttpSocket) <= 0)
                return;

            while (_receiveQueue.Count > 0)
            {
                ReceiveTask task = _receiveQueue.Peek();
                if (task == null)
                {
                    _receiveQueue.Dequeue();
                    continue;
                }

                // 接收数据
                int ret = 0;
                IntPtr bufferPtr = IntPtr.Zero;
                try
                {
                    bufferPtr = Marshal.AllocHGlobal(task.maxLen);
                    ret = RTSocketAPI.rt_recv(_rttpEngine, _rttpSocket, bufferPtr, task.maxLen, 0);
                    
                    if (ret > 0)
                    {
                        // 成功接收到数据
                        byte[] buff = new byte[ret];
                        Marshal.Copy(bufferPtr, buff, 0, ret);
                        NotifyDataReceived(buff);
                        _receiveQueue.Dequeue(); // 任务完成，移除队列
                    }
                    else if (ret == -1)
                    {
                        // 没有数据可读，等待下次READ事件
                        break;
                    }
                    else
                    {
                        // 接收错误，移除任务
                        _receiveQueue.Dequeue();
                        _status = string.Format("Receive Error:{0}", ret);
                        UnityEngine.Debug.Log(_status);
                    }
                }
                catch (Exception ex)
                {
                    UnityEngine.Debug.LogError("ProcessReceiveQueue error: " + ex.Message);
                    _receiveQueue.Dequeue();
                }
                finally
                {
                    if (bufferPtr != IntPtr.Zero)
                    {
                        Marshal.FreeHGlobal(bufferPtr);
                    }
                }
            }
        }

        // 实际处理发送队列（类似Java版本的processSendQueue）
        private void ProcessSendQueue()
        {
            if (_rttpSocket == (RTSOCKET)0)
                return;

            while (_sendQueue.Count > 0 && RTSocketAPI.rt_writable(_rttpEngine, _rttpSocket) > 0)
            {
                SendTask task = _sendQueue.Peek();
                if (task == null) 
                {
                    _sendQueue.Dequeue();
                    continue;
                }

                while (task.sentBytes < task.length)
                {
                    int willSend = task.length - task.sentBytes;
                    // rt_send参数: buffer, len, flag=0
                    // 注意：需要从已发送位置开始发送数据，创建子数组
                    byte[] sendBuffer = new byte[willSend];
                    Array.Copy(task.data, task.sentBytes, sendBuffer, 0, willSend);
                    
                    IntPtr bufferPtr = Marshal.AllocHGlobal(willSend);
                    try
                    {
                        Marshal.Copy(sendBuffer, 0, bufferPtr, willSend);
                        int ret = RTSocketAPI.rt_send(_rttpEngine, _rttpSocket, bufferPtr, willSend, 0);
                        UnityEngine.Debug.LogFormat("rt_send data len: {0}, already sent: {1}, cur send: {2}, send return: {3}",
                            task.length, task.sentBytes, willSend, ret);
                        
                        if (ret > 0)
                        {
                            task.sentBytes += ret;
                        }
                        else if (ret == -1)
                        {
                            // 不可写，等待下次WRITE事件
                            return;
                        }
                        else
                        {
                            // 发送错误，通知并丢弃
                            NotifyDataSent(task.data, task.length, ret);
                            _sendQueue.Dequeue();
                            break;
                        }
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(bufferPtr);
                    }
                }
                
                // 单个SendTask数据全部发完才通知
                if (task.sentBytes == task.length)
                {
                    NotifyDataSent(task.data, task.length, task.length);
                    _sendQueue.Dequeue();
                }
            }
        }


        [AOT.MonoPInvokeCallback(typeof(rtttech.SendDataCallback))]
        public static void CallbackRttpSendDataImp(RTEngine engine, RTSOCKET socket, IntPtr buffer, int len, IntPtr addr, int addrLen)
        {
            IntPtr ptr = RTSocketAPI.rt_get_userdata(engine, socket, 0);
            if (ptr == IntPtr.Zero)
                return;
                
            GCHandle handle;
            try
            {
                handle = GCHandle.FromIntPtr(ptr);
                if (!handle.IsAllocated)
                    return;
                    
                RttpClientConnection rttpConn = handle.Target as RttpClientConnection;
                if (rttpConn == null)
                    return;
                    
                // 使用_udpSocket发送数据
                if (rttpConn._udpSocket != null && addr != IntPtr.Zero && addrLen > 0 && buffer != IntPtr.Zero && len > 0)
                {
                    try
                    {
                        // 解析地址信息
                        sockaddr_in sockAddr = Marshal.PtrToStructure<sockaddr_in>(addr);
                        byte[] addressBytes = BitConverter.GetBytes(sockAddr.sin_addr);
                        IPAddress ipAddress = new IPAddress(addressBytes);
                        int port = IPAddress.NetworkToHostOrder((short)sockAddr.sin_port);
                        IPEndPoint endPoint = new IPEndPoint(ipAddress, port);
                        
                        // 复制缓冲区数据到托管数组
                        byte[] data = new byte[len];
                        Marshal.Copy(buffer, data, 0, len);
                        
                        // 使用UDP socket发送数据
                        int sent = rttpConn._udpSocket.SendTo(data, 0, len, SocketFlags.None, endPoint);
                    }
                    catch (Exception)
                    {
                        // 忽略发送错误
                    }
                }
                
            }
            catch
            {
                // 忽略无效的指针或已释放的GCHandle
            }
        }

        private void InitializeUdpSocket()
        {
            try
            {
                // 创建UDP socket
                _udpSocket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                // 绑定到任意可用端口
                _udpSocket.Bind(new IPEndPoint(IPAddress.Any, 0));
                // 获取实际绑定的端口号
                _udpPort = ((IPEndPoint)_udpSocket.LocalEndPoint).Port;
                // 设置为非阻塞模式
                _udpSocket.Blocking = false;
                
                // Initialize control sockets for wakeup mechanism
                _controlSocket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                _controlSocket.Bind(new IPEndPoint(IPAddress.Loopback, 0));
                _controlEndPoint = (IPEndPoint)_controlSocket.LocalEndPoint;
                _controlSocket.Blocking = false;
                
                _wakeupSender = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
            }
            catch (Exception)
            {
                // 如果初始化失败，确保释放资源
                DisposeUdpSocket();
                throw new InvalidOperationException("Failed to initialize UDP socket");
            }
        }

        private void DisposeUdpSocket()
        {
            if (_udpSocket != null)
            {
                try
                {
                    _udpSocket.Close();
                } catch { }
                _udpSocket = null;
                _udpPort = 0;
            }
            
            if (_controlSocket != null)
            {
                try { _controlSocket.Close(); } catch { }
                _controlSocket = null;
            }
            
            if (_wakeupSender != null)
            {
                try { _wakeupSender.Close(); } catch { }
                _wakeupSender = null;
            }
        }

        private void InitializeRttpEngine()
        {
            try
            {
                
                _rttpEngine = RTSocketAPI.rt_init("");
                if (_rttpEngine == IntPtr.Zero)
                {
                    UnityEngine.Debug.LogError("Failed to initialize RTTP engine");
                    NotifyError(RTSocketConstants.RTTP_INIT_RTTP);
                    return;
                }
                _eventCallback = CallbackOnRttpSocketEvent;
                _sendCallback = CallbackRttpSendDataImp;
                RTSocketAPI.rt_set_callback(_rttpEngine, _eventCallback, _sendCallback);
            }
            catch (Exception ex)
            {
                UnityEngine.Debug.LogError("Initialize error: " + ex.Message);
                NotifyError(RTSocketConstants.RTTP_LOAD_LIB);
            }
        }

        private void DisposeRttpEngine()
        {
            if (_rttpEngine != IntPtr.Zero)
            {
                RTSocketAPI.rt_uninit(_rttpEngine);
                _rttpEngine = IntPtr.Zero;
            }
        }

        private int DoConnect(string host, int port)
        {
            IPAddress address = IPAddress.Parse(host);
            IPEndPoint endPoint = new IPEndPoint(address, port);
            sockaddr_in addr = sockaddr_in.Create(endPoint);
            int size = Marshal.SizeOf(addr);

            IntPtr addrPtr = IntPtr.Zero;

            if (_rttpSocket != (RTSOCKET)0)
            {
                RTSocketAPI.rt_close(_rttpEngine, _rttpSocket);
            }
            _rttpSocket = RTSocketAPI.rt_socket(_rttpEngine, RTSocketConstants.RTSM_LOW_LATENCY);
            if (_rttpSocket == (RTSOCKET)0)
            {
                throw new InvalidOperationException("Failed to create RttpClientSocket");
            }

            // 将当前实例指针存储到socket的用户数据中
            GCHandle handle = GCHandle.Alloc(this);
            IntPtr ptr = GCHandle.ToIntPtr(handle);
            RTSocketAPI.rt_set_userdata(_rttpEngine, _rttpSocket, 0, ptr);

            int ret = 0;
            try
            {
                addrPtr = Marshal.AllocHGlobal(size);
                Marshal.StructureToPtr(addr, addrPtr, false);
                ret = RTSocketAPI.rt_connect(_rttpEngine, _rttpSocket, addrPtr, size);   
            }
            catch (Exception ex)
            {
                UnityEngine.Debug.LogError("Connect error: " + ex.Message);
                ret = -1;
                NotifyError(-1);
            }
            finally
            {
                if (addrPtr != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(addrPtr);
                }
            }
            return ret;
        }

        private int DoClose()
        {
            try
            {
                if (_rttpSocket != (RTSOCKET)0)
                {
                    // 释放GCHandle
                    IntPtr ptr = RTSocketAPI.rt_get_userdata(_rttpEngine, _rttpSocket, 0);
                    if (ptr != IntPtr.Zero)
                    {
                        try
                        {
                            GCHandle handle = GCHandle.FromIntPtr(ptr);
                            if (handle.IsAllocated)
                            {
                                handle.Free();
                            }
                        }
                        catch { }
                    }
                    
                    RTSocketAPI.rt_close(_rttpEngine, _rttpSocket);
                    _rttpSocket = (RTSOCKET)0;
                }

                // 清理队列和状态
                _sendQueue.Clear();
                _receiveQueue.Clear();
                
                _status = "disconnected";
            }
            catch (Exception ex)
            {
                UnityEngine.Debug.LogError("Disconnect error: " + ex.Message);
            }

            return 0;
        }

        public string GetConnState()
        {
            return _status;
        }

        private void Shutdown()
        {
            try
            {
                
                Func<int> shutdownAction = () => {
                    // 清理资源
                    if (_rttpSocket != (RTSOCKET)0)
                    {
                        // 释放GCHandle
                        IntPtr ptr = RTSocketAPI.rt_get_userdata(_rttpEngine, _rttpSocket, 0);
                        if (ptr != IntPtr.Zero)
                        {
                            try
                            {
                                GCHandle handle = GCHandle.FromIntPtr(ptr);
                                if (handle.IsAllocated)
                                {
                                    handle.Free();
                                }
                            }
                            catch { }
                        }
                        
                        RTSocketAPI.rt_close(_rttpEngine, _rttpSocket);
                        _rttpSocket = (RTSOCKET)0;
                    }

                    _sendQueue.Clear();
                    _receiveQueue.Clear();

                    _tickRunning = false;

                    return 0;
                };
                _apiCallQueue.Enqueue(shutdownAction);
                Wakeup();   
            }
            catch (Exception ex)
            {
                UnityEngine.Debug.LogError("Shutdown error: " + ex.Message);
            }
        }

        private bool ProcessApiCallQueue()
        {
            // 一次处理最多5个API调用任务，避免tick循环被长时间阻塞
            int processedCount = 0;
            const int maxProcessedPerTick = 5;
            
            while (processedCount < maxProcessedPerTick && _apiCallQueue.TryDequeue(out Func<int> apiCall))
            {
                try
                {
                    // 记录API调用耗时
                    DateTime startTime = DateTime.Now;
                    // UnityEngine.Debug.Log("entering apiCall");
                    apiCall();
                    // UnityEngine.Debug.Log("leave apiCall");
                    DateTime endTime = DateTime.Now;
                    TimeSpan duration = endTime - startTime;
                    // UnityEngine.Debug.Log($"跨线程RTScoketAPI调用耗时: {duration.TotalMilliseconds}ms");
                }
                catch (Exception ex)
                {
                    // 忽略API调用过程中的异常，让调用者通过返回值处理
                    UnityEngine.Debug.Log($"API调用异常: {ex.Message}");
                }
                processedCount++;
            }

            return processedCount == maxProcessedPerTick;
        }

        private bool CheckForIncomingUdpPackets()
        {
            // 确保UDP socket已初始化
            if (_udpSocket == null)
                return false;
                    
            byte[] buffer = new byte[MaxPacketSize];
                
            try
            {
                // 从操作系统UDP socket接收数据
                EndPoint remoteEndPoint = new IPEndPoint(IPAddress.Any, 0);
                int receivedBytes = 0;

                try
                {
                    receivedBytes = _udpSocket.ReceiveFrom(buffer, ref remoteEndPoint);
                }
                catch
                {
                    return false;
                }
                
                if (receivedBytes <= 0)
                    return false;
                    
                // 现在我们有了从操作系统UDP socket接收的数据包，可以调用rt_incoming_packet进行处理
                IPEndPoint senderEndPoint = (IPEndPoint)remoteEndPoint;
                    
                // 创建地址结构用于rt_incoming_packet
                sockaddr_in senderAddr = sockaddr_in.Create(senderEndPoint);
                int addrSize = Marshal.SizeOf(senderAddr);
                IntPtr addrPtr = Marshal.AllocHGlobal(addrSize);
                
                try
                {
                    Marshal.StructureToPtr(senderAddr, addrPtr, false);
                    
                    // 使用GCHandle固定缓冲区
                    GCHandle bufferHandle = GCHandle.Alloc(buffer, GCHandleType.Pinned);
                    try
                    {
                        // 调用rt_incoming_packet处理从操作系统接收的UDP报文
                        IntPtr socketPtr = RTSocketAPI.rt_incoming_packet(_rttpEngine,
                            bufferHandle.AddrOfPinnedObject(), 
                            receivedBytes, 
                            addrPtr, 
                            addrSize);

                        //客户端不可能有连入的socket产生
                        System.Diagnostics.Debug.Assert(socketPtr == IntPtr.Zero);

                    }
                    finally
                    {
                        // 释放GCHandle
                        bufferHandle.Free();
                    }
                }
                finally
                {
                    Marshal.FreeHGlobal(addrPtr);
                }
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private void TickLoop()
        {
            UnityEngine.Debug.Log("RTTP working thread running");
            
            InitializeUdpSocket();
            InitializeRttpEngine();

            // Timer for controlling rt_tick frequency
            Stopwatch tickTimer = Stopwatch.StartNew();
            long lastTickTime = 0;
            const int TICK_INTERVAL_MS = 5;
            
            // Lists for Socket.Select
            System.Collections.IList checkRead = new System.Collections.ArrayList();
            byte[] controlBuffer = new byte[16];
            
            while (_tickRunning)
            {
                try
                {
                    long currentTime = tickTimer.ElapsedMilliseconds;
                    
                    // 1. Execute rt_tick if needed
                    if (currentTime - lastTickTime >= TICK_INTERVAL_MS)
                    {
                        RTSocketAPI.rt_tick(_rttpEngine);
                        lastTickTime = currentTime;
                    }

                    // 2. Process API call queue tasks
                    ProcessApiCallQueue();
                    
                    // 3. Calculate wait time for next tick
                    long nextTickTime = lastTickTime + TICK_INTERVAL_MS;
                    int waitTime = (int)(nextTickTime - tickTimer.ElapsedMilliseconds);
                    if (waitTime < 0) waitTime = 0;
                    
                    // 4. Wait for UDP data or Control signal
                    if (_udpSocket != null && _controlSocket != null)
                    {
                        checkRead.Clear();
                        checkRead.Add(_udpSocket);
                        checkRead.Add(_controlSocket);
                        
                        // Socket.Select timeout is in microseconds
                        Socket.Select(checkRead, null, null, waitTime * 1000);
                        
                        // Check if control socket has data (wakeup signal)
                        if (checkRead.Contains(_controlSocket))
                        {
                            // Clear wakeup signal
                            try 
                            { 
                                while (_controlSocket.Available > 0)
                                    _controlSocket.Receive(controlBuffer);
                            } 
                            catch { }
                            // Loop will continue and process API queue immediately
                        }
                        
                        // Check if UDP socket has data
                        if (checkRead.Contains(_udpSocket))
                        {
                            CheckForIncomingUdpPackets();
                        }
                    }
                    else
                    {
                        if (waitTime > 0) Thread.Sleep(waitTime);
                    }
                }
                catch (Exception ex)
                {
                    UnityEngine.Debug.Log($"TickLoop error: {ex.Message}");
                    try { Thread.Sleep(1); } catch {}
                }
            }

            DisposeUdpSocket();
            DisposeRttpEngine();

            UnityEngine.Debug.Log("RTTP working thread exit");
        }
    }
}