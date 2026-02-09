#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <windows.h>
	#pragma comment(lib, "ws2_32.lib") // 链接Winsock库
	typedef int socklen_t;
	#define close closesocket
	#define usleep(x) Sleep((x)/1000)  // Windows没有usleep，用Sleep代替（单位ms）
	#define MSG_NOSIGNAL 0
#else
	#include <unistd.h>
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <sys/time.h>
	#include <netdb.h>
	#define MSG_NOSIGNAL 0
#endif

#define DEFAULT_PORT "9999"         // 默认连接的端口
#define BUF_SIZE 256                // 收发缓冲区大小
#define DEFAULT_INTERVAL 1000       // 默认ping间隔（毫秒）

// 全局变量：控制程序是否继续运行（Ctrl+C会改成0）
volatile int running = 1;

// 捕获Ctrl+C信号的处理函数（把running设为0，while循环就会退出）
void signal_handler(int sig) { running = 0; }

// 获取当前时间的微秒级时间戳（跨平台实现）
long long get_usec_timestamp() {
#ifdef _WIN32
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	long long t = ((long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	// 转换为Unix时间戳（微秒），Windows从1601年开始计数
	return (t - 116444736000000000LL) / 10;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000LL + tv.tv_usec;
#endif
}

// 把socket地址（ipv4或ipv6）转成可读的字符串
void addr_to_str(struct sockaddr *addr, char *str, size_t str_len) {
	void *addr_ptr = NULL;
	if (addr->sa_family == AF_INET) {
		addr_ptr = &((struct sockaddr_in*)addr)->sin_addr;
	} else if (addr->sa_family == AF_INET6) {
		addr_ptr = &((struct sockaddr_in6*)addr)->sin6_addr;
	}
	
	if (addr_ptr) {
		// 把二进制IP转成字符串（如 "192.168.1.1" 或 "2001:db8::1"）
		inet_ntop(addr->sa_family, addr_ptr, str, str_len);
	} else {
		strncpy(str, "unknown", str_len);
	}
}

// 从服务器返回的字符串中解析出时间戳
long long parse_pong_timestamp(const char *response) {
	long long ts;
	// 期望格式是：PONG 1234567890123
	if (sscanf(response, "PONG %lld", &ts) == 1) {
		return ts;
	}
	return -1;  // 解析失败
}

int main(int argc, char *argv[]) {
	// 检查参数，至少要给一个主机名/IP
	if (argc < 2) {
		printf("TCP Ping Client (IPv6/IPv4 Long Connection)\n");
		printf("Usage: %s <host> [port] [interval_ms] [count] [4|6]\n", argv[0]);
		printf("  host        : Target hostname or IP (v4/v6)\n");
		printf("  port        : Target port (default: %s)\n", DEFAULT_PORT);
		printf("  interval_ms : Ping interval in ms (default: %d)\n", DEFAULT_INTERVAL);
		printf("  count       : Number of pings, -1 for infinite (default: -1)\n");
		printf("  4|6         : Force IPv4 or IPv6 (default: auto)\n");
		printf("\nExamples:\n");
		printf("  %s 192.168.1.1\n", argv[0]);
		printf("  %s example.com 9999 1000 -1 4    (force IPv4)\n", argv[0]);
		printf("  %s ::1 9999 500                   (IPv6 localhost)\n", argv[0]);
		printf("  %s 2001:db8::1 80 1000 10        (IPv6 address)\n", argv[0]);
		return 1;
	}

	// Windows下需要先初始化Winsock
#ifdef _WIN32
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
		fprintf(stderr, "WSAStartup failed\n");
		return 1;
	}
#endif

	const char *host = argv[1];
	const char *port_str = (argc > 2) ? argv[2] : DEFAULT_PORT;
	int interval_ms = (argc > 3) ? atoi(argv[3]) : DEFAULT_INTERVAL;
	int max_count = (argc > 4) ? atoi(argv[4]) : -1;
	const char *force_family = (argc > 5) ? argv[5] : NULL;

	// 注册Ctrl+C信号处理
	signal(SIGINT, signal_handler);

	// 准备 getaddrinfo 的提示参数
	struct addrinfo hints, *res, *rp;
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;    // 使用TCP

	// 是否强制IPv4或IPv6
	if (force_family) {
		if (strcmp(force_family, "4") == 0) {
			hints.ai_family = AF_INET;  // Force IPv4
		} else if (strcmp(force_family, "6") == 0) {
			hints.ai_family = AF_INET6; // Force IPv6
		} else {
			hints.ai_family = AF_UNSPEC; // Auto
		}
	} else {
		hints.ai_family = AF_UNSPEC; // 自动选择 IPv4/IPv6
	}

	printf("Resolving %s:%s", host, port_str);
	if (force_family) {
		printf(" (IPv%s only)", force_family);
	}
	printf("...\n");

	int err = getaddrinfo(host, port_str, &hints, &res);
	if (err != 0) {
#ifdef _WIN32
		fprintf(stderr, "getaddrinfo failed: %d\n", err);
#else
		fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(err));
#endif
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	// 尝试连接所有解析到的地址（Happy Eyeballs 简化版）
	int sock = -1;
	char addr_str[INET6_ADDRSTRLEN];
	
	for (rp = res; rp != NULL; rp = rp->ai_next) {
		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock < 0) continue;

		// 关闭Nagle算法，减少小包延迟（对ping类应用很重要）
		int nodelay = 1;
		setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

		addr_to_str(rp->ai_addr, addr_str, sizeof(addr_str));
		printf("Trying %s...\n", addr_str);

		long long conn_start = get_usec_timestamp();
		
		if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
			long long conn_end = get_usec_timestamp();
			double conn_time = (conn_end - conn_start) / 1000.0;
			
			printf("Connected to [%s]:%s (TCP handshake: %.3f ms)\n", 
				   addr_str, port_str, conn_time);
			break; // 连接成功
		}

		close(sock);
		sock = -1;
	}

	freeaddrinfo(res);

	if (sock < 0) {
		fprintf(stderr, "Could not connect to any address\n");
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	printf("Starting long-connection ping (interval: %d ms, press Ctrl+C to stop)...\n\n", interval_ms);

	char buf[BUF_SIZE];
	int count = 0;
	long long total_rtt = 0;
	long long min_rtt = 999999999, max_rtt = 0;
	int lost = 0;

	// 设置接收超时（防止卡死）
#ifdef _WIN32
	DWORD timeout_ms = 5000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
#else
	struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
#endif

	while (running && (max_count == -1 || count < max_count)) {
		long long send_time = get_usec_timestamp();

		const char *ping_msg = "PING\n";
		if (send(sock, ping_msg, strlen(ping_msg), MSG_NOSIGNAL) < 0) {
			if (running) {
				fprintf(stderr, "\n[!] Send failed\n");
			}
			break;
		}

		memset(buf, 0, BUF_SIZE);
		int n = recv(sock, buf, BUF_SIZE - 1, 0);

		if (n <= 0) {
			if (running) {
				printf("\n[!] Timeout or connection closed\n");
				lost++;
			}
			break;
		}

		long long recv_time = get_usec_timestamp();
		long long rtt = recv_time - send_time;

		total_rtt += rtt;
		if (rtt < min_rtt) min_rtt = rtt;
		if (rtt > max_rtt) max_rtt = rtt;
		count++;

		printf("Reply from %s: seq=%d time=%.3f ms\n", 
			   addr_str, count, rtt / 1000.0);

		// 尽量精确地控制发送间隔
		if (interval_ms > 0 && (max_count == -1 || count < max_count)) {
			long long elapsed = get_usec_timestamp() - send_time;
			int sleep_us = interval_ms * 1000 - (int)elapsed;
			if (sleep_us > 0) {
				usleep(sleep_us);
			}
		}
	}

	// 打印统计信息
	if (count > 0) {
		printf("\n--- %s tcpping statistics ---\n", host);
		int total = count + lost;
		double loss_rate = total > 0 ? (100.0 * lost / total) : 0;
		printf("%d packets transmitted, %d received, %d lost, %.1f%% packet loss\n",
			   total, count, lost, loss_rate);
		printf("rtt min/avg/max = %.3f/%.3f/%.3f ms\n",
			   min_rtt / 1000.0,
			   (total_rtt / count) / 1000.0,
			   max_rtt / 1000.0);
	} else {
		printf("\nNo successful pings.\n");
	}

	close(sock);

#ifdef _WIN32
	WSACleanup();
#endif

	return 0;
}
