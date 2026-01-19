using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RttpDemo
{
    /// <summary>
    /// Data received event arguments
    /// </summary>
    public class DataReceivedEventArgs : EventArgs
    {
        public byte[] Data { get; }
        
        public DataReceivedEventArgs(byte[] data)
        {
            Data = data;
        }
    }

    /// <summary>
    /// Data sent event arguments
    /// </summary>
    public class DataSentEventArgs : EventArgs
    {
        public byte[] Data { get; }
        public int Length { get; }
        public int SentBytes { get; }
        
        public DataSentEventArgs(byte[] data, int length, int sentBytes)
        {
            Data = data;
            Length = length;
            SentBytes = sentBytes;
        }
    }

    /// <summary>
    /// Error event arguments
    /// </summary>
    public class ErrorEventArgs : EventArgs
    {
        public int ErrorCode { get; }
        
        public ErrorEventArgs(int errorCode)
        {
            ErrorCode = errorCode;
        }
    }

    /// <summary>
    /// Connection state enumeration
    /// </summary>
    public enum ConnectionState
    {
        /// <summary>
        /// Disconnected state
        /// </summary>
        Disconnected,
        /// <summary>
        /// Connecting state
        /// </summary>
        Connecting,
        /// <summary>
        /// Connected state
        /// </summary>
        Connected,
        /// <summary>
        /// Disconnecting state
        /// </summary>
        Disconnecting,
        /// <summary>
        /// Error state
        /// </summary>
        Error
    }

    
    /// <summary>
    /// Base class for client connections
    /// </summary>
    public abstract class BaseClientConnection
    {
        // Connection state
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

        // Last error code
        protected int m_lastError = 0;
        public int LastError
        {
            get { return m_lastError; }
        }

        // Event definitions
        /// <summary>
        /// Connection state change event
        /// </summary>
        public event EventHandler m_onConnected;
        
        /// <summary>
        /// Data received event
        /// </summary>
        public event EventHandler<DataReceivedEventArgs> m_onDataReceived;

        /// <summary>
        /// Data sent event
        /// </summary>
        public event EventHandler<DataSentEventArgs> m_onDataSent;
        
        /// <summary>
        /// Connection error event
        /// </summary>
        public event EventHandler<ErrorEventArgs> m_onError;

        /// <summary>
        /// Connect to server
        /// </summary>
        /// <param name="host">Server host address</param>
        /// <param name="port">Server port</param>
        public abstract void Connect(string host, int port);

        /// <summary>
        /// Close connection
        /// </summary>
        public abstract void Close();

        /// <summary>
        /// Send data
        /// </summary>
        /// <param name="data">Data to send</param>
        public abstract void Send(byte[] data);

        /// <summary>
        /// Receive data (active receive mode)
        /// </summary>
        /// <param name="maxLen">Maximum length to receive</param>
        public virtual void Receive(int maxLen)
        {
            throw new NotImplementedException("Active receive mode not implemented");
        }

        protected void NotifyConnected()
        {
            State = ConnectionState.Connected;
            m_onConnected?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>
        /// Notify data received (called by subclasses)
        /// </summary>
        /// <param name="data">Received data</param>
        protected void NotifyDataReceived(byte[] data)
        {
            m_onDataReceived?.Invoke(this, new DataReceivedEventArgs(data));
        }

        /// <summary>
        /// Notify error (called by subclasses)
        /// </summary>
        /// <param name="error">Error type</param>
        protected void NotifyError(int error)
        {
            m_lastError = error;
            State = ConnectionState.Error;
            m_onError?.Invoke(this, new ErrorEventArgs(error));
        }
        
        /// <summary>
        /// Notify data sent (called by subclasses)
        /// </summary>
        /// <param name="data">Data sent</param>
        /// <param name="length">Data length</param>
        /// <param name="sentBytes">Bytes actually sent</param>
        protected void NotifyDataSent(byte[] data, int length, int sentBytes)
        {
            m_onDataSent?.Invoke(this, new DataSentEventArgs(data, length, sentBytes));
        }
    }
}