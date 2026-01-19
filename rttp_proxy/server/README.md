# RTTP Proxy Server Operations Manual

## Introduction

The **RTTP Proxy Server** is a high-performance proxy application designed to bridge connections between an RTTP (Real-Time Transport Protocol) client and a TCP upstream server. It accepts reliable UDP connections from RTTP clients and forwards the traffic to a specified TCP destination, effectively acting as a gateway for RTTP traffic.

## Features

- **Protocol Bridging**: Converts reliable UDP (RTTP) to TCP.
- **High Concurrency**: Built on `libuv` for asynchronous I/O handling.
- **Monitoring**: Built-in HTTP monitor for real-time statistics and health checks.
- **Prometheus Support**: Exports metrics in Prometheus format.

## Build

The project is built using CMake. Ensure you have CMake and a C++ compiler installed.

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Usage

The server is started via the command line with the following arguments:

```bash
RttpProxyServer <local_host_ip> <local_port> <forward_host_ip> <forward_port> <max_connections> [http_monitor_port]
```

### Arguments

| Argument | Description | Example |
|----------|-------------|---------|
| `local_host_ip` | The IP address to bind the RTTP server to. | `0.0.0.0` |
| `local_port` | The UDP port to listen on for RTTP connections. | `9000` |
| `forward_host_ip` | The IP address of the upstream TCP server. | `192.168.1.100` |
| `forward_port` | The TCP port of the upstream server. | `8080` |
| `max_connections` | The maximum number of concurrent connections allowed. | `1000` |
| `http_monitor_port` | (Optional) Port for the HTTP monitor. Defaults to `local_port + 1000`. | `10000` |

### Example

Start the server listening on all interfaces at port 9000, forwarding to a local web server on port 80, with a limit of 500 connections and monitoring on port 9001:

```bash
./RttpProxyServer 0.0.0.0 9000 127.0.0.1 80 500 9001
```

## Monitoring

The server includes an embedded HTTP server for monitoring and health checks.

### Endpoints

| Path | Description | Content-Type |
|------|-------------|--------------|
| `/` | Homepage with links to other pages. | `text/html` |
| `/health` | Health check. Returns 200 OK and uptime if healthy. | `application/json` |
| `/ready` | Readiness check. Returns status "ready" if accepting connections. | `application/json` |
| `/metrics` | Prometheus-compatible metrics. | `text/plain` |
| `/stats` | Active connection statistics (alias for `/stats-active`). | `text/html` |
| `/stats-active` | Detailed statistics for currently active connections. | `text/html` |
| `/stats-history` | Statistics for closed connections (last 100). | `text/html` |
| `/connections` | List of current connections with RTT and loss rate. | `text/html` |

### Prometheus Metrics

The `/metrics` endpoint exports the following gauges and counters:

- `rttp_active_connections`: Current number of active connections.
- `rttp_total_connections`: Total number of connections handled since startup.
- `rttp_rtt_distribution_active`: RTT distribution buckets for active connections.
- `rttp_loss_rate_distribution_active`: Loss rate distribution buckets for active connections.
