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
    #define close closesocket
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <sys/wait.h>
    #include <sys/time.h>
    #include <netdb.h>
#endif

#define DEFAULT_PORT "9999"
#define BUF_SIZE 64

// 跨平台获取微秒时间戳
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

void handle_client(int client_fd, struct sockaddr *client_addr, socklen_t addr_len) {
    char buf[BUF_SIZE];
    char response[BUF_SIZE];
    char client_str[INET6_ADDRSTRLEN];

    // 解析客户端地址
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

    while (1) {
        memset(buf, 0, BUF_SIZE);
        int n = recv(client_fd, buf, BUF_SIZE-1, 0);
        
        if (n <= 0) {
            printf("[-] Client [%s]:%d disconnected\n", client_str, port);
            break;
        }

        buf[strcspn(buf, "\r\n")] = '\0';

        if (strcmp(buf, "PING") == 0) {
            long long timestamp = get_usec_timestamp();
            snprintf(response, BUF_SIZE, "PONG %lld\n", timestamp);
            send(client_fd, response, strlen(response), 0);
        }
    }

    close(client_fd);
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
#endif

    const char *port_str = (argc > 1) ? argv[1] : DEFAULT_PORT;

    int server_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    // 允许 IPv4 映射到 IPv6（双栈）
    int v6only = 0;
    setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v6only, sizeof(v6only));

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    struct sockaddr_in6 server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_addr = in6addr_any;
    server_addr.sin6_port = htons(atoi(port_str));

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
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
            CloseHandle(hThread);
        } else {
            free(args);
            close(client_fd);
        }
#else
        // Linux 使用 fork
        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd);
            handle_client(client_fd, (struct sockaddr*)&client_addr, addr_len);
            exit(0);
        } else if (pid > 0) {
            close(client_fd);
        }
#endif
    }

    close(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
