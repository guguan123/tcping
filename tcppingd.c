#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <windows.h>
	#pragma comment(lib, "ws2_32.lib")
	typedef int socklen_t;
#else
	#include <unistd.h>
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <sys/wait.h>
	#include <sys/time.h>
	#include <netdb.h>
#endif

#define DEFAULT_PORT "50414"
#define BUF_SIZE 64

// 获取微秒时间戳
long long get_usec_timestamp() {
#ifdef _WIN32
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	long long t = ((long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	return (t - 116444736000000000LL) / 10;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000LL + tv.tv_usec;
#endif
}

// 处理单个客户端连接（收PING → 回PONG + 当前时间戳）
void handle_client(int client_fd, struct sockaddr *client_addr) {
	char buf[BUF_SIZE];
	char client_str[INET6_ADDRSTRLEN];
	int read_ptr = 0; // 记录缓冲区当前位置

	// 获取客户端IP和端口
	void *addr_ptr = NULL;
	int port = 0;
	if (client_addr->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in*)client_addr;
		addr_ptr = &sin->sin_addr;
		port = ntohs(sin->sin_port);
	} else if (client_addr->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)client_addr;
		addr_ptr = &sin6->sin6_addr;
		port = ntohs(sin6->sin6_port);
	}

	if (addr_ptr) {
		inet_ntop(client_addr->sa_family, addr_ptr, client_str, sizeof(client_str));
	} else {
		strcpy(client_str, "unknown");
	}

	printf("[+] Client connected: [%s]:%d\n", client_str, port);

	// 循环接收客户端消息
	while (1) {
		int n = recv(client_fd, buf + read_ptr, BUF_SIZE - 1 - read_ptr, 0);

		if (n <= 0) {   // 连接断开
			printf("[-] Client [%s]:%d disconnected\n", client_str, port);
			break;
		}

		read_ptr += n;
		buf[read_ptr] = '\0';

		char *line_end;
		while ((line_end = strchr(buf, '\n')) != NULL) { // 循环处理缓冲区里的所有换行
			*line_end = '\0';
			char *cmd = buf;
			// 处理 \r\n
			if (line_end > buf && *(line_end - 1) == '\r') *(line_end - 1) = '\0';

			if (strcmp(cmd, "PING") == 0) {
				long long timestamp = get_usec_timestamp();
				char resp[BUF_SIZE];
				int len = snprintf(resp, BUF_SIZE, "PONG %lld\n", timestamp);
				send(client_fd, resp, len, 0);
			}

			// 挪动剩余数据
			int consumed = (line_end + 1) - buf;
			int remaining = read_ptr - consumed;
			if (remaining > 0) {
				memmove(buf, line_end + 1, remaining);
			}
			read_ptr = remaining;
			buf[read_ptr] = '\0';
		}

		if (read_ptr >= BUF_SIZE - 1) {
			// 缓冲区满了，重置偏移量
			printf("[!] Buffer overflow risk from client, clearing.\n");
			read_ptr = 0; 
		}
	}
#ifdef _WIN32
	closesocket(client_fd);
#else
	close(client_fd);
#endif
}

#ifdef _WIN32
// Windows 线程处理
typedef struct {
	int client_fd;
	struct sockaddr_storage addr;
	socklen_t addr_len;
} client_thread_args;

DWORD WINAPI client_thread(LPVOID arg) {
	client_thread_args *args = (client_thread_args*)arg;
	handle_client(args->client_fd, (struct sockaddr*)&args->addr, args->addr_len);
	free(args);
	return 0;
}
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
	WSADATA wsa_data;
	WSAStartup(MAKEWORD(2, 2), &wsa_data);
#else
	// 忽略子进程结束信号，防止产生僵尸进程
	signal(SIGCHLD, SIG_IGN);
#endif

	// 支持命令行指定端口
	const char *port_str = (argc > 1) ? argv[1] : DEFAULT_PORT;

	// 优先创建IPv6 socket（可以同时接受IPv4和IPv6连接）
	int server_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (server_fd < 0) {
		perror("socket");
		return 1;
	}

	// 允许 IPv4 映射到 IPv6（双栈）
	int v6only = 0;
	setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v6only, sizeof(v6only));

	// 端口快速重用
	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

	struct sockaddr_in6 server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin6_family = AF_INET6;
	server_addr.sin6_addr = in6addr_any;           // 监听所有地址
	server_addr.sin6_port = htons(atoi(port_str));

	if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("bind");
		return 1;
	}

	if (listen(server_fd, 10) < 0) {   // 允许10个等待连接
		perror("listen");
		return 1;
	}

	printf("[+] TCP Ping Server listening on [::]:%s (IPv4/IPv6 dual-stack)\n", port_str);

	while (1) {
		struct sockaddr_storage client_addr;
		socklen_t addr_len = sizeof(client_addr);
		
		int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
		if (client_fd < 0) {
			perror("accept");
			continue;
		}

#ifdef _WIN32
		// Windows 使用线程
		client_thread_args *args = malloc(sizeof(client_thread_args));
		args->client_fd = client_fd;
		args->addr = client_addr;
		args->addr_len = addr_len;
		
		HANDLE hThread = CreateThread(NULL, 0, client_thread, args, 0, NULL);
		if (hThread) {
			CloseHandle(hThread);   // 我们不等待线程结束
		} else {
			free(args);
			closesocket(client_fd);
		}
#else
		// Linux 使用 fork
		pid_t pid = fork();
		if (pid == 0) {             // 子进程
			close(server_fd);       // 子进程不需要监听socket
			handle_client(client_fd, (struct sockaddr*)&client_addr, addr_len);
			exit(0);
		} else if (pid > 0) {       // 父进程
			close(client_fd);       // 父进程关闭客户端连接（交给子进程）
		}
		// pid < 0 就是fork失败，这里简单忽略了
#endif
	}

	// 正常情况下走不到这里
#ifdef _WIN32
	closesocket(server_fd);
	WSACleanup();
#else
	close(server_fd);
#endif
	return 0;
}
