using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using rtttech;
using RTSOCKET = System.IntPtr;
using RTEngine = System.IntPtr;

namespace RttpDemo
{
    // IP address structure
    [StructLayout(LayoutKind.Sequential)]
    internal struct sockaddr_in
    {
        public short sin_family;
        public ushort sin_port;
        public int sin_addr;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
        public byte[] sin_zero;

        public static sockaddr_in Create(IPEndPoint endPoint)
        {
            sockaddr_in addr = new sockaddr_in
            {
                sin_family = 2, // AF_INET
                sin_port = (ushort)IPAddress.HostToNetworkOrder((short)endPoint.Port),
                sin_zero = new byte[8]
            };
            addr.sin_addr = BitConverter.ToInt32(endPoint.Address.GetAddressBytes(), 0);
            return addr;
        }
    }

    /// <summary>
    /// RttpClientSocket class - implements BaseClientConnection with RTTP protocol
    /// Uses task queue and ManualResetEvent for thread-safe API calls
    /// </summary>
    public class RttpClientConnection : BaseClientConnection, IDisposable
    {
        private Socket _udpSocket;
        private int _udpPort = 0; // UDP listening port
        
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
        private const int MaxPacketSize = 2000; // Maximum UDP packet size

        // Send task queue (similar to Java version's SendTask)
        private Queue<SendTask> _sendQueue = new Queue<SendTask>();
        
        // Send task structure (similar to Java version's SendTask)
        private class SendTask
        {
            public byte[] data;
            public int length;
            public int sentBytes = 0; // Current sent bytes
            public SendTask(byte[] data, int length)
            {
                this.data = data;
                this.length = length;
            }
        }
        
        // Receive task queue
        private Queue<ReceiveTask> _receiveQueue = new Queue<ReceiveTask>();
        
        // Receive task structure
        private class ReceiveTask
        {
            public int maxLen;
            public ReceiveTask(int maxLen)
            {
                this.maxLen = maxLen;
            }
        }

        private bool _disposed = false;

        public RttpClientConnection()
        {
            StartTickLoop();
        }

        ~RttpClientConnection()
        {
            // Clean up resources during destruction
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

        /// <summary>
        /// Active receive data API (similar to Java version's receiveAsync)
        /// </summary>
        /// <param name="maxLen">Maximum length to receive</param>
        public override void Receive(int maxLen)
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

        // Start tick loop and UDP listener thread
        private void StartTickLoop()
        {
            if (!_tickRunning)
            {
                _tickRunning = true;
                _tickTask = Task.Run(() => TickLoop());
            }
        }

        // Socket event callback
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
                // Ignore invalid pointers or released GCHandles
            }
        }

        public void OnConnected(RTEngine engine, RTSOCKET socket)
        {
            if (socket != _rttpSocket)
                return;

            _status = "connected";
            Debug.WriteLine("Connect server success");

            NotifyConnected();
        }

        public void OnRead(RTEngine engine, RTSOCKET socket)
        {
            if (socket != _rttpSocket)
                return;

            // When data is readable, process receive queue
            ProcessReceiveQueue();
        }

        public void OnWrite(RTEngine engine, RTSOCKET socket)    
        {
            if (socket != _rttpSocket)
                return;

            ProcessSendQueue();
        }

        public void OnError(RTEngine engine, RTSOCKET socket, int errcode)
        {
            if (socket != _rttpSocket)
                return;

            _status = string.Format("Error:{0}", errcode);
            NotifyError(errcode);

            Debug.WriteLine(_status);
        }

        // Process receive queue (similar to Java version's processReceiveQueue)
        private void ProcessReceiveQueue()
        {
            if (_rttpSocket == (RTSOCKET)0 || _receiveQueue.Count == 0)
                return;

            // Check if socket is readable
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

                // Receive data
                int ret = 0;
                IntPtr bufferPtr = IntPtr.Zero;
                try
                {
                    bufferPtr = Marshal.AllocHGlobal(task.maxLen);
                    ret = RTSocketAPI.rt_recv(_rttpEngine, _rttpSocket, bufferPtr, task.maxLen, 0);
                    //Debug.WriteLine($"rt_recv return: {ret}, time:{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}");
                    if (ret > 0)
                    {
                        // Successfully received data
                        byte[] buff = new byte[ret];
                        Marshal.Copy(bufferPtr, buff, 0, ret);
                        NotifyDataReceived(buff);
                        _receiveQueue.Dequeue(); // Task completed, remove from queue
                    }
                    else if (ret == -1)
                    {
                        // No data to read, wait for next READ event
                        break;
                    }
                    else
                    {
                        // Receive error, remove task
                        _receiveQueue.Dequeue();
                        _status = string.Format("Receive Error:{0}", ret);
                        Debug.WriteLine(_status);
                    }
                }
                catch (Exception ex)
                {
                    Debug.WriteLine("ProcessReceiveQueue error: " + ex.Message);
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

        // Process send queue (similar to Java version's processSendQueue)
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
                    // rt_send parameters: buffer, len, flag=0
                    // Note: need to send data from the sent position, create sub-array
                    byte[] sendBuffer = new byte[willSend];
                    Array.Copy(task.data, task.sentBytes, sendBuffer, 0, willSend);
                    
                    IntPtr bufferPtr = Marshal.AllocHGlobal(willSend);
                    try
                    {
                        Marshal.Copy(sendBuffer, 0, bufferPtr, willSend);
                        int ret = RTSocketAPI.rt_send(_rttpEngine, _rttpSocket, bufferPtr, willSend, 0);
                        //Debug.WriteLine($"rt_send data len: {task.length}, already sent: {task.sentBytes}, cur send: {willSend}, send return: {ret}, time:{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}");
                        
                        if (ret > 0)
                        {
                            task.sentBytes += ret;
                        }
                        else if (ret == -1)
                        {
                            // Not writable, wait for next WRITE event
                            return;
                        }
                        else
                        {
                            // Send error, notify and discard
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
                
                // Notify only when entire SendTask data is sent
                if (task.sentBytes == task.length)
                {
                    NotifyDataSent(task.data, task.length, task.length);
                    _sendQueue.Dequeue();
                }
            }
        }

        // Send data callback implementation
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
                    
                // Use _udpSocket to send data
                if (rttpConn._udpSocket != null && addr != IntPtr.Zero && addrLen > 0 && buffer != IntPtr.Zero && len > 0)
                {
                    try
                    {
                        // Parse address information
                        sockaddr_in sockAddr = Marshal.PtrToStructure<sockaddr_in>(addr);
                        byte[] addressBytes = BitConverter.GetBytes(sockAddr.sin_addr);
                        IPAddress ipAddress = new IPAddress(addressBytes);
                        int port = IPAddress.NetworkToHostOrder((short)sockAddr.sin_port);
                        IPEndPoint endPoint = new IPEndPoint(ipAddress, port);
                        
                        // Copy buffer data to managed array
                        byte[] data = new byte[len];
                        Marshal.Copy(buffer, data, 0, len);
                        
                        // Use UDP socket to send data
                        int sent = rttpConn._udpSocket.SendTo(data, 0, len, SocketFlags.None, endPoint);
                    }
                    catch (Exception)
                    {
                        // Ignore send errors
                    }
                }
                
            }
            catch
            {
                // Ignore invalid pointers or released GCHandles
            }
        }

        private void InitializeUdpSocket()
        {
            try
            {
                // Create UDP socket
                _udpSocket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                // Bind to any available port
                _udpSocket.Bind(new IPEndPoint(IPAddress.Any, 0));
                // Get actual bound port number
                _udpPort = ((IPEndPoint)_udpSocket.LocalEndPoint).Port;
                // Set to non-blocking mode
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
                // If initialization fails, ensure resources are released
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
                    Debug.WriteLine("Failed to initialize RTTP engine");
                    NotifyError(RTSocketConstants.RTTP_INIT_RTTP);
                    return;
                }
                _eventCallback = CallbackOnRttpSocketEvent;
                _sendCallback = CallbackRttpSendDataImp;
                RTSocketAPI.rt_set_callback(_rttpEngine, _eventCallback, _sendCallback);
            }
            catch (Exception ex)
            {
                Debug.WriteLine("Initialize error: " + ex.Message);
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

            // Store current instance pointer in socket user data
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
                Debug.WriteLine("Connect error: " + ex.Message);
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
                    RTSocketAPI.rt_close(_rttpEngine, _rttpSocket);
                    _rttpSocket = (RTSOCKET)0;
                }

                // Clean up queues and state
                _sendQueue.Clear();
                _receiveQueue.Clear();
                
                _status = "disconnected";
            }
            catch (Exception ex)
            {
                Debug.WriteLine("Disconnect error: " + ex.Message);
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
                    // Clean up resources
                    if (_rttpSocket != (RTSOCKET)0)
                    {
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
                Debug.WriteLine("Shutdown error: " + ex.Message);
            }
        }

        private bool ProcessApiCallQueue()
        {
            // Process up to 10 API call tasks per tick to avoid blocking the tick loop
            int processedCount = 0;
            const int maxProcessedPerTick = 5;
            
            while (processedCount < maxProcessedPerTick && _apiCallQueue.TryDequeue(out Func<int> apiCall))
            {
                try
                {
                    apiCall();
                }
                catch (Exception ex)
                {
                    Debug.WriteLine("API call exception: " + ex.Message);
                }
                processedCount++;
            }

            return processedCount == maxProcessedPerTick;
        }

        private void TickLoop()
        {
            Debug.WriteLine("RTTP working thread running");
            
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
                    Debug.WriteLine($"TickLoop error: {ex.Message}");
                    try { Thread.Sleep(1); } catch {}
                }
            }

            DisposeUdpSocket();
            DisposeRttpEngine();

            Debug.WriteLine("RTTP working thread exit");
        }

        private bool CheckForIncomingUdpPackets()
        {
            // Ensure UDP socket is initialized
            if (_udpSocket == null)
                return false;

            if (_udpSocket.Available == 0) 
                return false;
                
            byte[] buffer = new byte[MaxPacketSize];
            
            try
            {
                // Receive data from OS UDP socket
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
                
                // Now we have the packet received from OS UDP socket, call rt_incoming_packet to process
                IPEndPoint senderEndPoint = (IPEndPoint)remoteEndPoint;
                
                // Create address structure for rt_incoming_packet
                sockaddr_in senderAddr = sockaddr_in.Create(senderEndPoint);
                int addrSize = Marshal.SizeOf(senderAddr);
                IntPtr addrPtr = Marshal.AllocHGlobal(addrSize);
                
                try
                {
                    Marshal.StructureToPtr(senderAddr, addrPtr, false);
                    
                    // Use GCHandle to pin buffer
                    GCHandle bufferHandle = GCHandle.Alloc(buffer, GCHandleType.Pinned);
                    try
                    {
                        // Call rt_incoming_packet to process UDP packets received from OS
                        IntPtr socketPtr = RTSocketAPI.rt_incoming_packet(_rttpEngine,
                            bufferHandle.AddrOfPinnedObject(), 
                            receivedBytes, 
                            addrPtr, 
                            addrSize);

                        // Client cannot have incoming sockets
                        Debug.Assert(socketPtr == IntPtr.Zero);
                    }
                    finally
                    {
                        // Release GCHandle
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

        public void Dispose()
        {
            if (!_disposed)
            {
                Shutdown();
                _disposed = true;
                GC.SuppressFinalize(this);
            }
        }
    }
}