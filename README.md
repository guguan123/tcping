# TCPing: TCP Connectivity and Latency Checker

`TCPing` is a utility that allows you to check the connectivity and measure the round-trip time (RTT) to a specific TCP port on a host. Unlike the traditional `ping` command which uses ICMP, `TCPing` establishes a TCP connection and sends application-level "PING" messages, making it ideal for verifying service availability on a given port.

It consists of two main components:
- `tcpping`: The client application that sends TCP PINGs.
- `tcppingd`: The daemon (server) application that responds to `tcpping` requests.

## Features

*   **TCP Connectivity Check**: Verifies if a TCP port is open and accepting connections.
*   **Latency Measurement**: Measures the RTT for TCP handshakes and subsequent application-level PING/PONG exchanges.
*   **Cross-platform**: Supports both Linux and Windows.
*   **IPv4/IPv6 Support**: Can force connection over IPv4 or IPv6.
*   **Customizable**: Adjust ping count, interval, and timeout.

## Building

To build `tcpping` and `tcppingd`, simply use `make`:

```bash
make
```

This will create two executables: `tcpping` (client) and `tcppingd` (server) in the current directory.

## Usage

### 1. Start the Server (`tcppingd`)

First, start the `tcppingd` daemon on the target host. By default, it listens on port `50414`. You can specify a different port as an argument.

```bash
./tcppingd        # Listens on default port 50414
./tcppingd 8080   # Listens on port 8080
```

You should see output similar to:
```
[+] TCP Ping Server listening on [::]:50414 (IPv4/IPv6 dual-stack)
```

### 2. Run the Client (`tcpping`)

Once the server is running, you can use the `tcpping` client to test connectivity and measure latency. Specify the target host and optionally the port.

```bash
./tcpping <host> [port]
```

**Options:**

*   `-c <count>`: Stop after `count` pings (default: infinite).
*   `-i <interval>`: Seconds to wait between pings (default: 1 second).
*   `-w <timeout>`: Time in seconds to wait for a response (default: 5 seconds).
*   `-4`: Force IPv4.
*   `-6`: Force IPv6.

**Example:**

Ping a server at `example.com` on port `80` five times with a 2-second interval:

```bash
./tcpping -c 5 neko.guguan.us.kg
```

Expected output (example):

```
Trying ...
Connected to [93.184.216.34]:50414 (TCP handshake: 16.161 ms)
Starting long-connection ping to neko.guguan.us.kg:50414 (interval: 1 sec, press Ctrl+C to stop)...

Reply from 93.184.216.34: seq=1 time=16.789 ms
Reply from 93.184.216.34: seq=2 time=15.123 ms
Reply from 93.184.216.34: seq=3 time=17.456 ms
Reply from 93.184.216.34: seq=4 time=14.999 ms
Reply from 93.184.216.34: seq=5 time=16.321 ms

--- example.com tcpping statistics ---
5 packets transmitted, 5 received
rtt min/avg/max = 14.999/16.138/17.456 ms
```
