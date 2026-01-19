using System.Collections;
using System.Collections.Generic;
using UnityEngine.SceneManagement;
using System;
using UnityEngine;
using System.Threading.Tasks;
using rtttech;

namespace RttpDemo
{

    public class GameNetworkClient : MonoBehaviour
    {
        public enum ConnState { IDLE, CONNECTING, CONNECTED, ERROR }

        private static GameNetworkClient m_instance;
        public static GameNetworkClient Instance
        {
            get 
            { 
                if (m_instance == null) 
                {                     
                    // 添加日志输出
                    Debug.Log("GameNetworkClient Instance created.");
                    m_instance = new GameObject("GameNetworkClient").AddComponent<GameNetworkClient>();
                    // 使对象在场景切换时不被销毁
                    DontDestroyOnLoad(m_instance.gameObject);
                    //m_instance = new GameNetworkClient();
                    m_instance.Init();
                }
                return m_instance; 
            }
        }

        // 网络通信对象
        private NetworkManager m_networkManager;

        public delegate void AttackCallbackHandler(int attackId, long latency);
        public event AttackCallbackHandler m_attackCallback;
        public event Action<int> OnNetworkError;
        public event Action OnConnectedToServer;
        public event Action<byte[], int, int> OnDataSent;
        
        private ConnState m_connState = ConnState.IDLE;
        private string m_connectionStatus = "";
        // Use this for initialization
        private bool m_bUseStubMode = true;

        private readonly Queue<System.Action> _executionQueue = new Queue<System.Action>();
        
        // KeepAlive相关
        private float _keepAliveInterval = 5.0f; // 5秒发送一次心跳
        private float _lastKeepAliveTime = 0f;
        // 延迟统计
        private Dictionary<int, long> _requestTimestamps = new Dictionary<int, long>();
        private int _nextAttackId = 1;
        
        // 粘包处理
        private List<byte> _recvBuffer = new List<byte>();

        private void Init()
        {            
            m_networkManager = NetworkManager.Instance;

            NetworkManager.s_connectedCallback += OnServerConnectedCallback;
            NetworkManager.s_errorCallback += OnErrorCallbackHandler;
            NetworkManager.s_dataCallback += OnServerDataCallback;
            NetworkManager.s_dataSentCallback += OnDataSentCallback;

            /*
            if (m_bUseStubMode)
            {
                int ret = RTStubAPI.rt_stub_init();
                if (ret != 0)
                {
                    Debug.LogError($"rt_stub_init failed, ret: {ret}");
                    return;
                }

                ret = RTStubAPI.rt_stub_add_map(9000, "192.168.123.200", 7777);//replace with your RTTP proxy server
                if (ret != 0)
                {
                    Debug.LogError($"rt_stub_add_map failed, ret: {ret}");
                    return;
                }

                ret = RTStubAPI.rt_stub_start();
                if (ret != 0)
                {
                    Debug.LogError($"rt_stub_start failed, ret: {ret}");
                    return;
                }
            }
            */
        }
        
        void Update()
        {            
            lock (_executionQueue)
            {                
                while (_executionQueue.Count > 0)
                {                    
                    _executionQueue.Dequeue().Invoke();
                }
            }
            
            // 处理KeepAlive发送
            if (m_connState == ConnState.CONNECTED && Time.time - _lastKeepAliveTime >= _keepAliveInterval)
            {                
                SendKeepAlive();
                _lastKeepAliveTime = Time.time;
            }

            /*
            if (m_bUseStubMode)
            {
                while (true)
                {
                    string log = RTStubAPI.ReadLog();
                    if (log == null) break;
                    Debug.Log(log);
                    
                }
                //get state per 1 second
                if (Time.time - _lastPrintStateTime >= 1.0f)
                {
                    string state = RTStubAPI.GetState();
                    if (state != null)
                    {
                        Debug.Log(state);
                    }
                    _lastPrintStateTime = Time.time;
                }
            }
            */
        }

        void OnDestroy()
        {
            Debug.Log("GameNetworkClient OnDestroy");

            NetworkManager.s_connectedCallback -= OnServerConnectedCallback;
            NetworkManager.s_errorCallback -= OnErrorCallbackHandler;
            NetworkManager.s_dataCallback -= OnServerDataCallback;
            NetworkManager.s_dataSentCallback -= OnDataSentCallback;

            if (m_bUseStubMode)
            {
                RTStubAPI.rt_stub_stop();
                RTStubAPI.rt_stub_uninit();
            }
        }

        public void DispatchToMainThread(System.Action action)
        {            
            lock (_executionQueue)
            {                
                _executionQueue.Enqueue(action);
            }
        }
        
        private void ResetConnectionState()
        {            
            lock (m_connectionStatus)
            {                
                m_connectionStatus = "idle";
            }
            m_connState = ConnState.IDLE;
            
            // 清理回调引用，避免内存泄漏            
            if (m_attackCallback != null)
            {                
                foreach (var handler in m_attackCallback.GetInvocationList())
                {                    
                    m_attackCallback -= (AttackCallbackHandler)handler;
                }
            }
            
            // 清理延迟统计
            _requestTimestamps.Clear();
            _nextAttackId = 1;
            
            // 清理接收缓冲区
            _recvBuffer.Clear();
            
            // 可以替换为自定义日志系统或移除日志            
            Console.WriteLine("Connection state reset for new login attempt");
        }
        
        public void Dispose()
        {            
            NetworkManager.s_connectedCallback -= OnServerConnectedCallback;
            NetworkManager.s_errorCallback -= OnErrorCallbackHandler;
            NetworkManager.s_dataCallback -= OnServerDataCallback;
            NetworkManager.s_dataSentCallback -= OnDataSentCallback;
            
            ResetConnectionState();
        }

        public void ConnectToServer(string ip, int port)
        {            
            if (m_connState == ConnState.CONNECTING || m_connState == ConnState.CONNECTED)
                return;

            m_networkManager.ConnectServer(ip, port);

            m_connState = ConnState.CONNECTING;

            lock (m_connectionStatus)
            {                
                m_connectionStatus = "connecting";
            }
        }

        public void Attack(long targetId, int attackType)
        {            
            if (m_connState != ConnState.CONNECTED)
            {                
                // 可以替换为自定义日志系统或移除日志                
                Console.WriteLine("not connected");                
                return;
            }
            
            int attackId = _nextAttackId++;
            SendAttack(attackId, targetId, attackType);
        }

        public void CloseConnection()
        {
            if (m_connState == ConnState.CONNECTED)
            {
                m_networkManager.Disconnect();
                m_connState = ConnState.IDLE;
            }
        }

        public bool IsConnected()
        {            
            return m_connState == ConnState.CONNECTED;
        }

        public ConnState GetState()
        {            
            return m_connState;
        }

        public string GetStateDesc()
        {            
            lock (m_connectionStatus)
            {                
                return m_connectionStatus;
            }
        }

        public void OnServerConnectedCallback()
        {            
                    
            m_connState = ConnState.CONNECTED;

            lock (m_connectionStatus)
            {                
                m_connectionStatus = "connected";
            }
            
            DispatchToMainThread(() => OnMainThreadServerConnected());
        }

        public void OnMainThreadServerConnected()
        {
            // 初始化KeepAlive计时
            _lastKeepAliveTime = Time.time;

            Debug.Log("GameNetworkClient connect server success");

            OnConnectedToServer?.Invoke();

            m_networkManager.ReceiveData(16*1024);
        }

        public void OnErrorCallbackHandler(int errorCode)
        {            
            lock (m_connectionStatus)
            {
                m_connState = ConnState.ERROR;
                
            }

            DispatchToMainThread(() => OnMainThreadNetworkError(errorCode));
        }

        public void OnMainThreadNetworkError(int errorCode)
        {
            // 可以替换为自定义日志系统或移除日志
            Debug.Log("GameNetworkClient Error Event:{errorCode}");

            OnNetworkError?.Invoke(errorCode);
        }

        public void OnServerDataCallback(byte[] data, int len)
        {

            long recvTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            
            DispatchToMainThread(() => HandleServerData(data, len, recvTime));
        }
        
        public void OnDataSentCallback(byte[] data, int length, int sentBytes)
        {
            DispatchToMainThread(() => OnMainThreadDataSent(data, length, sentBytes));
        }

        public void OnMainThreadDataSent(byte[] data, int length, int sentBytes)
        {
            // 可以替换为自定义日志系统或移除日志
            Debug.Log($"GameNetworkClient sent {sentBytes} bytes");

            OnDataSent?.Invoke(data, length, sentBytes);
        }



        public void HandleServerData(byte[] data, int len, long recvTime)
        {

            Debug.Log($"received {len} bytes from server");
            // 将新数据添加到接收缓冲区
            lock (_recvBuffer)
            {                
                _recvBuffer.AddRange(new ArraySegment<byte>(data, 0, len));
                
                // 处理缓冲区中的完整报文
                while (_recvBuffer.Count >= GameProtocol.HEADER_SIZE)
                {                    
                    // 读取payload大小（不包含头部）
                    int packetSize = BitConverter.ToInt32(_recvBuffer.ToArray(), 0);
                    
                    // 确保报文长度合理
                    if (packetSize < 0 || packetSize > 128 * 1024) // 限制最大128K
                    {
                        Debug.LogError($"Invalid payload size: {packetSize}");
                        _recvBuffer.Clear();
                        return;
                    }
                    
                    // 检查是否有完整的报文
                    if (_recvBuffer.Count < packetSize)
                    {                        
                        break; // 数据不足，等待更多数据
                    }
                    
                    // 提取完整报文
                    byte[] packet = _recvBuffer.GetRange(0, packetSize).ToArray();
                    _recvBuffer.RemoveRange(0, packetSize);
                    
                    // 处理报文
                    ProcessPacket(packet, recvTime);
                }
            }

            m_networkManager.ReceiveData(16 * 1024);
        }
        
        private void ProcessPacket(byte[] packet, long recvTime)
        {            
            // 使用GameProtocol解析报文头
            if (GameProtocol.ParseHeader(packet, out int payloadSize, out GameProtocol.MessageType messageType))
            {
                switch (messageType)
            {   
                //demo server仅原样返回客户端的请求报文，所以此处仅处理Attack报文和KeepAlive报文
                case GameProtocol.MessageType.Attack:
                    // 处理攻击响应
                    if (packet.Length >= GameProtocol.HEADER_SIZE)
                    {
                        // 提取payload部分
                        int extractedPayloadSize = packet.Length - GameProtocol.HEADER_SIZE;
                        if (extractedPayloadSize > 0)
                        {
                            byte[] payload = new byte[extractedPayloadSize];
                            Buffer.BlockCopy(packet, GameProtocol.HEADER_SIZE, payload, 0, extractedPayloadSize);
                            HandleAttackResponse(payload, recvTime);
                        }
                    }
                    break;
                    
                case GameProtocol.MessageType.KeepAlive:
                    Debug.Log($"Received KeepAlive response, length: {packet.Length}");
                    break;
                    
                default:
                    Debug.LogWarning($"Unknown message type: {messageType}");
                    break;
                }
            }
            else
            {
                Debug.LogError("Failed to parse packet header");
            }
        }
        
        private void HandleAttackResponse(byte[] payload, long recvTime)
        {
            try
            {
                // 解析AttackResponse结构体
                GameProtocol.AttackRequest attack = GameProtocol.AttackRequest.FromBytes(payload);
                int attackId = (int)attack.AttackId;
                
                // 计算延迟
                if (_requestTimestamps.TryGetValue(attackId, out long sendTime))
                {
                    long latency = recvTime - sendTime;
                    
                    // 移除已处理的请求时间戳
                    _requestTimestamps.Remove(attackId);
                    
                    // 触发回调
                    m_attackCallback?.Invoke(attackId, latency);
                }
            }
            catch (Exception ex)
            {
                Debug.LogError($"Failed to parse attack response: {ex.Message}");
            }
        }
        
        private void SendKeepAlive()
        {            
            try
            {
                // 创建KeepAliveRequest结构体
                GameProtocol.KeepAliveRequest request = new GameProtocol.KeepAliveRequest
                {
                    Timestamp = (long)(Time.realtimeSinceStartup * 1000),
                    RandomNumber = UnityEngine.Random.Range(0, int.MaxValue)
                };
                
                // 转换为字节数组
                byte[] payload = request.ToBytes();
                
                // 使用GameProtocol的BuildMessage方法构建报文
                byte[] packet = GameProtocol.BuildMessage(GameProtocol.MessageType.KeepAlive, payload);
                
                // 发送报文
                m_networkManager.SendData(packet);
                
                Debug.Log($"KeepAlive packet sent, timestamp: {request.Timestamp}");
            }
            catch (Exception ex)
            {
                Debug.LogError($"Failed to send KeepAlive: {ex.Message}");
            }
        }
        
        private void SendAttack(int attackId, long targetId, int attackType)
        {
            long sendTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            _requestTimestamps[attackId] = sendTime;
            
            // 创建AttackRequest结构体
            GameProtocol.AttackRequest request = new GameProtocol.AttackRequest
            {
                AttackId = attackId,
                AttackerId = 123, // 假设的攻击者ID，实际应该从游戏状态中获取
                TargetId = targetId,
                AttackType = attackType,
                PositionX = 0, // 可以从游戏状态中获取实际位置
                PositionY = 0,
                PositionZ = 0,
                Timestamp = sendTime
            };
            
            // 转换为字节数组
            byte[] payload = request.ToBytes();
            
            // 使用GameProtocol的BuildMessage方法构建报文
            byte[] packet = GameProtocol.BuildMessage(GameProtocol.MessageType.Attack, payload);
            
            // 发送报文
            m_networkManager.SendData(packet);
            
            Debug.Log($"Attack packet sent, id: {attackId}, target: {targetId}, type: {attackType}");
        }
        

    }

}