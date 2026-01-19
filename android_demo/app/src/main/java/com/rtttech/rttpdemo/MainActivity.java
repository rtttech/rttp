package com.rtttech.rttpdemo;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import com.rtttech.rttp.RttpClient;
import com.rtttech.rttp.rtsocket;

import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.Random;
import java.util.concurrent.ConcurrentHashMap;

public class MainActivity extends AppCompatActivity {

    // UI组件
    private EditText ipEditText;
    private EditText portEditText;
    private Button connectButton;
    private TextView statusText;
    private TextView latencyText;
    private TextView statsText;

    // RTTP客户端
    private RttpClient rttpClient;
    private boolean isConnected = false;

    // 数据发送配置
    private int sendInterval = 200; // 与Java Demo保持一致，200ms发送一次
    private Handler timerHandler;
    private Runnable timerRunnable;
    private Random random;

    // 延迟统计
    private ConcurrentHashMap<Long, Long> sentPackets;
    private ArrayList<Float> latencyHistory;
    private double totalLatency;
    private int receivedCount;
    private int sentCount;
    private int sendingCount;
    private int failedCount;
    private static final int MAX_LATENCY_SAMPLES = 10; // 最多保留10个样本

    // 用于缓存分包接收的数据
    private ByteArrayOutputStream recvBuffer;
    private int expectedPacketSize;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // 初始化定时器
        timerHandler = new Handler(Looper.getMainLooper());
        random = new Random();

        // 初始化统计数据
        sentPackets = new ConcurrentHashMap<>();
        latencyHistory = new ArrayList<>();
        totalLatency = 0;
        receivedCount = 0;
        sentCount = 0;
        sendingCount = 0;
        failedCount = 0;

        // 初始化分包接收缓存
        recvBuffer = new ByteArrayOutputStream();
        expectedPacketSize = -1;

        // 初始化UI组件
        initUI();

        initRttpClient();
    }

    /**
     * 初始化UI组件
     */
    private void initUI() {
        ipEditText = findViewById(R.id.ipEditText);
        portEditText = findViewById(R.id.portEditText);
        connectButton = findViewById(R.id.connectButton);
        statusText = findViewById(R.id.statusText);
        latencyText = findViewById(R.id.latencyText);
        statsText = findViewById(R.id.statsText);

        // 设置连接按钮点击事件
        connectButton.setOnClickListener(v -> toggleConnection());

        // 更新统计信息
        updateStats();
    }

    private void initRttpClient() {
        rttpClient = new RttpClient();
        
        int initResult = rttpClient.initialize();
        if (initResult != 0) {
            runOnUiThread(() -> {
                Toast.makeText(MainActivity.this, "RTTP客户端初始化失败，错误码: " + initResult, Toast.LENGTH_SHORT).show();
                resetUI();
            });
            return;
        }
        
        rttpClient.setOnRttpEventListener(new RttpClient.OnRttpEventListener() {
            @Override
            public void onConnected() {
                runOnUiThread(() -> {
                    isConnected = true;
                    statusText.setText("已连接");
                    statusText.setTextColor(getResources().getColor(android.R.color.holo_green_dark));
                    connectButton.setText("断开连接");
                    ipEditText.setEnabled(false);
                    portEditText.setEnabled(false);
                    connectButton.setEnabled(true);

                    // 连接成功时清空之前的统计信息
                    resetStats();

                    startTimer();
                    startReceivingData();
                    Toast.makeText(MainActivity.this, "连接成功", Toast.LENGTH_SHORT).show();
                });
            }



            @Override
            public void onError(int code, String msg) {
                runOnUiThread(() -> {
                    Toast.makeText(MainActivity.this, "错误: " + code + " " + msg, Toast.LENGTH_SHORT).show();
                    // 错误情况下重置统计信息
                    resetStats();
                    resetUI();
                });
            }

            @Override
            public void onSend(byte[] data, int len, int result) {
                // 可选: 统计发送结果
                if (result == len) {
                    sentCount++;
                    sendingCount--;
                } else {
                    onError(result, "发送失败");
                }
            }

            @Override
            public void onReceived(byte[] data, int len) {
                if (len <= 0) {
                    if (len == 0) {
                        onError(0, "连接关闭");
                    } else {
                        onError(len, "接收数据错误");
                    }
                    return;
                }
                try {
                    // 累加到缓存
                    recvBuffer.write(data, 0, len);

                    while (true) {
                        byte[] buf = recvBuffer.toByteArray();
                        int bufLen = buf.length;

                        // 至少要有包头
                        if (expectedPacketSize < 0 && bufLen >= 4) {
                            expectedPacketSize = ((buf[0] & 0xFF)) |
                                                 ((buf[1] & 0xFF) << 8) |
                                                 ((buf[2] & 0xFF) << 16) |
                                                 ((buf[3] & 0xFF) << 24);
                            if (expectedPacketSize < 4 || expectedPacketSize > 4096) {
                                // 无效包长度，重置缓存
                                recvBuffer.reset();
                                expectedPacketSize = -1;
                                onError(-1, "无效包长度");
                                return;
                            }
                        }

                        // 包体是否完整
                        if (expectedPacketSize > 0 && bufLen >= expectedPacketSize) {
                            // 拷贝一个完整包
                            byte[] packet = new byte[expectedPacketSize];
                            System.arraycopy(buf, 0, packet, 0, expectedPacketSize);

                            // 处理包
                            processReceivedData(packet, expectedPacketSize);

                            // 移除已处理数据
                            byte[] remain = null;
                            if (bufLen > expectedPacketSize) {
                                remain = new byte[bufLen - expectedPacketSize];
                                System.arraycopy(buf, expectedPacketSize, remain, 0, remain.length);
                            }
                            recvBuffer.reset();
                            if (remain != null) {
                                recvBuffer.write(remain);
                            }
                            // 继续处理下一个包
                            expectedPacketSize = -1;
                        } else {
                            // 数据不够，等待下次
                            break;
                        }
                    }
                    startReceivingData();
                } catch (Exception e) {
                    e.printStackTrace();
                    recvBuffer.reset();
                    expectedPacketSize = -1;
                    onError(-1, "接收数据异常");
                }
            }
        });

    }

    /**
     * 切换连接状态
     */
    private void toggleConnection() {
        if (isConnected) {
            disconnect();
        } else {
            connect();
        }
    }

    /**
     * 连接到RTTP服务器
     */
    private void connect() {
        String serverIp = ipEditText.getText().toString().trim();
        String portStr = portEditText.getText().toString().trim();

        if (serverIp.isEmpty() || portStr.isEmpty()) {
            Toast.makeText(this, "请输入服务器IP和端口", Toast.LENGTH_SHORT).show();
            return;
        }

        int serverPort;
        try {
            serverPort = Integer.parseInt(portStr);
            if (serverPort <= 0 || serverPort > 65535) {
                throw new NumberFormatException("端口号必须在1-65535之间");
            }
        } catch (NumberFormatException e) {
            Toast.makeText(this, "端口号格式错误: " + e.getMessage(), Toast.LENGTH_SHORT).show();
            return;
        }

        ipEditText.setEnabled(false);
        portEditText.setEnabled(false);
        connectButton.setEnabled(false);
        connectButton.setText("连接中...");

        // RTTP客户端API是非阻塞的，连接结果通过回调处理
        rttpClient.connect(serverIp, serverPort);
    }

    /**
     * 断开连接
     */
    private void disconnect() {
        stopTimer();
        
        rttpClient.disconnect();
        isConnected = false;

        resetUI();
    }

    /**
     * 重置UI状态
     */
    private void resetUI() {
        ipEditText.setEnabled(true);
        portEditText.setEnabled(true);
        connectButton.setEnabled(true);
        connectButton.setText("连接");
        
        // 重置连接状态显示
        isConnected = false;
        statusText.setText("未连接");
        statusText.setTextColor(getResources().getColor(android.R.color.darker_gray));
        
        // 重置统计信息
        sendingCount = 0;
        failedCount = 0;
    }

    /**
     * 启动定时器
     */
    private void startTimer() {
        timerRunnable = new Runnable() {
            @Override
            public void run() {
                handleTimerTick();
                timerHandler.postDelayed(this, sendInterval);
            }
        };
        timerHandler.post(timerRunnable);
    }

    /**
     * 停止定时器
     */
    private void stopTimer() {
        if (timerRunnable != null) {
            timerHandler.removeCallbacks(timerRunnable);
            timerRunnable = null;
        }
    }

    /**
     * 定时器回调处理
     */
    private void handleTimerTick() {
        sendData();
        updateLatencyDisplay();
        updateStats();
    }

    /**
     * 发送随机数据
     */
    private void sendData() {
        if (!isConnected) {
            return;
        }
        
        if (sendingCount >= 10) {
            // 避免发送堆积过多
            return;
        }
        
        try {
            // 参考 Java Demo，生成不同大小的数据包
            int[] packetSizes = {100, 300, 500, 800, 1100};
            int randBytes = packetSizes[random.nextInt(packetSizes.length)];
            int intNum = randBytes / 4;
            byte[] data = new byte[randBytes];

            // 包头: 前4字节为包长度
            data[0] = (byte) (randBytes & 0xFF);
            data[1] = (byte) ((randBytes >> 8) & 0xFF);
            data[2] = (byte) ((randBytes >> 16) & 0xFF);
            data[3] = (byte) ((randBytes >> 24) & 0xFF);

            // 序号: 第5-8字节
            int seq = sentCount + 1;
            data[4] = (byte) (seq & 0xFF);
            data[5] = (byte) ((seq >> 8) & 0xFF);
            data[6] = (byte) ((seq >> 16) & 0xFF);
            data[7] = (byte) ((seq >> 24) & 0xFF);

            // 随机内容
            for (int i = 2; i < intNum; ++i) {
                int val = i;
                int offset = i * 4;
                data[offset] = (byte) (val & 0xFF);
                data[offset + 1] = (byte) ((val >> 8) & 0xFF);
                data[offset + 2] = (byte) ((val >> 16) & 0xFF);
                data[offset + 3] = (byte) ((val >> 24) & 0xFF);
            }

            // 记录发送的数据包（使用序号作为键，纳秒时间戳作为值）
            sentPackets.put((long) seq, System.nanoTime());
            
            // 发送数据
            rttpClient.send(data, data.length);
            
            sendingCount++;
            // 发送结果由回调统计
        } catch (Exception e) {
            e.printStackTrace();
            failedCount++;
            runOnUiThread(this::updateStats);
        }
    }

    /**
     * 开始异步接收数据
     */
    private void startReceivingData() {
        if (isConnected) {
            rttpClient.receiveAsync(4096);
        }
    }

    /**
     * 处理接收到的数据
     */
    private void processReceivedData(byte[] data, int len) {
        
        
        // 解析包长度
        int packetSize = ((data[0] & 0xFF)) |
                        ((data[1] & 0xFF) << 8) |
                        ((data[2] & 0xFF) << 16) |
                        ((data[3] & 0xFF) << 24);
        
        // 解析序号
        int seq = ((data[4] & 0xFF)) |
                 ((data[5] & 0xFF) << 8) |
                 ((data[6] & 0xFF) << 16) |
                 ((data[7] & 0xFF) << 24);
        
        // 计算延迟
        Long sentTime = sentPackets.remove((long) seq);
        if (sentTime != null) {
            float latency = (System.nanoTime() - sentTime) / 1000000.0f; // 纳秒转毫秒（支持小数点）
            
            synchronized (latencyHistory) {
                // 确保只保留最新的MAX_LATENCY_SAMPLES个样本
                if (latencyHistory.size() >= MAX_LATENCY_SAMPLES) {
                    // 移除最早的样本
                    float oldestLatency = latencyHistory.remove(0);
                    totalLatency -= oldestLatency;
                }
                
                // 添加新样本
                latencyHistory.add(latency);
                totalLatency += latency;
            }
            
            receivedCount++;
            
            
            // 更新UI
            runOnUiThread(this::updateStats);
        }
    }

    /**
     * 更新延迟显示
     */
    private void updateLatencyDisplay() {
        // 添加空值检查以防止空指针异常
        if (latencyHistory == null) {
            if (latencyText != null) {
                latencyText.setText("- ms");
            }
            return;
        }
        
        synchronized (latencyHistory) {
            if (latencyHistory.isEmpty()) {
                if (latencyText != null) {
                    latencyText.setText("- ms");
                }
                return;
            }

            float averageLatency = (float) (totalLatency / latencyHistory.size());
            if (latencyText != null) {
                latencyText.setText(String.format("%.2f ms", averageLatency));
            }
        }
    }

    /**
     * 更新统计信息
     */
    private void updateStats() {
        // 添加空值检查以防止空指针异常
        if (latencyHistory == null) {
            return;
        }
        
        StringBuilder sb = new StringBuilder();
        sb.append("RTTP Ver: ").append(rtsocket.rt_get_version()).append("\n\n");
        sb.append("统计信息：\n");
        sb.append("发送数据包：").append(sentCount).append("\n");
        sb.append("接收响应：").append(receivedCount).append("\n");
        
        synchronized (latencyHistory) {
            if (!latencyHistory.isEmpty()) {
                float averageLatency = (float) (totalLatency / latencyHistory.size());
                float minLatency = Float.MAX_VALUE;
                float maxLatency = Float.MIN_VALUE;
                
                // 手动计算最小和最大延迟（避免使用Stream API）
                for (float latency : latencyHistory) {
                    if (latency < minLatency) minLatency = latency;
                    if (latency > maxLatency) maxLatency = latency;
                }
                
                sb.append("平均延迟：").append(String.format("%.2f", averageLatency)).append(" ms\n");
                sb.append("最小延迟：").append(String.format("%.2f", minLatency)).append(" ms\n");
                sb.append("最大延迟：").append(String.format("%.2f", maxLatency)).append(" ms\n");
                sb.append("样本数量：").append(latencyHistory.size()).append("\n");
            } else {
                sb.append("平均延迟：- ms\n");
                sb.append("最小延迟：- ms\n");
                sb.append("最大延迟：- ms\n");
                sb.append("样本数量：0\n");
            }
        }

        if (statsText != null) {
            statsText.setText(sb.toString());
        }
    }

    /**
     * 重置统计数据
     */
    private void resetStats() {
        if (sentPackets != null) {
            sentPackets.clear();
        }
        
        // 添加空值检查以防止空指针异常
        if (latencyHistory != null) {
            synchronized (latencyHistory) {
                latencyHistory.clear();
                totalLatency = 0;
            }
        }
        
        receivedCount = 0;
        sentCount = 0;
        sendingCount = 0;
        failedCount = 0;
        
        if (latencyText != null) {
            latencyText.setText("- ms");
        }
        
        updateStats();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // 确保停止定时器
        stopTimer();
        
        // 确保断开连接和释放资源
        if (isConnected) {
            rttpClient.disconnect();
        }

        try {
            rttpClient.uninitialize();
        } catch (Exception e) {
            e.printStackTrace();
        }
        
        // 清理Handler
        if (timerHandler != null) {
            timerHandler.removeCallbacksAndMessages(null);
        }
    }
}



