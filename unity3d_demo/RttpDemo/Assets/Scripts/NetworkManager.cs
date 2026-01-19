using System.Collections.Generic;
using UnityEngine.SceneManagement;
using System;
using rtttech;
using System.Threading.Tasks;
using UnityEngine;

namespace RttpDemo
{
    public class NetworkManager
    {
        private static NetworkManager m_instance;
        public static NetworkManager Instance
        {
            get
            {
                if (m_instance == null)
                {
                    // 添加日志输出
                    Debug.Log("NetworkManager Instance created.");
                    m_instance = new NetworkManager();
                    m_instance.Initialize();
                }
                return m_instance;
            }
        }

        // 网络通信实例
        private RttpClientConnection m_connection;
        
        // 回调函数
        public static Action s_connectedCallback;
        public static Action<int> s_errorCallback;
        public static Action<byte[], int> s_dataCallback;
        public static Action<byte[], int, int> s_dataSentCallback;
        
        // 当前是否在FightScene中
        private bool m_isInFightScene = false;

        // 初始化时自动订阅场景加载事件
        private void Initialize()
        {
            
            m_connection = new RttpClientConnection();
            
            // 确保事件订阅正确
            if (m_connection != null)            
            {                
                // 确保正确订阅基类的事件                
                ((BaseClientConnection)m_connection).m_onConnected += OnConnectedHandler;
                ((BaseClientConnection)m_connection).m_onError += OnErrorHandler;
                ((BaseClientConnection)m_connection).m_onDataReceived += OnDataReceivedHandler;               
                ((BaseClientConnection)m_connection).m_onDataSent += OnDataSentHandler;
            }
            
            // 监听场景加载完成事件
            SceneManager.sceneLoaded += OnSceneLoaded;
        }

        public void ConnectServer(string host, int port)
        {
            Debug.Log($"Connecting to server: {host}:{port}");

            if (m_connection != null)
            {
                m_connection.Connect(host, port);
            }
        }

        public void SendData(byte[] data)
        {
            Debug.Log($"Sending data to server: {data.Length} bytes");
            
            if (m_connection != null)
            {
                m_connection.Send(data);
            }
        }

        public void ReceiveData(int maxLen)
        {
            if (m_connection != null)
            {
                m_connection.Receive(maxLen);
            }
        }

        public void Disconnect()
        {
            Debug.Log("Disconnecting from server");

            if (m_connection != null)
            {
                m_connection.Close();
            }
        }

        private void OnConnectedHandler(object sender, EventArgs e)
        {
               
            s_connectedCallback?.Invoke();
            
        }

        private void OnErrorHandler(int errorCode)
        {            
            s_errorCallback?.Invoke(errorCode);
        }

        private void OnDataReceivedHandler(byte[] data)
        {
            s_dataCallback?.Invoke(data, data.Length);
        }

        private void OnDataSentHandler(byte[] data, int length, int sentBytes)
        {
            // 可以替换为自定义日志系统或移除日志            
            Console.WriteLine($"Data sent: {sentBytes} bytes");
            
            // 通知上层数据发送事件
            s_dataSentCallback?.Invoke(data, length, sentBytes);
        }
        
        private void OnSceneLoaded(Scene scene, LoadSceneMode mode)
        {            
            // 更新当前场景状态
            m_isInFightScene = scene.name == "FightScene";
            // 可以替换为自定义日志系统或移除日志            
            Console.WriteLine("Current scene: " + scene.name + ", isInFightScene: " + m_isInFightScene);
        }
        


        public void Dispose()
        {            
            // 取消监听场景加载事件
            SceneManager.sceneLoaded -= OnSceneLoaded;
            
            if (m_connection != null)
            {                
                // 安全地取消订阅事件                
                try
                {                     
                    // 确保正确取消订阅基类的事件                       
                    ((BaseClientConnection)m_connection).m_onConnected -= OnConnectedHandler;
                    ((BaseClientConnection)m_connection).m_onError -= OnErrorHandler;
                    ((BaseClientConnection)m_connection).m_onDataReceived -= OnDataReceivedHandler;
                    ((BaseClientConnection)m_connection).m_onDataSent -= OnDataSentHandler;
                }                
                catch (Exception ex)
                {                     
                    // 可以替换为自定义日志系统或移除日志                     
                    Console.WriteLine("Exception when unsubscribing events: " + ex.Message);
                }
                m_connection.Close();

            }
        }


    }
}