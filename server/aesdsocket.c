#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <syslog.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define PORT "9000"
#define DATA_FILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t caught_signal = 0;

void signal_handler(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        caught_signal = 1;
    }
}

int main(int argc, char *argv[]) {
    bool daemon_mode = false;
    int server_fd, client_fd;
    struct addrinfo hints, *servinfo, *p;
    int yes = 1;
    int rv;

    // 1. 檢查是否傳入了 -d 參數 (要求背景執行)
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }

    // 2. 初始化系統日誌 (Syslog)
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    // 3. 註冊訊號捕捉器 (攔截 SIGINT 和 SIGTERM)
    struct sigaction new_action;
    memset(&new_action, 0, sizeof(struct sigaction));
    new_action.sa_handler = signal_handler;
    
    if (sigaction(SIGTERM, &new_action, NULL) != 0) {
        syslog(LOG_ERR, "Error registering for SIGTERM");
        return -1;
    }
    if (sigaction(SIGINT, &new_action, NULL) != 0) {
        syslog(LOG_ERR, "Error registering for SIGINT");
        return -1;
    }

    // 4. 準備 Socket 位址設定 (Port 9000)
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       // 使用 IPv4
    hints.ai_socktype = SOCK_STREAM; // 使用 TCP
    hints.ai_flags = AI_PASSIVE;     // 自動填入本機 IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed: %s", gai_strerror(rv));
        return -1;
    }

    // 5. 建立 Socket 並綁定 (Bind)
    for(p = servinfo; p != NULL; p = p->ai_next) {
        server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server_fd == -1) continue;

        // 避免重開程式時遇到 "Address already in use"
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            syslog(LOG_ERR, "setsockopt failed");
            close(server_fd);
            freeaddrinfo(servinfo);
            return -1;
        }

        if (bind(server_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(server_fd);
            continue;
        }
        break; 
    }

    freeaddrinfo(servinfo); 

    if (p == NULL) {
        syslog(LOG_ERR, "Failed to bind to port 9000");
        return -1;
    }

    // 6. 處理 Daemon 背景執行模式 (必須在 bind 成功後)
    if (daemon_mode) {
        pid_t pid = fork();
        
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed");
            close(server_fd);
            return -1;
        }
        if (pid > 0) {
            exit(EXIT_SUCCESS); // 父行程結束
        }
        
        // 子行程專屬設定
        if (setsid() < 0) {
            syslog(LOG_ERR, "setsid failed");
            close(server_fd);
            return -1;
        }
        if (chdir("/") < 0) {
            syslog(LOG_ERR, "chdir failed");
            close(server_fd);
            return -1;
        }
        
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        open("/dev/null", O_RDWR);
        dup(0); 
        dup(0); 
    }

    // 7. 開始監聽 (Listen)
    if (listen(server_fd, 10) == -1) { 
        syslog(LOG_ERR, "Listen failed");
        close(server_fd);
        return -1;
    }
    
    // 8. 無窮迴圈：等待連線 -> 接收資料 -> 寫入檔案 -> 回傳歷史紀錄 -> 關閉連線
    struct sockaddr_in client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    char buffer[1024];

    while (!caught_signal) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_size);

        if (client_fd == -1) {
            if (caught_signal) break; // 若因收到訊號而中斷，跳出迴圈
            continue;
        }

        // 記錄連線 IP
        char *client_ip = inet_ntoa(client_addr.sin_addr);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // 打開暫存檔準備寫入 (附加模式)
        int file_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (file_fd == -1) {
            syslog(LOG_ERR, "Could not open data file for writing");
            close(client_fd);
            continue;
        }

        // 接收資料，直到讀到換行符號 (\n)
        ssize_t bytes_received;
        bool packet_complete = false;

        while (!packet_complete && (bytes_received = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
            write(file_fd, buffer, bytes_received);
            if (memchr(buffer, '\n', bytes_received) != NULL) {
                packet_complete = true;
            }
        }
        close(file_fd);

        // 重新打開檔案，將所有內容回傳給客戶端
        file_fd = open(DATA_FILE, O_RDONLY);
        if (file_fd != -1) {
            ssize_t bytes_read;
            while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
                send(client_fd, buffer, bytes_read, 0);
            }
            close(file_fd);
        }

        // 關閉本次連線
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
    }

    // 9. 優雅關閉 (Graceful Exit)
    syslog(LOG_INFO, "Caught signal, exiting");
    close(server_fd);
    remove(DATA_FILE); 
    closelog();
    
    return 0;
}
