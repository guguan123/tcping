#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <getopt.h>  // 用于 getopt_long 支持长选项

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <windows.h>
	#pragma comment(lib, "ws2_32.lib") // 链接Winsock库
	typedef int socklen_t;
	#ifndef MSG_NOSIGNAL
		#define MSG_NOSIGNAL 0
	#endif
#else
	#include <unistd.h>
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <netdb.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <sys/time.h>
#endif

#define DEFAULT_PORT "50414"         // 默认连接的端口
#define BUF_SIZE 256                // 收发缓冲区大小
#define DEFAULT_INTERVAL 1       // 默认ping间隔（秒）
#define DEFAULT_TIMEOUT 5

// 全局变量：控制程序是否继续运行（Ctrl+C会改成0）
volatile int running = 1;

// 捕获Ctrl+C信号的处理函数（把running设为0，while循环就会退出）
void signal_handler(int sig) { running = 0; (void)sig; }

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

/**
返回值：
 >0 : 超时
  0 : 有数据可读
 <0 : 错误或连接断开
*/
int wait_for_readable(int sock, int timeout_sec) {
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int ret = select(sock + 1, &readfds, NULL, NULL, &tv);

    if (ret < 0) {
        // select 出错（可能是 socket 已关闭等）
        return -1;
    }
    if (ret == 0) {
        // 超时
        return 1;
    }
    // 可读
    return 0;
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
	const char *host = NULL;
	const char *port_str = DEFAULT_PORT;
	int max_count = -1;               // -1 = infinite
	int interval_sec = DEFAULT_INTERVAL;
	int timeout_sec = DEFAULT_TIMEOUT;
	int force_family = 0; // 0=auto, AF_INET=4, AF_INET6=6

	int opt;
	while ((opt = getopt(argc, argv, "c:i:w:46")) != -1) {
		switch (opt) {
			case 'c':
				max_count = atoi(optarg);
				if (max_count < 1) {
					printf("%s: invalid argument: -c '%d': out of range: 1 <= value <= 2147483647\n", argv[0], max_count);
					return 1;
				}
				break;
			case 'i':
				interval_sec = atoi(optarg);
				if (interval_sec < 1) {
					printf("%s: cannot flood, minimal interval for user must be >= 1 s, use -i 1 (or higher)\n", argv[0]);
					return 1;
				}
				break;
			case 'w':
				timeout_sec = atoi(optarg);
				if (timeout_sec < 0) {
					printf("%s: invalid argument: -w '%d': out of range: 0 <= value <= 2147483647\n", argv[0], timeout_sec);
					return 1;
				}
				break;
			case '4':
				force_family = 4;
				break;
			case '6':
				force_family = 6;
				break;
			default:
				// 打印 usage
				printf("Usage: %s [-c count] [-i interval_sec] [-4|-6] host [port]\n", argv[0]);
				printf("  -c <count>         stop after count pings (default: infinite)\n");
				printf("  -i <interval>      seconds to wait between pings (default: %d)\n", DEFAULT_INTERVAL);
				printf("  -w <timeout>       time to wait for response (default: %d)\n", DEFAULT_TIMEOUT);
				printf("  -4                 force IPv4\n");
				printf("  -6                 force IPv6\n");
				return 0;
		}
	}

	if (optind >= argc) {
		printf("Missing host\n");
		return 1;
	}

	host = argv[optind++];
	if (optind < argc) {
		port_str = argv[optind];
	}

	// Windows下需要先初始化Winsock
#ifdef _WIN32
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
		fprintf(stderr, "WSAStartup failed\n");
		return 1;
	}
#endif

	// 注册Ctrl+C信号处理
	signal(SIGINT, signal_handler);

	// 准备 getaddrinfo 的提示参数
	struct addrinfo hints, *res, *rp;
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;    // 使用TCP

	// 是否强制IPv4或IPv6
	if (force_family) {
		if (force_family == 4) {
			hints.ai_family = AF_INET;  // Force IPv4
		} else if (force_family == 6) {
			hints.ai_family = AF_INET6; // Force IPv6
		} else {
			hints.ai_family = AF_UNSPEC; // Auto
		}
	} else {
		hints.ai_family = AF_UNSPEC; // 自动选择 IPv4/IPv6
	}

	printf("Resolving %s:%s", host, port_str);
	if (force_family) {
		printf(" (IPv%d only)", force_family);
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

		// 关闭Nagle算法，减少小包延迟
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

#ifdef _WIN32
		closesocket(sock);
#else
		close(sock);
#endif
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

    printf("Starting long-connection ping to %s:%s (interval: %d sec, press Ctrl+C to stop)...\n\n",
           host, port_str, interval_sec);

	char buf[BUF_SIZE];
	int count = 0;
	long long total_rtt = 0;
	long long min_rtt = 999999999, max_rtt = 0;

	while (running && (max_count == -1 || count < max_count)) {
		long long send_time = get_usec_timestamp();

		const char *ping_msg = "PING\n";
		if (send(sock, ping_msg, strlen(ping_msg), MSG_NOSIGNAL) < 0) {
			if (running) {
				fprintf(stderr, "\n[!] Send failed\n");
			}
			break;
		}

		// 清空缓冲
		memset(buf, 0, BUF_SIZE);
		char *ptr = buf;
		int total_received = 0;
		bool got_line = false;

		while (running && total_received < BUF_SIZE - 1) {
			int can_read = wait_for_readable(sock, timeout_sec);
			if (can_read < 0) {
				// select 出错或连接已断
				if (running) {
					printf("\n[!] Connection error or closed\n");
				}
				break;
			}
			if (can_read > 0) {
				// 超时
				if (running) {
					printf("\n[!] Timeout waiting for PONG\n");
				}
				break;
			}

			// 有数据，读
			int n = recv(sock, ptr, BUF_SIZE - 1 - total_received, 0);
			if (n <= 0) {
				if (running && n < 0) {
					printf("\n[!] Recv error or connection closed\n");
				}
				break;
			}

			total_received += n;
			ptr += n;

			// 检查是否收到完整一行（以 \n 结尾）
			if (memchr(buf, '\n', total_received) != NULL) {
				got_line = true;
				break;
			}
		}

		if (!got_line || total_received == 0) {
			if (running) {
				printf("\n[!] Incomplete or empty response\n");
			}
			break;
		}

		// 确保字符串以 \0 结尾
		buf[total_received] = '\0';

		// 去掉尾部可能的 \r\n
		char *end = buf + total_received - 1;
		while (end >= buf && (*end == '\n' || *end == '\r')) {
			*end-- = '\0';
		}

		// 这里 buf 就是完整的回复行了，比如 "PONG 123456789"
		long long recv_time = get_usec_timestamp();
		long long rtt = recv_time - send_time;

		total_rtt += rtt;
		if (rtt < min_rtt) min_rtt = rtt;
		if (rtt > max_rtt) max_rtt = rtt;
		count++;

		printf("Reply from %s: seq=%d time=%.3f ms\n", 
			   addr_str, count, rtt / 1000.0);

		// 控制发送间隔
		if (interval_sec > 0 && (max_count == -1 || count < max_count)) {
#ifdef _WIN32
			// Windows: 拆成小块睡，200ms 粒度
			int remain_ms = interval_sec * 1000;
			while (remain_ms > 0 && running) {
				int slice_ms = (remain_ms > 200) ? 200 : remain_ms;
				Sleep(slice_ms);
				remain_ms -= slice_ms;
			}
#else
			// Linux：直接 sleep
			if (running) sleep(interval_sec);
#endif
		}
	}

	// 打印统计信息
	if (count > 0) {
		printf("\n--- %s tcpping statistics ---\n", host);
		printf("%d packets transmitted, %d received\n", count, count);
		printf("rtt min/avg/max = %.3f/%.3f/%.3f ms\n",
			   min_rtt / 1000.0,
			   (total_rtt / count) / 1000.0,
			   max_rtt / 1000.0);
	} else {
		printf("\nNo successful pings.\n");
	}

#ifdef _WIN32
	closesocket(sock);
#else
	close(sock);
#endif

#ifdef _WIN32
	WSACleanup();
#endif

	return 0;
}
