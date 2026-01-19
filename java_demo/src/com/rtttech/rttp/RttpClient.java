package com.rtttech.rttp;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.DatagramChannel;
import java.nio.channels.Selector;
import java.nio.channels.SelectionKey;
import java.util.Iterator;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;

/**
 * RttpClient is the Java interface for the RTTP library,
 * providing a friendly API and encapsulating rtsocket, UDP socket, and worker threads.
 */
public class RttpClient implements rtsocket_listener {
    // Load JNI library
    static {
        System.loadLibrary("JniRttpDLL");
    }

    private long engine = 0;
    private long socket = 0;
    private volatile boolean initialized = false;
    private volatile boolean connected = false;
    private volatile boolean tickRunning = false;
    private volatile boolean destroying = false; // 防止重复销毁
    private Thread tickThread;
    private Thread udpListenerThread;
    private DatagramSocket udpSocket;
    private DatagramChannel udpChannel;
    private Selector udpSelector;
    private int udpPort = 0;
    private static final int MAX_PACKET_SIZE = 2000;
    private static final int MAX_RECV_SIZE = 16 * 1024;

    // API call task queue
    private final ConcurrentLinkedQueue<ApiCallTask> apiCallQueue = new ConcurrentLinkedQueue<>();
    
    // User data send queue
    private final Queue<SendTask> sendQueue = new ConcurrentLinkedQueue<>();
    // User data receive queue
    private final Queue<ReceiveTask> receiveQueue = new ConcurrentLinkedQueue<>();

    private static class SendTask {
        final byte[] data;
        final int length;
        int sentBytes = 0; // 当前已发送字节数
        SendTask(byte[] data, int length) {
            this.data = data;
            this.length = length;
        }
    }
    private static class ReceiveTask {
        final int maxLen;
        ReceiveTask(int maxLen) {
            this.maxLen = maxLen;
        }
    }

    /**
     * API call task interface
     */
    private interface ApiCallTask {
        int execute();
    }

    /**
     * Constructor
     */
    public RttpClient() {

    }

    /**
     * Initialize RTTP client
     * @return 0 for success, other values for failure
     */
    public synchronized int initialize() {
        if (destroying) {
            throw new IllegalStateException("Cannot initialize while destroying");
        }
        if (!initialized) {
            // Initialize UDP socket
            initializeUdpSocket();

            // Initialize engine with empty config string
            engine = rtsocket.rt_init(null);
            if (engine != 0) {
                // Set callback listener (pass engine parameter)
                rtsocket.rt_set_callback(engine, this);
                initialized = true;

                // Start tick loop thread
                startTickLoop();

                return 0;
            }
            else {
                return -1;
            }
        }
        return 0;
    }

    /**
     * Initialize UDP socket
     */
    private void initializeUdpSocket() {
        try {
            udpChannel = DatagramChannel.open();
            udpChannel.configureBlocking(false);
            udpChannel.socket().bind(new InetSocketAddress(0));
            udpPort = udpChannel.socket().getLocalPort();
            udpSelector = Selector.open();
            udpChannel.register(udpSelector, SelectionKey.OP_READ);
            // 不再创建新线程，selector逻辑集成到tickLoop
        } catch (Exception e) {
            e.printStackTrace();
            throw new RuntimeException("Failed to initialize UDP channel");
        }
    }

    /**
     * Release UDP socket resources
     */
    private void disposeUdpSocket() {
        try {
            if (udpSelector != null) {
                udpSelector.close();
                udpSelector = null;
            }
            if (udpChannel != null) {
                udpChannel.close();
                udpChannel = null;
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        udpPort = 0;
    }

    /**
     * Start tick loop thread
     */
    private void startTickLoop() {
        if (!tickRunning) {
            tickRunning = true;
            tickThread = new Thread(() -> {
                try {
                    tickLoop();
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }, "RTTP-TickLoop");
            tickThread.start();

        }
    }

    /**
     * Tick loop thread function
     */
    private void tickLoop() {
        long lastTick = System.currentTimeMillis();
        while (tickRunning) {
            try {
                long now = System.currentTimeMillis();
                if (now - lastTick >= 5) {
                    rtsocket.rt_tick(engine);
                    lastTick = now;
                }
                // Process API call queue
                processApiCallQueue();

                // UDP selector loop (集成到tickLoop)
                if (udpSelector != null && udpChannel != null) {
                    int readyChannels = udpSelector.select(5);
                    if (readyChannels > 0) {
                        Iterator<SelectionKey> keyIterator = udpSelector.selectedKeys().iterator();
                        while (keyIterator.hasNext()) {
                            SelectionKey key = keyIterator.next();
                            keyIterator.remove();
                            if (key.isReadable()) {
                                processIncomingUdpChannel();
                            }
                        }
                    }
                }
            } catch (java.nio.channels.ClosedSelectorException e) {
                // Selector已关闭，正常退出
                break;
            } catch (Exception e) {
                e.printStackTrace();
                try {
                    Thread.sleep(10);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
        
        // 清理工作
        System.out.println("RTTP tick loop stopped");
    }


    /**
     * Process received UDP packet
     * @param packet Received UDP packet
     */
    private void processIncomingUdpPacket(DatagramPacket packet) {
        try {
            InetAddress senderAddress = packet.getAddress();
            int senderPort = packet.getPort();

            // Create address structure (IP address + port)
            ByteBuffer addrBuffer = ByteBuffer.allocate(6);
            addrBuffer.put(senderAddress.getAddress());
            addrBuffer.putShort((short) senderPort);
            byte[] addrBytes = addrBuffer.array();

            // Call rt_incoming_packet to process packet
            rtsocket.rt_incoming_packet(engine, 
                    packet.getData(), 
                    packet.getLength(), 
                    addrBytes, 
                    addrBytes.length);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    /**
     * Process API call queue
     */
    private void processApiCallQueue() {
        ApiCallTask task;
        while ((task = apiCallQueue.poll()) != null) {
            try {
                task.execute();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    public interface OnRttpEventListener {
        void onConnected();
        void onError(int code, String msg);
        void onSend(byte[] data, int len, int result); // 新增：发送结果通知
        void onReceived(byte[] data, int len);
    }

    private OnRttpEventListener eventListener;

    public void setOnRttpEventListener(OnRttpEventListener listener) {
        this.eventListener = listener;
    }

    // --- API: connect ---
    public void connect(final String serverIp, final int serverPort) {
        apiCallQueue.offer(() -> {
            if (initialized && !connected) {
                try {
                    // 构造有效的 sockaddr_in 结构（16字节，AF_INET, port, addr, 填充0）
                    byte[] addrBytes = new byte[16];
                    addrBytes[0] = 2; // AF_INET
                    addrBytes[1] = 0;
                    addrBytes[2] = (byte) ((serverPort >> 8) & 0xFF);
                    addrBytes[3] = (byte) (serverPort & 0xFF);
                    InetAddress inetAddr = InetAddress.getByName(serverIp);
                    byte[] ipBytes = inetAddr.getAddress();
                    System.arraycopy(ipBytes, 0, addrBytes, 4, 4);
                    // 其余填充0

                    socket = rtsocket.rt_socket(engine, 0);
                    if (socket == 0) {
                        if (eventListener != null) eventListener.onError(-1, "socket create failed");
                        return 0;
                    }
                    int result = rtsocket.rt_connect(engine, socket, addrBytes, addrBytes.length);
                    if (result != 0) {
                        if (eventListener != null) eventListener.onError(result, "connect failed");
                    }
                } catch (Exception e) {
                    if (eventListener != null) eventListener.onError(-1, e.getMessage());
                }
            }
            return 0;
        });

        if (udpSelector != null)
            udpSelector.wakeup();
    }

    // --- API: send (异步) ---
    public void send(final byte[] data, final int length) {
        if (data == null || length <= 0 || length > data.length) return;
        sendQueue.offer(new SendTask(data, length));
        // 主动触发一次写事件处理
        apiCallQueue.offer(() -> {
            processSendQueue();
            return 0;
        });

        if (udpSelector != null)
            udpSelector.wakeup(); // 确保selector不阻塞
    }

    // --- API: receive (异步) ---
    public void receiveAsync(final int maxLen) {
        receiveQueue.offer(new ReceiveTask(maxLen));
        // 主动触发一次读事件处理
        apiCallQueue.offer(() -> {
            processReceiveQueue();
            return 0;
        });

        if (udpSelector != null)
            udpSelector.wakeup(); // 确保selector不阻塞
    }

    // 实际处理发送队列（只在RTTP_EVENT_WRITE或主动触发时调用）
    private void processSendQueue() {
        if (!connected || socket == 0 || eventListener == null)
            return;
        while (!sendQueue.isEmpty() && rtsocket.rt_writable(engine, socket) > 0) {
            SendTask task = sendQueue.peek();
            if (task == null) break;
            while (task.sentBytes < task.length) {
                int willSend = task.length - task.sentBytes;
                // rt_send参数: buffer, len, flag=0
                // 注意：需要从已发送位置开始发送数据，创建子数组
                byte[] sendBuffer = new byte[willSend];
                System.arraycopy(task.data, task.sentBytes, sendBuffer, 0, willSend);
                int ret = rtsocket.rt_send(engine, socket, sendBuffer, willSend, 0);
                //System.out.println("rt_send return " + ret);
                if (ret > 0) {
                    task.sentBytes += ret;
                } else if (ret == -1) {
                    // 不可写，等待下次WRITE事件
                    return;
                } else {
                    // 发送错误，通知并丢弃
                    eventListener.onSend(task.data, task.length, ret);
                    sendQueue.poll();
                    break;
                }
            }
            // 单个SendTask数据全部发完才通知
            if (task.sentBytes == task.length) {
                eventListener.onSend(task.data, task.length, task.length);
                sendQueue.poll();
            }
        }
    }

    // 实际处理接收队列（只在RTTP_EVENT_READ或主动触发时调用）
    private void processReceiveQueue() {
        if (!connected || socket == 0 || eventListener == null)
            return;
        while (!receiveQueue.isEmpty() && rtsocket.rt_readable(engine, socket) > 0) {
            ReceiveTask task = receiveQueue.peek();
            if (task == null) break;
            byte[] buffer = new byte[task.maxLen];
            // rt_recv参数: buffer, len, flag=0
            int ret = rtsocket.rt_recv(engine, socket, buffer, task.maxLen, 0);
            //System.out.println("rt_recv return " + ret);
            if (ret >= 0) {
                eventListener.onReceived(buffer, ret);
                receiveQueue.poll();
            }
            if (ret == -1) {
                // 没有数据可读，等待下次READ事件
                break;
            }
        }
    }

    // --- API: disconnect ---
    public void disconnect() {
        apiCallQueue.offer(() -> {
            if (connected && socket != 0) {
                rtsocket.rt_close(engine, socket);
                socket = 0;
                connected = false;
            }
            return 0;
        });

        if (udpSelector != null)
            udpSelector.wakeup();
    }

    /**
     * Destroy RTTP client and release resources
     */
    public synchronized void uninitialize() {
        if (destroying) {
            return; // 防止重复销毁
        }
        destroying = true;
        
        // 停止tick循环
        tickRunning = false;
        
        // 唤醒可能阻塞的selector
        if (udpSelector != null) {
            udpSelector.wakeup();
        }
        
        // 等待tick线程结束
        try {
            if (tickThread != null) {
                tickThread.join();
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        
        // 在主线程中同步释放资源（与initialize对称）
        if (initialized) {
            // 释放RTTP资源
            rtsocket.rt_unset_callback(engine);
            rtsocket.rt_uninit(engine);
            engine = 0;
            initialized = false;
        }
        
        // 释放UDP资源
        disposeUdpSocket();
        
        // 清空队列
        apiCallQueue.clear();
        sendQueue.clear();
        receiveQueue.clear();
        
        destroying = false;
    }

    /**
     * Check if connected
     * @return true if connected
     */
    public boolean isConnected() {
        return connected;
    }

    /**
     * Check if initialized
     * @return true if initialized
     */
    public boolean isInitialized() {
        return initialized;
    }

    /**
     * Get UDP port
     * @return UDP port number
     */
    public int getUdpPort() {
        return udpPort;
    }

    // Implement rtsocket_listener interface methods
    @Override
    public void on_socket_event(long engine, long socket, int event) {
        //System.out.println("rtsocket event: " + event);
        if (eventListener == null) return;
        switch (event) {
            case rtsocket.RTTP_EVENT_CONNECT:
                connected = true;
                eventListener.onConnected();
                break;
            case rtsocket.RTTP_EVENT_READ:
                processReceiveQueue();
                break;
            case rtsocket.RTTP_EVENT_WRITE:
                processSendQueue();
                break;
            case rtsocket.RTTP_EVENT_ERROR:
                connected = false;
                int err = rtsocket.rt_get_error(engine, socket);
                eventListener.onError(err, "RTTP_EVENT_ERROR");
                break;
        }
    }

    @Override
    public void on_send_data(long engine, long socket, byte[] buffer, int len, byte[] addr, int addr_len) {
        // 按最新的 sockaddr_in 结构解析地址
        if (udpChannel != null && buffer != null && len > 0 && addr != null && addr_len >= 16) {
            try {
                int port = ((addr[2] & 0xFF) << 8) | (addr[3] & 0xFF);
                byte[] ipBytes = new byte[4];
                System.arraycopy(addr, 4, ipBytes, 0, 4);
                InetAddress ipAddress = InetAddress.getByAddress(ipBytes);
                InetSocketAddress endPoint = new InetSocketAddress(ipAddress, port);

                ByteBuffer sendBuf = ByteBuffer.wrap(buffer, 0, len);
                udpChannel.send(sendBuf, endPoint);

                //System.out.println("Sent UDP data to " + endPoint.toString() + ", size=" + len);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    private void processIncomingUdpChannel() {
        try {
            ByteBuffer buffer = ByteBuffer.allocate(MAX_PACKET_SIZE);
            InetSocketAddress sender = (InetSocketAddress) udpChannel.receive(buffer);
            if (sender != null) {
                buffer.flip();
                byte[] data = new byte[buffer.remaining()];
                buffer.get(data);

                // 构造有效的 sockaddr_in 结构（16字节，AF_INET, port, addr, 填充0）
                byte[] addrBytes = new byte[16];
                addrBytes[0] = 2; // AF_INET
                addrBytes[1] = 0;
                int port = sender.getPort();
                addrBytes[2] = (byte) ((port >> 8) & 0xFF);
                addrBytes[3] = (byte) (port & 0xFF);
                byte[] ipBytes = sender.getAddress().getAddress();
                System.arraycopy(ipBytes, 0, addrBytes, 4, 4);
                // 其余填充0

                //System.out.println("Received UDP packet from " + sender.toString() + ", size=" + data.length);

                rtsocket.rt_incoming_packet(engine, data, data.length, addrBytes, addrBytes.length);
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
