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

// 全域變數，用來通知主迴圈是否收到了中斷訊號 (SIGINT / SIGTERM)
volatile sig_atomic_t caught_signal = 0;

// 訊號處理函式：當按下 Ctrl+C 或被系統 kill 時會觸發
void signal_handler(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        caught_signal = 1;
    }
}

int main(int argc, char *argv[]) {
    bool daemon_mode = false;

    // 1. 檢查是否傳入了 -d 參數 (要求背景執行)
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }

    // 2. 初始化系統日誌 (Syslog)
    // LOG_USER 表示這是一般使用者的應用程式訊息
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    // 暫時先用一下這個變數
    if (daemon_mode) {
        syslog(LOG_INFO, "Daemon mode specified");
    }

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
    int server_fd;
    struct addrinfo hints, *servinfo, *p;
    int yes = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // 使用 IPv4
    hints.ai_socktype = SOCK_STREAM; // 使用 TCP
    hints.ai_flags = AI_PASSIVE;     // 自動填入本機 IP

    if (getaddrinfo(NULL, PORT, &hints, &servinfo) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed");
        return -1;
    }

    // 5. 建立 Socket 並綁定 (Bind)
    for(p = servinfo; p != NULL; p = p->ai_next) {
        server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server_fd == -1) continue;

        // 💡 神奇小設定：避免重開程式時遇到 "Address already in use" 卡住的問題
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            syslog(LOG_ERR, "setsockopt failed");
            freeaddrinfo(servinfo);
            return -1;
        }

        if (bind(server_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(server_fd);
            continue;
        }
        break; // 成功綁定就跳出迴圈
    }

    freeaddrinfo(servinfo); // 用完就釋放記憶體

    // 如果把清單找完還是沒綁成功，就報錯退出
    if (p == NULL) {
        syslog(LOG_ERR, "Failed to bind to port 9000");
        return -1;
    }

    // 6. 處理 Daemon 背景執行模式 (必須在 bind 成功後)
    if (daemon_mode) {
        syslog(LOG_INFO, "Entering daemon mode");
        pid_t pid = fork(); // 分身術！產生子行程
        
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed");
            close(server_fd);
            return -1;
        }
        if (pid > 0) {
            // 這是原本的父行程，直接安息結束，讓子行程在背景繼續活著
            exit(EXIT_SUCCESS); 
        }
        
        // --- 以下是存活下來的子行程 (Daemon) 專屬設定 ---
        setsid(); // 建立新的獨立 Session，脫離原本終端機的控制
        
        // 更改工作目錄到根目錄，避免鎖死原本的資料夾無法卸載
        if (chdir("/") == -1) {
            syslog(LOG_ERR, "chdir failed");
            close(server_fd);
            return -1;
        }
        
        // 將標準輸入、輸出、錯誤都重新導向到「黑洞 (/dev/null)」
        // 這樣它在背景才不會亂印東西干擾終端機畫面
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        open("/dev/null", O_RDWR); // 變成 stdin (0)
        dup(0);                    // 變成 stdout (1)
        dup(0);                    // 變成 stderr (2)
    }

    // 7. 開始監聽 (Listen)
    if (listen(server_fd, 5) == -1) { // 允許 5 個連線在門口排隊
        syslog(LOG_ERR, "Listen failed");
        close(server_fd);
        return -1;
    }
    
    // 5. 無窮迴圈：等待連線 -> 接收資料 -> 寫入檔案 -> 回傳歷史紀錄 -> 關閉連線
    while (!caught_signal) {
        struct sockaddr_storage their_addr;
        socklen_t sin_size = sizeof their_addr;
        
        // 程式會卡在這裡等待，直到有客戶端連線
        int client_fd = accept(server_fd, (struct sockaddr *)&their_addr, &sin_size);

        // 錯誤處理：如果是因為按下 Ctrl+C 被中斷，就跳出迴圈準備結束程式
        if (client_fd == -1) {
            if (caught_signal) break; 
            syslog(LOG_ERR, "accept failed");
            continue;
        }

        // 取得並轉換客戶端的 IP 位址為字串 (為了印 Log)
        char s[INET6_ADDRSTRLEN];
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)&their_addr;
        inet_ntop(AF_INET, &(ipv4->sin_addr), s, sizeof s);
        syslog(LOG_INFO, "Accepted connection from %s", s);

        // 打開檔案準備寫入 (O_APPEND 代表不會覆蓋，會接續寫在檔案尾巴)
        int file_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (file_fd == -1) {
            syslog(LOG_ERR, "could not open data file");
            close(client_fd);
            continue;
        }

        // 接收資料，直到讀到換行符號 (\n)
        char buffer[1024];
        ssize_t bytes_received;
        bool packet_complete = false;

        while (!packet_complete && (bytes_received = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
            write(file_fd, buffer, bytes_received);
            // 檢查這包資料裡面有沒有換行符號
            if (memchr(buffer, '\n', bytes_received) != NULL) {
                packet_complete = true;
            }
        }
        close(file_fd); // 寫完先關閉檔案

        // 重新打開檔案，把裡面所有的內容傳回給客戶端
        file_fd = open(DATA_FILE, O_RDONLY);
        if (file_fd != -1) {
            ssize_t bytes_read;
            while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
                send(client_fd, buffer, bytes_read, 0);
            }
            close(file_fd);
        }

        // 關閉客戶端連線並記錄
        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", s);
    }

    // 6. 收到中斷訊號後的完美收尾 (Graceful Exit)
    syslog(LOG_INFO, "Caught signal, exiting");
    close(server_fd);
    remove(DATA_FILE); // 刪除暫存檔
    closelog();
    
    return 0;
}
