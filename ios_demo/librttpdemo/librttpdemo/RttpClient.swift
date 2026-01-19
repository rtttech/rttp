import Foundation
import Darwin

// Task structures for async operations
private class SendTask {
    let data: Data
    let length: Int
    var sentBytes: Int = 0
    
    init(data: Data, length: Int) {
        self.data = data
        self.length = length
    }
}

private class ReceiveTask {
    let maxLength: Int
    
    init(maxLength: Int) {
        self.maxLength = maxLength
    }
}

// API call task interface (使用闭包替代)
typealias ApiCallTask = () -> Void

// C-compatible callback functions
private func globalSocketEventCallback(engine: RTEngine, socket: RTSOCKET, event: Int32) {
    guard let enginePtr = engine else { return }
    let clientPtr = RTSocketAPI.rt_get_userdata(enginePtr, nil, 0)

    guard let clientPtr = clientPtr else { return }
    let client = Unmanaged<RttpClient>.fromOpaque(clientPtr).takeUnretainedValue()
    client.handleSocketEvent(engine: engine, socket: socket, event: event)
    
}

// C-compatible callback for sending data
private func globalSendDataCallback(engine: RTEngine, socket: RTSOCKET, buffer: UnsafePointer<Int8>?, len: Int32, addr: UnsafePointer<sockaddr>?, addr_len: Int32) {
    guard let enginePtr = engine, let buffer = buffer else { return }
    let clientPtr = RTSocketAPI.rt_get_userdata(enginePtr, nil, 0)

    guard let clientPtr = clientPtr else { return }
    let client = Unmanaged<RttpClient>.fromOpaque(clientPtr).takeUnretainedValue()
    client.sendUdpPacket(engine: engine, socket: socket, buffer: buffer, len: len, addr: addr, addr_len: addr_len)
    
}

protocol RttpEventListener {
    func onConnected()
    func onReceived(data: Data, length: Int32)
    func onSend(data: Data, length: Int32, result: Int32)
    func onError(error: Int32, message: String)
}

class RttpClient {
    private var engine: RTEngine?
    private var socket: RTSOCKET?
    private var udpSocket: Int32 = -1
    private var eventListener: RttpEventListener?
    private var isRunning = false
    private var isInitialized = false
    private var isDestroying = false
    private let udpPort: UInt16 = 0
    private var serverPort: UInt16?
    private var serverIp: String?
    private var sessionId: String?
    private var isConnected = false
    
    // Async queues for operations
    private var sendQueue = [SendTask]()
    private let sendQueueLock = NSLock()
    private var receiveQueue = [ReceiveTask]()
    private let receiveQueueLock = NSLock()
    private var apiTasks: [ApiCallTask] = []
    private let apiTaskLock = NSLock()
    
    init() {
        
    }

    deinit {
        uninitialize()
    }


    // MARK: - Initialization and Configuration
    
    func initialize() -> Int {
        apiTaskLock.lock()
        defer { apiTaskLock.unlock() }
        
        if isDestroying {
            return -1
        }
        
        if !isInitialized {
            // Initialize UDP socket
            if !initializeUdpSocket() {
                return -1
            }
            
            // Initialize RTTP engine using RTSocketAPI wrapper
            engine = RTSocketAPI.rt_init("")
            if let engine = engine {
                RTSocketAPI.rt_set_userdata(engine, nil, 0, UnsafeMutableRawPointer(Unmanaged.passUnretained(self).toOpaque()))
            }
            guard engine != nil else {
                print("Failed to initialize RTTP engine")
                closeUdpSocket()
                return -1
            }
            
            // Set callbacks using RTSocketAPI wrapper
            if let engine = engine, let enginePtr = engine {
                
                // Use RTSocketAPI wrapper for callback setup
                RTSocketAPI.rt_set_callback(engine, 
                                          event_callback: globalSocketEventCallback, 
                                          sendproc: globalSendDataCallback)
            }
            
            
            
            isInitialized = true
            
            // Start DispatchSource-based event loop
            startDispatchSources()
            
            return 0
        }
        return 0
    }

    func uninitialize() {
        // 防止重复销毁
        apiTaskLock.lock()
        if isDestroying {
            apiTaskLock.unlock()
            return
        }
        isDestroying = true
        apiTaskLock.unlock()
        
        // 停止DispatchSource事件循环
        stopDispatchSources()
        
        // 在主线程中同步释放资源（与initialize对称）
        if isInitialized {
            // 释放RTTP资源 using RTSocketAPI wrapper
            if let engine = engine {
                // Note: RTSocketAPI doesn't have rt_unset_callback, so we'll skip that
                RTSocketAPI.rt_uninit(engine)
                self.engine = nil
            }
            
            isInitialized = false
        }
        
        // 释放UDP资源
        closeUdpSocket()
        
        // 清空队列
        clearQueues()
        
        // 重置标志
        isDestroying = false
    }
    
    private func clearQueues() {
         apiTaskLock.lock()
         apiTasks.removeAll()
         apiTaskLock.unlock()
         
         sendQueueLock.lock()
         sendQueue.removeAll()
         sendQueueLock.unlock()
         
         receiveQueueLock.lock()
         receiveQueue.removeAll()
         receiveQueueLock.unlock()
         
     }
     
     func destroy() {
        uninitialize()
    }

    func setEventListener(_ listener: RttpEventListener?) {
        eventListener = listener
    }
    
    // MARK: - Event Handling
    
    func handleSocketEvent(engine: RTEngine, socket: RTSOCKET, event: Int32) {
        switch event {
        case RTTP_EVENT_CONNECT:
            isConnected = true
            eventListener?.onConnected()
        case RTTP_EVENT_READ:
            processReceiveQueue()
        case RTTP_EVENT_WRITE:
            processSendQueue()
        case RTTP_EVENT_ERROR:
            isConnected = false
            let error = RTSocketAPI.rt_get_error(engine, socket)
            eventListener?.onError(error: error, message: "RTTP_EVENT_ERROR")
        default:
            print("Unknown RTTP event: \(event)")
        }
    }


    // MARK: - Connection Management
    
    func connect(to serverIp: String, serverPort: UInt16) {
        let task = {
            if self.isInitialized && !self.isConnected {
                self.serverIp = serverIp
                self.serverPort = serverPort
                
                // Create socket using RTSocketAPI wrapper
                guard let engine = self.engine else {
                    self.eventListener?.onError(error: -1, message: "Engine not initialized")
                    return
                }
                
                self.socket = RTSocketAPI.rt_socket(engine, 0)
                
                guard let socket = self.socket else {
                    self.eventListener?.onError(error: -1, message: "Failed to create socket")
                    return
                }
                
                // Create socket address using RTSocketAPI helper
                guard var addr = RTSocketAPI.createSocketAddress(ip: serverIp, port: serverPort) else {
                    self.eventListener?.onError(error: -1, message: "Failed to create socket address")
                    return
                }
                
                // Connect using RTSocketAPI wrapper
                let result = withUnsafePointer(to: &addr) { addrPtr in
                    addrPtr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sockaddrPtr in
                        RTSocketAPI.rt_connect(engine, socket, sockaddrPtr, Int32(MemoryLayout<sockaddr_in>.size))
                    }
                }
                
                if result == 0 {
                    self.isRunning = true
                } else {
                    self.eventListener?.onError(error: result, message: "Failed to connect to server")
                }
            }
        }
        
        addApiTask(task)
    }

    func disconnect() {
        let task = {
            if self.isConnected {
                if let engine = self.engine, let socket = self.socket {
                    // Use RTSocketAPI wrapper for closing socket
                    RTSocketAPI.rt_close(engine, socket)
                    self.socket = nil
                }
                self.isConnected = false
                self.isRunning = false

                self.clearQueues()
            }
        }
        
        addApiTask(task)
    }

    
    func send(data: Data) {
        guard isInitialized && isConnected else {
            eventListener?.onSend(data: data, length: Int32(data.count), result: -1)
            return
        }
        
        let sendTask = SendTask(data: data, length: data.count)
        
        // 加锁保护sendQueue的访问
        sendQueueLock.lock()
        sendQueue.append(sendTask)
        sendQueueLock.unlock()
        
        // 主动触发一次写事件处理
        dispatchQueue.async { [weak self] in
            self?.processSendQueue()
        }
    }
    
    func receive(maxLength: Int) {
        guard isInitialized && isConnected else {
            return
        }
        
        let receiveTask = ReceiveTask(maxLength: maxLength)
        
        // 加锁保护receiveQueue的访问
        receiveQueueLock.lock()
        receiveQueue.append(receiveTask)
        receiveQueueLock.unlock()
        
        // 主动触发一次读事件处理
        dispatchQueue.async { [weak self] in
            self?.processReceiveQueue()
        }
    }


    // MARK: - API Task Queue Management (高效版本)
    
    private func addApiTask(_ task: @escaping () -> Void) {
        apiTaskLock.lock()
        apiTasks.append(task)
        apiTaskLock.unlock()
        
        // 通知有新的API任务需要处理
        signalApiTaskAvailable()
    }
    
    
    private func processApiTasks() {
        // 处理所有待处理的API任务
        while true {
            apiTaskLock.lock()
            guard !apiTasks.isEmpty else {
                apiTaskLock.unlock()
                break
            }
            let task = apiTasks.removeFirst()
            apiTaskLock.unlock()
            
            // Execute the task
            task()
        }
    }


    // MARK: - Data Transfer (异步队列实现)


    // MARK: - DispatchSource Integration (高效事件驱动)
    
    private var udpDispatchSource: DispatchSourceRead?
    private var apiTaskDispatchSource: DispatchSourceUserDataAdd?
    private var tickTimer: DispatchSourceTimer?
    private let dispatchQueue = DispatchQueue(label: "com.rtttech.rttp.tickloop", qos: .userInitiated)
    
    private func startDispatchSources() {
        // 创建UDP socket的DispatchSource
        if udpSocket != -1 {
            udpDispatchSource = DispatchSource.makeReadSource(fileDescriptor: udpSocket, queue: dispatchQueue)
            udpDispatchSource?.setEventHandler { [weak self] in
                self?.handleUdpDataAvailable()
            }
            udpDispatchSource?.setCancelHandler { [weak self] in
                self?.closeUdpSocket()
            }
            udpDispatchSource?.resume()
        }
        
        // 创建API任务队列的DispatchSource
        apiTaskDispatchSource = DispatchSource.makeUserDataAddSource(queue: dispatchQueue)
        apiTaskDispatchSource?.setEventHandler { [weak self] in
            self?.processApiTasks()
        }
        apiTaskDispatchSource?.resume()
        
        // 创建定时器用于定期调用tick函数
        tickTimer = DispatchSource.makeTimerSource(queue: dispatchQueue)
        tickTimer?.schedule(deadline: .now(), repeating: .milliseconds(5), leeway: .microseconds(1000))
        tickTimer?.setEventHandler { [weak self] in
            self?.handleTick()
        }
        tickTimer?.resume()
    }
    
    private func stopDispatchSources() {
        tickTimer?.cancel()
        tickTimer = nil
        
        apiTaskDispatchSource?.cancel()
        apiTaskDispatchSource = nil
        
        udpDispatchSource?.cancel()
        udpDispatchSource = nil
    }
    
    private func handleUdpDataAvailable() {
        //print("thread \(Thread.current) enter handleUdpDataAvailable")

        var buffer = [UInt8](repeating: 0, count: 65536)
        var addr = sockaddr_in()
        var addrLen = socklen_t(MemoryLayout<sockaddr_in>.size)
        
        // 读取所有可用的UDP数据包
        while true {
            addrLen = socklen_t(MemoryLayout<sockaddr_in>.size)
            let bytesRead = recvfrom(udpSocket, &buffer, buffer.count, 0,
                                     withUnsafeMutablePointer(to: &addr) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { $0 } },
                                     &addrLen)
            
            if bytesRead > 0 {
                let data = Data(bytes: buffer, count: bytesRead)
                processUdpPacket(data: data, addr: addr, addrLen: addrLen)
            } else if bytesRead == -1 {
                // 检查是否还有更多数据
                if errno == EAGAIN || errno == EWOULDBLOCK {
                    break
                }
            } else {
                break
            }
        }
        //print("thread \(Thread.current) exit handleUdpDataAvailable")
    }
    
    private func handleTick() {
        //print("thread \(Thread.current) enter handleTick")
        // 调用tick函数处理超时和其他定时任务   
        if let engine = engine {
            _ = rt_tick_c(engine)
        }
        //print("thread \(Thread.current) exit handleTick")
    }
    
    // 实际处理发送队列（只在RTTP_EVENT_WRITE或主动触发时调用）
    private func processSendQueue() {
        /*
        print("thread \(Thread.current) enter processSendQueue")
        defer {
            print("thread \(Thread.current) exit processSendQueue")
        }
        */
        guard isConnected, let socket = socket, let engine = engine, eventListener != nil else {
            return
        }
        
        // 循环处理发送队列
        while true {
            // 1. 获取队列头部任务（加锁）
            var task: SendTask?
            var willSend: Int = 0
            sendQueueLock.lock()
            if let currentTask = sendQueue.first {
                task = currentTask
                willSend = currentTask.length - currentTask.sentBytes
            }
            sendQueueLock.unlock()
            
            // 如果队列为空或任务已完成，退出循环
            guard let currentTask = task, willSend > 0 else {
                break
            }
            
            // 2. 检查socket是否可写（在锁外执行）
            if RTSocketAPI.rt_writable(engine, socket) <= 0 {
                break
            }
            
            // 3. 准备发送数据（在锁外执行）
            let sendBuffer = currentTask.data.subdata(in: currentTask.sentBytes..<currentTask.sentBytes + willSend)
            
            // 4. 执行发送操作（在锁外执行）
            let ret = sendBuffer.withUnsafeBytes { bufferPtr -> Int32 in
                guard let buffer = bufferPtr.baseAddress?.assumingMemoryBound(to: Int8.self) else {
                    return -1
                }
                return RTSocketAPI.rt_send(engine, socket, buffer, Int32(willSend), 0)
            }
            
            // 5. 处理发送结果（加锁）
        
        
            if ret > 0 {
                // 更新已发送字节数
                currentTask.sentBytes += Int(ret)
                
                // 检查是否发送完成
                if currentTask.sentBytes == currentTask.length {
                    // 发送完成，移除任务
                    sendQueueLock.lock()
                    sendQueue.removeFirst()
                    let completedTask = currentTask
                    // 释放锁后再调用回调
                    sendQueueLock.unlock()
                    
                    eventListener?.onSend(data: completedTask.data, length: Int32(completedTask.length), result: Int32(completedTask.length))
                } else {
                    
                }
            } else if ret == -1 {
                // 不可写，等待下次WRITE事件
                break
            } else {
                // 发送错误，移除任务
                sendQueueLock.lock()
                sendQueue.removeFirst()
                let failedTask = currentTask
                // 释放锁后再调用回调
                sendQueueLock.unlock()
                
                eventListener?.onSend(data: failedTask.data, length: Int32(failedTask.length), result: ret)
            }
        }
    }
    
    // 实际处理接收队列（只在RTTP_EVENT_READ或主动触发时调用）
    private func processReceiveQueue() {
        /*
        print("thread \(Thread.current) enter processReceiveQueue")
        defer {
            print("thread \(Thread.current) exit processReceiveQueue")
        }
        */

        guard isConnected, let socket = socket, let engine = engine, eventListener != nil else {
            return
        }
        
        // 循环处理接收队列
        while true {
            // 1. 获取队列头部任务（加锁）
            var task: ReceiveTask?
            receiveQueueLock.lock()
            if !receiveQueue.isEmpty {
                task = receiveQueue.first
            }
            receiveQueueLock.unlock()
            
            // 如果队列为空，退出循环
            guard let currentTask = task else {
                break
            }
            
            // 2. 检查socket是否可读（在锁外执行）
            if RTSocketAPI.rt_readable(engine, socket) <= 0 {
                break
            }
            
            // 3. 执行接收操作（在锁外执行）
            var buffer = [UInt8](repeating: 0, count: currentTask.maxLength)
            let ret = buffer.withUnsafeMutableBytes { bufferPtr -> Int32 in
                guard let cbuffer = bufferPtr.baseAddress?.assumingMemoryBound(to: Int8.self) else {
                    return -1
                }
                return RTSocketAPI.rt_recv(engine, socket, cbuffer, Int32(currentTask.maxLength), 0)
            }
            
            //print("rt_recv return \(ret)")
        
            if ret >= 0 {
                // 接收成功，移除任务
                receiveQueueLock.lock()
                receiveQueue.removeFirst()
                // 释放锁后再调用回调
                receiveQueueLock.unlock()
                
                let receivedData = Data(bytes: buffer, count: Int(ret))
                eventListener?.onReceived(data: receivedData, length: ret)
            } else if ret == -1 {
                // 没有数据可读，退出循环
                
                break
            } else {
                // 接收错误，移除任务
                receiveQueueLock.lock()
                receiveQueue.removeFirst()
                receiveQueueLock.unlock()

                eventListener?.onReceived(data: Data(), length: ret)
            }
        }
    }
    
    private func signalApiTaskAvailable() {
        // 通知有新的API任务需要处理
        apiTaskDispatchSource?.add(data: 1)
    }


    // MARK: - UDP Packet Handling
    
    private func initializeUdpSocket() -> Bool {
        // Create UDP socket
        udpSocket = Darwin.socket(Darwin.AF_INET, Darwin.SOCK_DGRAM, Darwin.IPPROTO_UDP)
        if udpSocket == -1 {
            print("Failed to create UDP socket: \(errno)")
            return false
        }

        // Bind to INADDR_ANY to ensure the socket is associated with network interfaces
        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(Darwin.AF_INET)
        addr.sin_port = 0 // Let OS pick a port
        addr.sin_addr.s_addr = in_addr_t(0) // INADDR_ANY

        let bindRet = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.bind(udpSocket, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }

        if bindRet == -1 {
            print("Failed to bind UDP socket: \(errno)")
            closeUdpSocket()
            return false
        }
        
        var flags = Darwin.fcntl(udpSocket, Darwin.F_GETFL, 0)
        if flags != -1 {
            if Darwin.fcntl(udpSocket, Darwin.F_SETFL, flags | Darwin.O_NONBLOCK) == -1 {
                print("Failed to set non-blocking mode on UDP socket: \(errno)")
                closeUdpSocket()
                return false
            }
        } else {
            print("Failed to get flags for UDP socket: \(errno)")
            closeUdpSocket()
            return false
        }
            
        return true
    }
    
    private func closeUdpSocket() {
        if udpSocket != -1 {
            close(udpSocket)
            udpSocket = -1
        }
    }
    
    func sendUdpPacket(engine: RTEngine, socket: RTSOCKET, buffer: UnsafePointer<Int8>?, len: Int32, addr: UnsafePointer<sockaddr>?, addr_len: Int32) {
        /*
        print("thread \(Thread.current) enter sendUdpPacket")
        defer {
            print("thread \(Thread.current) exit sendUdpPacket")
        }
        */
        guard let buffer = buffer, let addr = addr else {
            return
        }
        
        // Print target IP and port
        var ipAddress = ""
        var port: UInt16 = 0
        
        // Check if it's an IPv4 address
        if addr.pointee.sa_family == UInt8(Darwin.AF_INET) {
            let addrIn = UnsafeRawPointer(addr).assumingMemoryBound(to: sockaddr_in.self).pointee
            let ip = Darwin.inet_ntoa(addrIn.sin_addr)
            if let ip = ip {
                ipAddress = String(cString: ip)
            }
            port = UInt16(bigEndian: addrIn.sin_port)
        }
        
        print("Sending UDP packet to: \(ipAddress):\(port)")
        
        // Send UDP packet using the socket
        let data = Data(bytes: buffer, count: Int(len))
        data.withUnsafeBytes { dataPtr in
            if let dataBuffer = dataPtr.baseAddress?.assumingMemoryBound(to: UInt8.self) {
                //var mutableAddr = addr.pointee
                let ret = Darwin.sendto(udpSocket, dataBuffer, data.count, 0, addr, socklen_t(addr_len))
                if ret != len {
                    print("send to return :\(ret)")
                }
            }
        }
    }
    
    private func processUdpPacket(data: Data, addr: sockaddr_in, addrLen: socklen_t) {
        /*
        print("thread \(Thread.current) enter processUdpPacket")
        defer {
            print("thread \(Thread.current) exit processUdpPacket")
        }
        */
        // Process received UDP packet using RTTP engine
        if let engine = engine {
            // Create a mutable copy of the address since we need to pass it as inout
            var mutableAddr = addr
            data.withUnsafeBytes { bufferPtr in
                if let buffer = bufferPtr.baseAddress?.assumingMemoryBound(to: Int8.self) {
                    let incomingSocket = withUnsafePointer(to: &mutableAddr) { addrPtr in
                        addrPtr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sockaddrPtr in
                            RTSocketAPI.rt_incoming_packet(engine, buffer, Int32(data.count), sockaddrPtr, Int32(addrLen))
                        }
                    }
                    
                    //should not happen
                    if incomingSocket != nil {
                        // Handle incoming connection
                        RTSocketAPI.rt_close(engine, incomingSocket)
                    } 
                }
            }
        }
    }


    // MARK: - Status Checks
    
    private func isReadable() -> Bool {
        guard let engine = engine, let socket = socket else {
            return false
        }
        return RTSocketAPI.rt_readable(engine, socket) == 1
    }
    
    private func isWritable() -> Bool {
        guard let engine = engine, let socket = socket else {
            return false
        }
        return RTSocketAPI.rt_writable(engine, socket) == 1
    }


    // MARK: - Getters

    func getUdpPort() -> UInt16 {
        return udpPort
    }

    func getServerIp() -> String? {
        return serverIp
    }

    func getIsInitialized() -> Bool {
        return isInitialized
    }

    func getIsConnected() -> Bool {
        return isConnected
    }
}
