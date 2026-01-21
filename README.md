# RTTP: Real-Time Transmission Protocol

[English](README.md) | [简体中文](README.zh.md)

[![Platform](https://img.shields.io/badge/platform-Windows%20|%20Linux%20|%20macOS%20|%20iOS%20|%20Android-blue.svg)](#)
[![Unity](https://img.shields.io/badge/Unity-Supported-green.svg)](#)
[![Language](https://img.shields.io/badge/language-C%2B%2B%20|%20C%23%20|%20Java-orange.svg)](#)

**RTTP** (Real-Time Transmission Protocol) is a high-performance, reliable transport protocol built on top of UDP. Specifically engineered for **ultra-low latency** and **high reliability**, RTTP outperforms TCP and other protocols in extreme network conditions with high packet loss and jitter.

Whether you're building a massive multiplayer game, a high-frequency trading platform, or a real-time streaming service, RTTP provides the rock-solid foundation your network layer needs.

---

## ✨ Key Features

-   **⚡ Ultra-Low Latency**: Optimized retransmission and congestion control algorithms to minimize tail latency and jitter.
-   **🛡️ Advanced Loss Recovery**: 
    -   **Adaptive ARQ**: Smart retransmission strategies based on real-time RTT estimation.
    -   **FEC (Forward Error Correction)**: Proactive packet recovery to avoid retransmission delays in lossy environments.
-   **🔌 Agnostic IO Design**: "Bring Your Own Socket" (BYOS). RTTP manages protocol logic without being coupled to any specific IO framework, making it easy to integrate into `libuv`, `Asio`, `epoll`, or custom game engines.
-   **🌍 Multi-Platform & Multi-Language**: 
    -   Native **C/C++** core for maximum performance.
    -   First-class support for **Unity (C#)**, **Android (Java)**, and **iOS**.
-   **📦 Zero-Code Migration**: Use the **RTTP Proxy** to bridge existing TCP application servers to RTTP without changing a single line of server-side code.

---

## 📊 Performance Benchmark

RTTP maintains high throughput and low latency even as network conditions degrade, whereas TCP performance often collapses under packet loss.

![Stable](http://www.rtttech.com/assets/images/0lost-5c97e21028a5d5c33db85a74688ecd04.png)
![10%](http://www.rtttech.com/assets/images/10lost-8d011a906720e384c5b31e724039a3e3.png)
![20%](http://www.rtttech.com/assets/images/20lost-4d0f2aab072fb05671847802306398f2.png)

---

## 📂 Project Structure

-   **`/api`**: RTTP SDK.
-   **`/rttp_proxy`**: A standalone proxy server for effortless TCP-to-RTTP migration.
-   **`/unity3d_demo`**: Ready-to-use Unity integration patterns.
-   **`/cpp_demo`, `/csharp_demo`,  `/java_demo`,  `/android_demo`, `/ios_demo`**: Comprehensive platform-specific examples. (Note: All client demos can use `cpp_demo/rttp_async_echo_server` as the backend server)

---

## 🏗️ Building C++ Demo and RTTP Proxy Server

RTTP utilizes CMake, ensuring a consistent build process across all major platforms.

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## 📖 Learn More

Dive deeper into our documentation for API references and integration guides:  
👉 [RTTP Documentation Portal](http://www.rtttech.com/docs/intro)

---

## 📧 Contact & Support

We are dedicated to helping you achieve the best real-time performance.

-   **Official Website**: [rtttech.com](http://www.rtttech.com)
-   **Email**: [support@rtttech.com](mailto:support@rtttech.com)

---

© 2026 RttTech. Empowering the next generation of real-time connectivity.
