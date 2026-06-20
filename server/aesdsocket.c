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

    // 每次啟動前，先清除可能殘留的舊暫存檔，確保測試環境乾淨
    remove(DATA_FILE);

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

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

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       
    hints.ai_socktype = SOCK_STREAM; 
    hints.ai_flags = AI_PASSIVE;     

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed: %s", gai_strerror(rv));
        return -1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server_fd == -1) continue;

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

    // 💡 關鍵修正 1：必須在 fork 之前開始監聽！
    // 這樣就算父行程一結束，測試腳本立刻連線，作業系統也能幫我們把連線接住。
    if (listen(server_fd, 10) == -1) { 
        syslog(LOG_ERR, "Listen failed");
        close(server_fd);
        return -1;
    }

    // 💡 關鍵修正 2：確保準備就緒後，才進入 Daemon 模式
    if (daemon_mode) {
        pid_t pid = fork();
        
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed");
            close(server_fd);
            return -1;
        }
        if (pid > 0) {
            exit(EXIT_SUCCESS); 
        }
        
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
    
    struct sockaddr_in client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    char buffer[1024];

    while (!caught_signal) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_size);

        if (client_fd == -1) {
            if (caught_signal) break; 
            continue;
        }

        char *client_ip = inet_ntoa(client_addr.sin_addr);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        int file_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (file_fd == -1) {
            syslog(LOG_ERR, "Could not open data file for writing");
            close(client_fd);
            continue;
        }

        ssize_t bytes_received;
        bool packet_complete = false;

        while (!packet_complete && (bytes_received = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
            write(file_fd, buffer, bytes_received);
            if (memchr(buffer, '\n', bytes_received) != NULL) {
                packet_complete = true;
            }
        }
        close(file_fd);

        file_fd = open(DATA_FILE, O_RDONLY);
        if (file_fd != -1) {
            ssize_t bytes_read;
            while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
                send(client_fd, buffer, bytes_read, 0);
            }
            close(file_fd);
        }

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    close(server_fd);
    remove(DATA_FILE); 
    closelog();
    
    return 0;
}
