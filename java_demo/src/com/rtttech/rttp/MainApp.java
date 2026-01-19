package com.rtttech.rttp;

import javax.swing.*;
import java.util.ArrayList;
import java.util.Random;
import java.util.concurrent.ConcurrentHashMap;
import java.io.ByteArrayOutputStream;

public class MainApp {
    private JFrame frame;
    private JTextField ipField;
    private JTextField portField;
    private JButton connectButton;
    private JLabel statusLabel;
    private JComboBox<String> frequencyCombo;
    private JLabel latencyLabel;
    private JLabel statsLabel;

    private RttpClient rttpClient;
    private boolean isConnected = false;

    private int sendInterval = 1000;
    private Timer timer;
    private Random random = new Random();

    private ConcurrentHashMap<Long, Long> sentPackets = new ConcurrentHashMap<>();
    private ArrayList<Long> latencyHistory = new ArrayList<>();
    private long totalLatency = 0;
    private int receivedCount = 0;
    private int sentCount = 0;
    private int sendingCount = 0;

    private String version;

    // 用于缓存分包接收的数据
    private ByteArrayOutputStream recvBuffer = new ByteArrayOutputStream();
    private int expectedPacketSize = -1;

    public MainApp() {
        initUI();
        initRttpClient();
    }

    private void initUI() {
        frame = new JFrame("RTTP Java Demo");
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        frame.setSize(400, 350);
        frame.setLayout(null);

        ipField = new JTextField("127.0.0.1");
        ipField.setBounds(20, 20, 120, 25);
        frame.add(ipField);

        portField = new JTextField("9000");
        portField.setBounds(150, 20, 60, 25);
        frame.add(portField);

        connectButton = new JButton("连接");
        connectButton.setBounds(220, 20, 100, 25);
        frame.add(connectButton);

        statusLabel = new JLabel("未连接");
        statusLabel.setBounds(20, 60, 120, 25);
        frame.add(statusLabel);

        latencyLabel = new JLabel("延迟: - ms");
        latencyLabel.setBounds(20, 100, 120, 25);
        frame.add(latencyLabel);

        statsLabel = new JLabel("统计信息：");
        statsLabel.setBounds(20, 140, 300, 100);
        frame.add(statsLabel);

        connectButton.addActionListener(e -> toggleConnection());

        frame.setVisible(true);

        version = rtsocket.rt_get_version();

        updateStats();
    }

    private void initRttpClient() {

        rttpClient = new RttpClient();

        if (rttpClient.initialize() != 0) {
            connectButton.setEnabled(false);
            JOptionPane.showMessageDialog(frame, "初始化RTTP失败");
            return;
        }

        rttpClient = new RttpClient();
        rttpClient.setOnRttpEventListener(new RttpClient.OnRttpEventListener() {
            @Override
            public void onConnected() {
                SwingUtilities.invokeLater(() -> {
                    isConnected = true;
                    statusLabel.setText("已连接");
                    connectButton.setText("断开连接");
                    ipField.setEnabled(false);
                    portField.setEnabled(false);
                    connectButton.setEnabled(true);
                    startTimer();
                    startReceivingData();
                });
            }

            @Override
            public void onError(int code, String msg) {
                handleConnectionError(code, msg);
            }

            @Override
            public void onSend(byte[] data, int len, int result) {
                sendingCount--;
                if (result == len) {
                    sentCount++;
                } else {
                    handleConnectionError(result, "发送数据失败");
                }
                
            }

            @Override
            public void onReceived(byte[] data, int len) {
                if (len <= 0)
                    return;
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
                                //出现严重错误，报告失败，断开连接，TODO
                                handleConnectionError(-1, "收到无效包长度: " + expectedPacketSize);
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

                    //继续接收数据
                    rttpClient.receiveAsync(4096);

                } catch (Exception e) {
                    e.printStackTrace();
                    recvBuffer.reset();
                    expectedPacketSize = -1;
                }
            }
        });
    }

    private void toggleConnection() {
        if (isConnected) {
            disconnect();
        } else {
            connect();
        }
    }

    private void connect() {
        String serverIp = ipField.getText().trim();
        String portStr = portField.getText().trim();
        if (serverIp.isEmpty() || portStr.isEmpty()) {
            JOptionPane.showMessageDialog(frame, "请输入服务器IP和端口");
            return;
        }
        int serverPort = Integer.parseInt(portStr);

        ipField.setEnabled(false);
        portField.setEnabled(false);
        connectButton.setEnabled(false);
        connectButton.setText("连接中...");

        try {
            int initResult = rttpClient.initialize();
            if (initResult != 0) {
                SwingUtilities.invokeLater(() -> {
                    JOptionPane.showMessageDialog(frame, "初始化失败");
                    resetUI();
                });
                return;
            }
            rttpClient.connect(serverIp, serverPort);
        } catch (Exception e) {
            e.printStackTrace();
            SwingUtilities.invokeLater(() -> {
                JOptionPane.showMessageDialog(frame, "连接失败: " + e.getMessage());
                resetUI();
            });
        }

    }

    private void disconnect() {
        if (!isConnected) return;
        isConnected = false;

        stopTimer();
        
        rttpClient.disconnect();

        resetUI();
    }

    private void resetUI() {
        ipField.setEnabled(true);
        portField.setEnabled(true);
        connectButton.setEnabled(true);
        connectButton.setText("连接");
        statusLabel.setText("未连接");
    }


    private void startTimer() {
        timer = new Timer(200, e -> handleTimerTick());
        timer.start();
    }

    private void handleTimerTick() {
        sendData();
        
        updateLatencyDisplay();
        updateStats();
    }

    private void startReceivingData() {
        rttpClient.receiveAsync(4096);
    }

    private void stopTimer() {
        if (timer != null) {
            timer.stop();
            timer = null;
        }
    }

    private void sendData() {
        if (!isConnected) return;

        if (sendingCount >= 10) {
            // 避免发送堆积过多
            return;
        }

        try {
            // 参考 C++ 版，生成包头+序号+随机内容
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

            sentPackets.put((long) seq, System.nanoTime());
            rttpClient.send(data, data.length);

            //System.out.println("send " + seq + " size " + randBytes);

            sendingCount++;
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private void processReceivedData(byte[] data, int length) {
        if (length < 8) return;
        // 参考 C++ 版，解析包头和序号
        int packetSize = ((data[0] & 0xFF)) |
                         ((data[1] & 0xFF) << 8) |
                         ((data[2] & 0xFF) << 16) |
                         ((data[3] & 0xFF) << 24);
        int seq = ((data[4] & 0xFF)) |
                  ((data[5] & 0xFF) << 8) |
                  ((data[6] & 0xFF) << 16) |
                  ((data[7] & 0xFF) << 24);

        Long sendTime = sentPackets.remove((long) seq);
        if (sendTime != null) {
            receivedCount++;
            long latency = (System.nanoTime() - sendTime) / 1000000; // ms
            synchronized (latencyHistory) {
                latencyHistory.add(latency);
                totalLatency += latency;
                if (latencyHistory.size() > 10) {
                    long removedLatency = latencyHistory.remove(0);
                    totalLatency -= removedLatency;
                }
            }
        }
    }

    private void updateLatencyDisplay() {
        if (latencyHistory == null) {
            latencyLabel.setText("延迟: - ms");
            return;
        }
        synchronized (latencyHistory) {
            if (latencyHistory.isEmpty()) {
                latencyLabel.setText("延迟: - ms");
                return;
            }
            long averageLatency = totalLatency / latencyHistory.size();
            latencyLabel.setText("延迟: " + averageLatency + " ms");
        }
    }

    private void updateStats() {
        if (latencyHistory == null) return;
        StringBuilder sb = new StringBuilder();
        sb.append("<html>");
        sb.append("RTTP Version: ").append(version).append("<br>");
        sb.append("统计信息：<br>");

        sb.append("发送数据包：").append(sentCount).append("<br>");
        sb.append("接收响应：").append(receivedCount).append("<br>");
        
        synchronized (latencyHistory) {
            if (!latencyHistory.isEmpty()) {
                long averageLatency = totalLatency / latencyHistory.size();
                sb.append("平均延迟：").append(averageLatency).append(" ms<br>");
            } else {
                sb.append("平均延迟：- ms<br>");
            }
        }
        sb.append("</html>");
        statsLabel.setText(sb.toString());
    }

    private void resetStats() {
        sentPackets.clear();
        synchronized (latencyHistory) {
            latencyHistory.clear();
            totalLatency = 0;
        }
        receivedCount = 0;
        sentCount = 0;
        sendingCount = 0;
        latencyLabel.setText("延迟: - ms");
        updateStats();
    }

    private void handleConnectionError(int err, String message) {
        isConnected = false;
        SwingUtilities.invokeLater(() -> {
            String errType;
            if (err == 0) {
                errType = "连接关闭:";
            } else {
                errType = "连接错误:";
            }
            JOptionPane.showMessageDialog(frame, errType + message);
            resetUI();
            resetStats();
        });
    }

    public static void main(String[] args) {

        SwingUtilities.invokeLater(MainApp::new);
    }
}