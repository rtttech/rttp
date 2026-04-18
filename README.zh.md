# RTTP: 实时传输协议

[English](README.md) | [简体中文](README.zh.md)

[![Platform](https://img.shields.io/badge/platform-Windows%20|%20Linux%20|%20macOS%20|%20iOS%20|%20Android-blue.svg)](#)
[![Unity](https://img.shields.io/badge/Unity-Supported-green.svg)](#)
[![Language](https://img.shields.io/badge/language-C%2B%2B%20|%20C%23%20|%20Java-orange.svg)](#)

**RTTP** (Real-Time Transmission Protocol) 是一款构建在 UDP 之上的高性能、可靠传输协议。它专为**超低延迟**和**高可靠性**而设计，在极端的网络条件（高丢包、高抖动）下，其实时性表现大幅优于 TCP 及其他协议。

---

## ✨ 核心特性

-   **⚡ 超低延迟**：优化的重传机制和拥塞控制算法，最大限度地减少尾部延迟和抖动。
-   **🛡️ 高级丢包恢复**：
    -   **自适应 ARQ**：基于实时 RTT 估算的智能重传策略。
    -   **FEC (前向纠错)**：主动恢复丢失的数据包，避免丢包环境下的重传等待时间。
-   **🔌 不受 IO 框架限制的设计**：RTTP 仅负责协议逻辑，不与任何特定的 IO 框架耦合，可轻松集成到 `libuv`、`Asio`、`epoll` 或自定义游戏引擎中。
-   **🌍 多平台与多语言支持**：
    -   原生 **C/C++** 核心，追求极致性能。
    -   为 **Unity (C#)**、**Android (Java)** 和 **iOS** 提供原生支持。
-   **📦 零代码迁移**：使用 **RTTP Proxy**，无需修改服务端代码，即可将现有的 TCP 应用服务器桥接到 RTTP。

---

## 📊 对比测试

![Stable](https://www.rtttech.com/assets/images/0lost-5c97e21028a5d5c33db85a74688ecd04.png)
![10%](https://www.rtttech.com/assets/images/10lost-8d011a906720e384c5b31e724039a3e3.png)
![20%](https://www.rtttech.com/assets/images/20lost-4d0f2aab072fb05671847802306398f2.png)

---

## 📂 目录结构

-   **`/api`**: RTTP SDK。
-   **`/rttp_proxy`**: 独立代理服务器，实现 TCP 到 RTTP 的无缝迁移。
-   **`/unity3d_demo`**: 开箱即用的 Unity 集成模式示例。
-   **`/cpp_demo`, `/csharp_demo`,  `/java_demo`,  `/android_demo`, `/ios_demo`**: 丰富的平台示例。（注：所有客户端示例均可使用 `cpp_demo/rttp_async_echo_server` 作为服务器端）

---

## 🏗️ 编译 C++ 示例与 RTTP Proxy 服务器


```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## 📖 文档

深入了解我们的文档，获取完整的 API 参考和集成指南：
👉 [RTTP 文档中心](https://www.rtttech.com/docs/intro)

---

## 📧 联系与支持

-   **官方网站**: [rtttech.com](https://www.rtttech.com)
-   **电子邮箱**: [support@rtttech.com](mailto:support@rtttech.com)

---

© 2026 RttTech. 为下一代实时连接赋能。
