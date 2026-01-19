using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics;
using System.Drawing;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace RttpDemo
{
    public partial class EchoClientForm : Form
    {
        private RttpClientConnection _clientConn;
        private volatile bool _isRunning = false;
        private Queue<long> _latencyQueue = new Queue<long>();
        private long _avgLatency = 0;
        

        private Dictionary<long, long> _sentPacketInfo = new Dictionary<long, long>();
        private long _packetSeq = 0;
        private long _packetSent = 0;
        private long _packetCount = 0;
        private long _packetSending = 0;
        private long _maxSendingPacketCount = 10;
        private CancellationTokenSource _cancellationTokenSource;
        
        private string _remoteIP;
        private int _remotePort;
        private readonly Random _random = new Random();
        private System.Windows.Forms.Timer _timer;

        private readonly SynchronizationContext _uiContext;

        // Data buffer for receiving stream data
        private readonly List<byte> _receiveBuffer = new List<byte>();
        

        public EchoClientForm()
        {
            InitializeComponent();

            _uiContext = SynchronizationContext.Current;
        }

        private void EchoClientForm_Load(object sender, EventArgs e)
        {
            // Initialize UI components
            txtRemoteIP.Text = "192.168.123.200";
            txtRemotePort.Text = "9999";
            btnConnect.Enabled = true;
            btnDisconnect.Enabled = false;
            
            // Initialize receive timer (similar to Unity3D's tick loop)
            _timer = new System.Windows.Forms.Timer();
            _timer.Interval = 200; // 10ms interval for processing
            _timer.Tick += OnTimer;

            //set form caption
            this.Text = "Rttp demo - RTTP Ver:" + rtttech.RTSocketAPI.rt_get_version();
        }

        private void btnConnect_Click(object sender, EventArgs e)
        {
            _remoteIP = txtRemoteIP.Text.Trim();
            if (!int.TryParse(txtRemotePort.Text.Trim(), out _remotePort))
            {
                MessageBox.Show("Please enter a valid port", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            try
            {
                // Disable buttons to prevent repeated clicks
                btnConnect.Enabled = false;
                btnDisconnect.Enabled = false;
                UpdateStatus("Connecting...");
                
                // Create RttpClientSocket instance (inherits from BaseClientConnection)
                _clientConn = new RttpClientConnection();
                
                // Subscribe to events (similar to Unity3D event model)
                _clientConn.m_onConnected += ClientConnection_OnConnected;
                _clientConn.m_onDataReceived += ClientConnection_OnDataReceived;
                _clientConn.m_onError += ClientConnection_OnError;
                _clientConn.m_onDataSent += ClientConnection_OnDataSent;
                
                // Connect to server (synchronous call that queues the operation)
                _clientConn.Connect(_remoteIP, _remotePort);
                
                
            }
            catch (Exception ex)
            {
                MessageBox.Show("Connection failed: " + ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                Cleanup();
            }
        }

        private void btnDisconnect_Click(object sender, EventArgs e)
        {
            Disconnect();
        }

        private void Disconnect()
        {
            if (!_isRunning)
                return;

            _isRunning = false;
            
            // Stop receive timer
            _timer.Stop();
            
            // Cancel async tasks
            if (_cancellationTokenSource != null)
            {
                _cancellationTokenSource.Cancel();
                _cancellationTokenSource.Dispose();
                _cancellationTokenSource = null;
            }
            
            // Close connection
            if (_clientConn != null)
            {
                _clientConn.Close();
            }
            
            Cleanup();
            
            btnConnect.Enabled = true;
            btnDisconnect.Enabled = false;
            UpdateStatus("Disconnected");
            UpdateLatency(0);
        }

        private void Cleanup()
        {
            // Unsubscribe from events
            if (_clientConn != null)
            {
                _clientConn.m_onConnected -= ClientConnection_OnConnected;
                _clientConn.m_onDataReceived -= ClientConnection_OnDataReceived;
                _clientConn.m_onError -= ClientConnection_OnError;
                _clientConn.m_onDataSent -= ClientConnection_OnDataSent;

                try 
                {
                    // Release resources
                    _clientConn.Dispose();
                }
                catch { }
                _clientConn = null;
            }

            // Reset status
            _latencyQueue.Clear();
            
            _sentPacketInfo.Clear();
            
            
        }

        // Event handlers for connection events
        private void ClientConnection_OnConnected(object sender, EventArgs e)
        {
            _uiContext.Post(state =>
            {
                // Connection success
                UpdateStatus("Connected to server");

                // Update interface status
                btnDisconnect.Enabled = true;
                _isRunning = true;

                // Create cancellation token
                _cancellationTokenSource = new CancellationTokenSource();

                _timer.Start();

                _clientConn.Receive(4096);
            }, null);
        }

        private void ClientConnection_OnDataReceived(object sender, DataReceivedEventArgs e)
        {
            _uiContext.Post(state =>
            {
                if (e.Data.Length == 0)
                {
                    UpdateStatus("Connection Closed");
                    Disconnect();
                    return;
                }

                _receiveBuffer.AddRange(e.Data);
                
                // Process received data from buffer
                ProcessReceivedDataFromBuffer();

                _clientConn.Receive(4096);
            }, null);
        }

        private void ClientConnection_OnDataSent(object sender, DataSentEventArgs e)
        {
            _uiContext.Post(state =>
            {
                _packetSending--;
            }, null);
        }

        private void ClientConnection_OnError(object sender, ErrorEventArgs e)
        {
            _uiContext.Post(state =>
            {
                UpdateStatus("Connection error: " + e.ErrorCode);
                Disconnect();
            }, null);
        }

        // Timer tick for processing receive queue (similar to Unity3D's tick loop)
        private void OnTimer(object sender, EventArgs e)
        {
            SendPingPacket();
        }

        
        // Send ping packet
        private void SendPingPacket()
        {
            if (_clientConn == null || _clientConn.State != ConnectionState.Connected)
                return;

            if (_packetSending >= _maxSendingPacketCount)
                return;

            try
            {
                // Randomly select packet size
                int[] packetSizes = { 100, 300, 500, 800, 1100 };
                int packetSize = packetSizes[_random.Next(packetSizes.Length)];
                
                // Create data packet
                byte[] buffer = new byte[packetSize];
                BitConverter.GetBytes(packetSize).CopyTo(buffer, 0);
                _packetSeq++;
                BitConverter.GetBytes(_packetSeq).CopyTo(buffer, 4);
                _packetSent++;
                _packetSending++;

                // Record send time
                
                _sentPacketInfo[_packetSeq] = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                
                Debug.WriteLine($"send ping packet, seq:{_packetSeq} size: {packetSize} time:{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}");
                
                // Send data (queued operation similar to Unity3D)
                _clientConn.Send(buffer);
            }
            catch (Exception ex)
            {
                UpdateStatus("Send failed: " + ex.Message);
            }
        }

        // Process received data from buffer
        private void ProcessReceivedDataFromBuffer()
        {
            
            // Process buffer while there's enough data for a header (4 bytes for size)
            while (_receiveBuffer.Count >= 4)
            {
                // Read packet size from header
                int packetSize = BitConverter.ToInt32(_receiveBuffer.ToArray(), 0);
                    
                // Check if we have the entire packet (size + data)
                if (_receiveBuffer.Count < packetSize)
                    break; // Not enough data yet
                        
                // Extract complete packet
                byte[] packetData = _receiveBuffer.GetRange(0, packetSize).ToArray();
                    
                // Remove packet from buffer
                _receiveBuffer.RemoveRange(0, packetSize);
                    
                // Process the complete packet
                ProcessReceivedData(packetData);
            }
            
        }

        // Process received data
        private void ProcessReceivedData(byte[] data)
        {
            if (data == null || data.Length < 8) // Need at least 8 bytes for seq and size
                return;

            try
            {
                // Extract packet information
                int packetSize = BitConverter.ToInt32(data, 0);
                uint packetSeq = BitConverter.ToUInt32(data, 4);

                Debug.WriteLine($"received a packet, seq:{packetSeq} size: {data.Length}, time: {DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}");

                // Process received ping response
                long sentTime;
                
                if (!_sentPacketInfo.TryGetValue(packetSeq, out sentTime))
                {
                    return;
                }
                _sentPacketInfo.Remove(packetSeq);
                
                
                long curTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                long latency = curTime - sentTime;
                _packetCount++;
                
                
                _latencyQueue.Enqueue(latency);
                if (_latencyQueue.Count > 10)
                {
                    _latencyQueue.Dequeue();
                }
                    
                // Calculate average latency
                _avgLatency = _latencyQueue.Sum() / _latencyQueue.Count;
                UpdateLatency(_avgLatency);
                
            }
            catch (Exception ex)
            {
                Debug.WriteLine("Error processing received data: " + ex.Message);
            }
        }

        // UI update methods
        private void UpdateStatus(string status)
        {
            if (InvokeRequired)
            {
                Invoke(new Action<string>(UpdateStatus), status);
            }
            else
            {
                lblStatus.Text = status;
                if (lblStatus is Label)
                {
                    lblStatus.ForeColor = status.Contains("Connected") ? Color.Green : Color.Red;
                }
            }
        }

        private void UpdateLatency(long latency)
        {
            if (InvokeRequired)
            {
                Invoke(new Action<long>(UpdateLatency), latency);
            }
            else
            {
                lblLatency.Text = $"Latency: {latency}ms";
            }
        }

        private void EchoClientForm_FormClosing(object sender, FormClosingEventArgs e)
        {
            Disconnect();
            
            // Cleanup timer
            if (_timer != null)
            {
                _timer.Stop();
                _timer.Dispose();
            }
        }
    }
}