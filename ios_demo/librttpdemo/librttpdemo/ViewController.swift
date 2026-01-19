//
//  ViewController.swift
//  librttpdemo
//

import UIKit

class ViewController: UIViewController, RttpEventListener {
    // UI组件
    @IBOutlet weak var serverIpTextField: UITextField!
    @IBOutlet weak var portTextField: UITextField!
    @IBOutlet weak var connectButton: UIButton!
    @IBOutlet weak var disconnectButton: UIButton!
    @IBOutlet weak var statusLabel: UILabel!
    @IBOutlet weak var latencyLabel: UILabel!
    @IBOutlet weak var dataLogTextView: UITextView!
    @IBOutlet weak var statsTextView: UITextView!
    
    // RttpClient实例
    private var rttpClient: RttpClient?
    
    // 定时器
    private var updateTimer: Timer?
    private var sendTimer: Timer?
    
    // 状态变量
    private var isConnected = false
    
    // 统计变量
    private var sentCount = 0
    private var receivedCount = 0
    private var sendingCount = 0
    private var failedCount = 0
    private var totalLatency: Double = 0.0
    private var latencyHistory: [Double] = []
    private var sentPackets: [Int: UInt64] = [:] // 序号 -> 发送时间戳(纳秒)
    private var random = arc4random_uniform
    
    private var nextExpectedBytes = 4

    // 接收数据缓冲区
    private var receiveBuffer = Data()
    
    // 随机数生成器种子
    private var seed: UInt32 = 0
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        // 初始化UI状态
        setupUI()
        
        // 设置默认值
        serverIpTextField.text = "127.0.0.1"
        portTextField.text = "8080"
        
        // 添加键盘隐藏手势
        let tapGesture = UITapGestureRecognizer(target: self, action: #selector(dismissKeyboard))
        view.addGestureRecognizer(tapGesture)
        
        // 初始化RttpClient
        rttpClient = RttpClient()
        let result = rttpClient?.initialize()
        if result != 0 {
            logMessage("RttpClient 初始化失败")
            return
        }
        rttpClient?.setEventListener(self)

        // 启动更新定时器
        updateTimer = Timer.scheduledTimer(timeInterval: 0.01, target: self, selector: #selector(updateRttpClient), userInfo: nil, repeats: true)
    }
    
    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        
        // 清理资源
        cleanup()
    }
    
    // 设置UI
    private func setupUI() {
        connectButton.isEnabled = true
        disconnectButton.isEnabled = false
        statusLabel.text = "状态: 未连接"
        latencyLabel.text = "延迟: -- ms"
        
        self.updateStats()
        //dataLogTextView.text = ""
        //statsTextView.text = "统计信息:\n发送数据包: 0\n接收响应: 0\n平均延迟: - ms\n最小延迟: - ms\n最大延迟: - ms\n样本数量: 0"
    }
    
    // 清理资源
    private func cleanup() {
        // 停止定时器
        updateTimer?.invalidate()
        sendTimer?.invalidate()
        
        // 断开连接
        disconnect()
        
        // 释放RttpClient
        rttpClient?.uninitialize()
        rttpClient = nil
        
        // 重置统计数据
        resetStats()
    }
      
    // MARK: - RttpEventListener Protocol
    
    func onConnected() {
        DispatchQueue.main.async {
            self.statusLabel.text = "状态: 已连接"
            self.isConnected = true
            
            
            // 重置统计数据
            self.resetStats()
            
            // 启动定期发送定时器
            self.sendTimer = Timer.scheduledTimer(timeInterval: 0.2, target: self, selector: #selector(self.handleTimerTick), userInfo: nil, repeats: true)
            
            // 开始异步接收数据
            self.startReceivingData()
        }
    }
    
    func onError(error: Error) {
        DispatchQueue.main.async {
            self.statusLabel.text = "状态: 错误"
            self.isConnected = false
            self.connectButton.isEnabled = true
            self.disconnectButton.isEnabled = false
            
            self.sendTimer?.invalidate()
            self.sendTimer = nil
            
            self.logMessage("错误: \(error.localizedDescription)")
        }
    }
    
    
    func onReceived(data: Data, length: Int32) {
        // 处理接收到的数据
        print("received \(length) bytes")
        
        if length == 0 {
            // 连接关闭
            DispatchQueue.main.async {
                self.disconnect()
                self.showAlert(title: "连接状态", message: "连接已关闭")
            }
            return
        } else if length < 0 {
            DispatchQueue.main.async {
                self.disconnect()
                self.showAlert(title: "连接错误", message: "接收数据失败")
            }
            return
        }
        
        DispatchQueue.main.async {
            self.receiveBuffer.append(data)
            self.processReceivedData()
            
            self.startReceivingData()
        }
    }
    
    func onSend(data: Data, length: Int32, result: Int32) {
        if result == length {
            logMessage("发送了 \(result) 字节数据")
            sendingCount -= 1
        } else {
            onError(error: NSError(domain: "RttpClient", code: Int(result), userInfo: nil))
        }
    }
    
    func onError(error: Int32, message: String) {
        // 将错误转换为Error类型并调用现有的onError方法
        let nsError = NSError(domain: "RttpClient", code: Int(error), userInfo: [NSLocalizedDescriptionKey: message])
        onError(error: nsError)
    }
    
    // 处理接收到的数据
    private func processReceivedData() {
        if nextExpectedBytes == 4 {
            if self.receiveBuffer.count == 4 {
                nextExpectedBytes = Int(self.receiveBuffer[0]) | (Int(self.receiveBuffer[1]) << 8) | (Int(self.receiveBuffer[2]) << 16) | (Int(self.receiveBuffer[3]) << 24)
            } else {
                // 等待更多数据
                return
            }
        } else {
                if self.receiveBuffer.count == nextExpectedBytes {
                
                    // 解析序号（前4字节为包长度，后4字节为序号）
                    let seq = Int(self.receiveBuffer[4]) | (Int(self.receiveBuffer[5]) << 8) | (Int(self.receiveBuffer[6]) << 16) | (Int(self.receiveBuffer[7]) << 24)
                    
                    // 计算延迟
                    if let sentTime = self.sentPackets.removeValue(forKey: seq) {
                        let currentTime = DispatchTime.now().uptimeNanoseconds
                        let latencyMs = Double(currentTime - sentTime) / 1_000_000.0 // 纳秒转毫秒
                        
                        latencyLabel.text = String(format: "延迟: %.2f ms", latencyMs)
                        
                        // 保持latencyHistory最多只有10条记录
                        if self.latencyHistory.count >= 10 {
                            let removedLatency = self.latencyHistory.removeFirst()
                            self.totalLatency -= removedLatency
                        }
                        self.latencyHistory.append(latencyMs)
                        self.totalLatency += latencyMs
                        self.receivedCount += 1
                        
                        self.updateStats()
                    }
                    
                    self.nextExpectedBytes = 4
                    self.receiveBuffer = Data()
                    

                } else {
                    // 等待更多数据
                    return
                }
        }
    }
    
    // 更新统计信息显示
    private func updateStats() {
        let avgLatency = self.latencyHistory.count > 0 ? totalLatency / Double(self.latencyHistory.count) : 0.0
        let sampleCount = latencyHistory.count
        
        let statsText = """
        RTTP Ver: \(RTSocketAPI.rt_get_version()!)
        
        统计信息:
        发送数据包: \(sentCount)
        接收响应: \(receivedCount)
        平均延迟: \(String(format: "%.2f", avgLatency)) ms
        样本数量: \(sampleCount)
        """
        
        // 安全检查statsTextView是否存在
        guard let statsTextView = statsTextView else {
            print("Error: statsTextView is nil")
            return
        }
        statsTextView.text = statsText
    }
    
    // 重置统计数据
    private func resetStats() {
        sentCount = 0
        receivedCount = 0
        sendingCount = 0
        failedCount = 0
        totalLatency = 0.0
        latencyHistory.removeAll()
        sentPackets.removeAll()
    }
    

    
    // 连接按钮点击事件
    @IBAction func connectButtonTapped(_ sender: UIButton) {
        guard let serverIp = serverIpTextField.text, !serverIp.isEmpty else {
            showAlert(title: "错误", message: "请输入服务器IP")
            return
        }
        
        guard let portText = portTextField.text, let port = UInt16(portText), port > 0 && port <= 65535 else {
            showAlert(title: "错误", message: "请输入有效的端口号")
            return
        }
        
        // 隐藏键盘
        dismissKeyboard()
        
        resetStats()
        
        nextExpectedBytes = 4
        receiveBuffer = Data()
        
        // 初始化并连接到服务器
        if let client = rttpClient {
            
            client.connect(to: serverIp, serverPort: port)
            
            // 更新UI状态
            connectButton.isEnabled = false
            disconnectButton.isEnabled = true
            statusLabel.text = "状态: 连接中..."
        }
    }
    
    // 定时器处理函数
    @objc private func handleTimerTick() {

        DispatchQueue.main.async {
            if self.sendingCount < 10 {
                self.sendRandomData()
            }

            self.updateStats()
        }
    }
    
    // 发送随机数据
    @objc private func sendRandomData() {
        guard isConnected, let client = rttpClient else { return }
        
        // 限制发送队列大小，避免发送堆积过多
        if sendingCount >= 10 {
            return
        }
        
        // 参考Android Demo，生成不同大小的数据包
        let packetSizes = [100, 300, 500, 800, 1100]
        let randIndex = Int(arc4random_uniform(UInt32(packetSizes.count)))
        let packetSize = packetSizes[randIndex]
        var data = Data(count: packetSize) // 数据包大小
        
        // 使用更简洁的方式构造包头
        data.withUnsafeMutableBytes { (rawBuffer: UnsafeMutableRawBufferPointer) in
            // 检查baseAddress是否存在
            guard let buffer = rawBuffer.bindMemory(to: UInt8.self).baseAddress else {
                print("Error: Unable to get buffer base address")
                return
            }
            
            // 写入包大小（前4字节）
            buffer[0] = UInt8(packetSize & 0xFF)
            buffer[1] = UInt8((packetSize >> 8) & 0xFF)
            buffer[2] = UInt8((packetSize >> 16) & 0xFF)
            buffer[3] = UInt8((packetSize >> 24) & 0xFF)
            
            // 写入序号（第5-8字节）
            let seq = sentCount + 1
            buffer[4] = UInt8(seq & 0xFF)
            buffer[5] = UInt8((seq >> 8) & 0xFF)
            buffer[6] = UInt8((seq >> 16) & 0xFF)
            buffer[7] = UInt8((seq >> 24) & 0xFF)
            
            // 填充数据内容
            let intNum = packetSize / 4
            for i in 2..<intNum {
                let val = i
                let offset = i * 4
                buffer[offset] = UInt8(val & 0xFF)
                buffer[offset + 1] = UInt8((val >> 8) & 0xFF)
                buffer[offset + 2] = UInt8((val >> 16) & 0xFF)
                buffer[offset + 3] = UInt8((val >> 24) & 0xFF)
            }
        }
        
        // 记录发送时间和序号
        sentPackets[sentCount + 1] = DispatchTime.now().uptimeNanoseconds
        sentCount += 1
        sendingCount += 1
        
        // 发送数据
        client.send(data: data)
    }
    
    // 断开连接按钮点击事件
    @IBAction func disconnectButtonTapped(_ sender: UIButton) {
        disconnect()
    }
    
    // 重置UI状态
    private func resetUI() {
        serverIpTextField.isEnabled = true
        portTextField.isEnabled = true
        connectButton.isEnabled = true
        disconnectButton.isEnabled = false
        connectButton.setTitle("Connect", for: .normal)
        statusLabel.text = "状态: 未连接"
        statusLabel.textColor = UIColor.systemGray
    }
    
    // 断开连接
    private func disconnect() {
        // 停止定时器
        updateTimer?.invalidate()
        sendTimer?.invalidate()
        updateTimer = nil
        sendTimer = nil
        
        // 断开RTTP连接
        if isConnected {
            rttpClient?.disconnect()
            isConnected = false
        }
        
        // 重置UI
        resetUI()
    }
    
    // 发送数据按钮点击事件
    @IBAction func sendDataButtonTapped(_ sender: UIButton) {
        // 发送随机数据包
        sendRandomData()
    }
    
    // 开始异步接收数据
    private func startReceivingData() {
        guard isConnected, let client = rttpClient else { return }
        
        let curCount = receiveBuffer.count
        assert(curCount < nextExpectedBytes)
        let shouldRecvBytes =  nextExpectedBytes - curCount
        client.receive(maxLength: shouldRecvBytes);
    }
    
    // 更新RttpClient状态
    @objc private func updateRttpClient() {
        // RttpClient内部自动处理tick循环，无需手动调用
        // 延迟信息通过统计计算得出
        DispatchQueue.main.async {
            // 延迟信息已经在updateStats中更新，这里不需要额外处理
        }
    }
    
    // 显示警告
    private func showAlert(title: String, message: String) {
        let alert = UIAlertController(title: title, message: message, preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "确定", style: .default, handler: nil))
        present(alert, animated: true, completion: nil)
    }
    
    // 隐藏键盘
    @objc private func dismissKeyboard() {
        view.endEditing(true)
    }
    
    // 记录日志
    private func logMessage(_ message: String) {
        /*
        DispatchQueue.main.async {
            let timestamp = DateFormatter.localizedString(from: Date(), dateStyle: .none, timeStyle: .medium)
            self.dataLogTextView.text.append("[\(timestamp)] \(message)\n")
            
            // 滚动到底部
            let bottomRange = NSMakeRange(self.dataLogTextView.text.count - 1, 1)
            self.dataLogTextView.scrollRangeToVisible(bottomRange)
        }
         */
    }
    
    // 资源清理
    deinit {
        // 停止定时器
        updateTimer?.invalidate()
        sendTimer?.invalidate()
        
        // 断开连接
        if isConnected {
            rttpClient?.disconnect()
        }
        
        // 释放RttpClient
        rttpClient?.uninitialize()
        rttpClient = nil
        
        // 重置统计数据
        resetStats()
    }
}

