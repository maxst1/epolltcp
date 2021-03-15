#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

const size_t MAX_CONNECTIONS = 128;
bool DEBUG = false;

bool got_SIGTERM = false;
bool epoll_waiting = false;

int listener;
int epoll_fd;

void Exit(int code) {
    shutdown(listener, SHUT_RDWR);
    close(listener);
    close(epoll_fd);
    exit(code);
}

void SIGTERM_Handler(int signum) {
    got_SIGTERM = true;

    if (epoll_waiting) {
        Exit(0);
    }
}

void RegisterEvent(int epoll, int fd, uint32_t events) {
    struct epoll_event event_rw_ready;
    memset(&event_rw_ready, 0, sizeof(event_rw_ready));

    event_rw_ready.data.fd = fd;
    event_rw_ready.events = events;

    epoll_ctl(epoll, EPOLL_CTL_ADD, fd, &event_rw_ready);
}

void MakeNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void Error(const char* err) {
    perror(err);
    Exit(1);
}

int SetServer(const char* ip_addr, uint16_t port, size_t max_connections) {
    int server_socket;
    if (-1 == (server_socket = socket(AF_INET, SOCK_STREAM, 0))) {
        Error("socket");
    }
    if (DEBUG) {
        printf("set socket\n");
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = inet_addr(ip_addr)
    };

    if (-1 == bind(server_socket, (const struct sockaddr*)&addr, sizeof(addr))) {
        Error("bind");
    }
    if (DEBUG) {
        printf("binded\n");
    }

    if (-1 == listen(server_socket, max_connections)) {
        Error("listen");
    }
    if (DEBUG) {
        printf("listening\n");
    }

    return server_socket;
}

char* ReadSocket(int socket) {
    char* res = NULL;

    char buffer[8192];  
    memset(buffer, 0, sizeof(buffer));
    int count;

    if (-1 == (count = read(socket, buffer, sizeof(buffer)))) {
        Error("read");
    }

    res = calloc(count + 1, 1);
    memcpy(res, buffer, count);
    res[count] == '\0';

    return res;
}

void WriteSocket(int socket, const char* str, size_t length) {
    int total = 0;
    int n;

    while(total < length) {
        if (-1 == (n = write(socket, str + total, length - total))) {
            Error("write");
        }

        total += n;
    }
}

char* MakeOutput(const char* input) {
    size_t len = strlen(input);

    char* res = calloc(len + 1, 1);
    memcpy(res, input, len);
    res[len] = '\0';

    for (int i = 0; i < len; ++i) {
        if ('a' <= res[i] && res[i] <= 'z') {
            res[i] += 'A' - 'a';
        }
    }

    return res;
}

void ClientRoutine(int client_socket) {
    if (DEBUG) {
        printf("accepted\n");
    }

    char* input = ReadSocket(client_socket);
    if (DEBUG) {
        printf("\ninput:\n%s\n", input);
    }

    if (0 == strlen(input)) {
        shutdown(client_socket, SHUT_RDWR);
        close(client_socket);
        return;
    }

    char* output = MakeOutput(input);
    if (DEBUG) {
        printf("\noutput:\n%s\n", output);
    }

    WriteSocket(client_socket, output, strlen(output));
    if (DEBUG) {
        printf("written\n");
    }

    free(input);
    free(output);
}

int main(int argc, char* argv[]) {
    uint16_t port = strtol(argv[1], NULL, 10);
    //DEBUG = strtol(argv[2], NULL, 10);

    sigaction(
        SIGTERM,
        &(struct sigaction) {
            .sa_handler = SIGTERM_Handler,
            .sa_flags = SA_RESTART
        },
        NULL
    );

    struct epoll_event events[MAX_CONNECTIONS];

    listener = SetServer("127.0.0.1", port, MAX_CONNECTIONS);
    if (-1 == (epoll_fd = epoll_create1(0))) {
        Error("epoll_create");
    }

    MakeNonBlocking(listener);
    RegisterEvent(epoll_fd, listener, EPOLLIN);

    int n;
    while (!got_SIGTERM) {
        epoll_waiting = true;
        if (-1 == (n = epoll_wait(epoll_fd, events, MAX_CONNECTIONS, -1))) {
            Error("epoll_wait");
        }
        epoll_waiting = false;

        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == listener) {
                if (DEBUG) {
                    printf("new client\n");
                }
                int client_socket;
                if (-1 == (client_socket = accept(listener, NULL, NULL))) {
                    Error("accept");
                }

                MakeNonBlocking(client_socket);
                RegisterEvent(epoll_fd, client_socket, EPOLLIN);
            } 
            else {
                if (events[i].events & EPOLLIN) {
                    int client_socket = events[i].data.fd;
                    ClientRoutine(client_socket);
                }
            }
        }
    }

    Exit(0);
}