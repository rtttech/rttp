using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RttpDemo
{
    /// <summary>
    /// 连接状态枚举
    /// </summary>
    public enum ConnectionState
    {
        /// <summary>
        /// 断开连接状态
        /// </summary>
        Disconnected,
        /// <summary>
        /// 连接中状态
        /// </summary>
        Connecting,
        /// <summary>
        /// 已连接状态
        /// </summary>
        Connected,
        /// <summary>
        /// 断开连接中状态
        /// </summary>
        Disconnecting,
        /// <summary>
        /// 错误状态
        /// </summary>
        Error
    }

    
    /// <summary>
    /// 客户端连接基类
    /// </summary>
    public abstract class BaseClientConnection
    {
        // 连接状态
        private ConnectionState m_state = ConnectionState.Disconnected;
        public ConnectionState State
        {
            get { return m_state; }
            protected set
            {
                if (m_state != value)
                {
                    m_state = value;
                }
            }
        }

        // 最后错误信息
        public int m_lastError { get; protected set; } = 0;

        // 事件定义
        /// <summary>
        /// 连接状态变化事件
        /// </summary>
        public event EventHandler m_onConnected;
        
        /// <summary>
        /// 接收到数据事件
        /// </summary>
        public event Action<byte[]> m_onDataReceived;

        /// <summary>
        /// 数据发送事件
        /// </summary>
        public event Action<byte[], int, int> m_onDataSent;
        
        /// <summary>
        /// 连接错误事件
        /// </summary>
        public event Action<int> m_onError;

        /// <summary>
        /// 连接到服务器
        /// </summary>
        /// <param name="host">服务器主机地址</param>
        /// <param name="port">服务器端口</param>
        public abstract void Connect(string host, int port);

        /// <summary>
        /// 关闭连接
        /// </summary>
        public abstract void Close();

        /// <summary>
        /// 发送数据
        /// </summary>
        /// <param name="data">要发送的数据</param>
        public abstract void Send(byte[] data);


        protected void NotifyConnected()
        {
            State = ConnectionState.Connected;
            m_onConnected?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>
        /// 通知数据接收（由子类调用）
        /// </summary>
        /// <param name="data">接收到的数据</param>
        protected void NotifyDataReceived(byte[] data)
        {
            m_onDataReceived?.Invoke(data);
        }

        /// <summary>
        /// 通知错误（由子类调用）
        /// </summary>
        /// <param name="error">错误类型</param>
        protected void NotifyError(int error)
        {
            m_lastError = error;
            State = ConnectionState.Error;
            m_onError?.Invoke(error);
        }
        
        /// <summary>
        /// 通知数据发送（由子类调用）
        /// </summary>
        /// <param name="bytesSent">发送的字节数</param>
        protected void NotifyDataSent(byte[] data, int length, int sentBytes)
        {
            m_onDataSent?.Invoke(data, length, sentBytes);
        }
    }
}