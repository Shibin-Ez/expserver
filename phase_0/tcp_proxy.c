#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_EPOLL_EVENTS 10
#define UPSTREAM_PORT 3000
#define MAX_SOCKS 10

int listen_sock_fd, epoll_fd;
struct epoll_event events[MAX_EPOLL_EVENTS];
int route_table[MAX_SOCKS][2], route_table_size = 0;
char buff[BUFF_SIZE];

// Creating a client object of struct socketaddr_in
struct sockaddr_in client_addr;
socklen_t client_addr_len = sizeof(client_addr);

// Enum for indentifying an epoll event is from client or upstream
enum {
    CLIENT_SOCK_FD,
    UPSTREAM_SOCK_FD,
};

// Function to reverse a string in-place
void strrev(char *str) {
    for (int start = 0, end = strlen(str) - 2; start < end; start++, end--) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
    }
}

int create_loop() {
    /* return new epoll instance */

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        exit(1);
    }

    return epoll_fd;
}

void loop_attach(int epoll_fd, int fd, int events) {
    /* attach fd to epoll */

    struct epoll_event event;
    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
        perror("epoll_ctl: listen_sock_fd");
        exit(1);
    }
}

int create_server() {
    /* create listening socket and return it */
    int listen_sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Setting sock opt reuse addr
    int enable = 1;
    setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

    // Creating an object of struct socketaddr_in
    struct sockaddr_in server_addr;

    // Setting up server addr
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    // Binding listening sock to port
    if (bind(listen_sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_sock_fd);
        exit(1);
    }

    // Starting to listen
    if (listen(listen_sock_fd, MAX_ACCEPT_BACKLOG) < 0) {
        perror("listen");
        close(listen_sock_fd);
        exit(1);
    }
    
    printf("[INFO] Server listening on port %d\n", PORT);
    return listen_sock_fd;
}

int connect_upstream() {

    int upstream_sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    /* add upstream server details */
    struct sockaddr_in upstream_addr;
    upstream_addr.sin_family = AF_INET;
    upstream_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    upstream_addr.sin_port = htons(UPSTREAM_PORT);

    /* connect to upstream server */
    if (connect(upstream_sock_fd, (struct sockaddr *)&upstream_addr, sizeof(upstream_addr)) < 0) {
        perror("connect to upstream");
        close(upstream_sock_fd);
        return -1;
    }

    return upstream_sock_fd;

}

void accept_connection(int listen_sock_fd) {
    /* accept connection */
    int new_conn_fd = accept(listen_sock_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (new_conn_fd < 0) {
        perror("accept");
        return;
    }

    /* add client socket to epoll */
    loop_attach(epoll_fd, new_conn_fd, EPOLLIN);

    // create connection to upstream server
    int upstream_sock_fd = connect_upstream();
    if (upstream_sock_fd < 0) {
        fprintf(stderr, "[ERROR] Could not connect to upstream %d. Closing client socket.\n", UPSTREAM_PORT);
        close(new_conn_fd);
        return;
    }

    /* add upstream_sock_fd to loop using loop_attach() */
    loop_attach(epoll_fd, upstream_sock_fd, EPOLLIN);

    // add conn_sock_fd and upstream_sock_fd to routing table
    if (route_table_size < MAX_SOCKS) {
        route_table[route_table_size][0] = new_conn_fd;
        route_table[route_table_size][1] = upstream_sock_fd;
        route_table_size += 1;
    } else {
        fprintf(stderr, "[ERROR] Route table full\n");
        close(new_conn_fd);
        close(upstream_sock_fd);
        return;
    }

    printf("[INFO] New client connected to server (client fd=%d -> upstream fd=%d)\n", new_conn_fd, upstream_sock_fd);
}

void handle_client(int conn_sock_fd) {
    /* read message from client to buffer using recv */
    int read_n = recv(conn_sock_fd, buff, sizeof(buff), 0);

    // client closed connection or error occurred
    if (read_n <= 0) {
        close(conn_sock_fd);
        return;
    }
    else if (read_n == 0) {
        printf("[INFO] Client Disconnected. Closing connection\n");
        close(conn_sock_fd);
        return;
    }

    /* print client message (helpful for Milestone #2) */
    printf("[CLIENT MESSAGE] %s", buff);

    /* find the right upstream socket from the route table */
    int upstream_sock_fd = -1;
    for (int i = 0; i < route_table_size; i++) {
        if (route_table[i][0] == conn_sock_fd) {
            upstream_sock_fd = route_table[i][1];
            break;
        }
    }

    // sending client message to upstream
    int bytes_written = 0;
    int message_len = read_n;
    while (bytes_written < message_len) {
        int n = send(upstream_sock_fd, buff + bytes_written, message_len - bytes_written, 0);
        bytes_written += n;
    }

    memset(buff, 0, BUFF_SIZE);
}

void handle_upstream(int upstream_sock_fd) {

    /* read message from upstream to buffer using recv */
    int read_n = recv(upstream_sock_fd, buff, sizeof(buff), 0);

    // Upstream closed connection or error occurred
    if (read_n <= 0) {
        close(upstream_sock_fd);
        perror("Upstream closed connection");
        return;
    }

    /* find the right client socket from the route table */
    int conn_sock_fd = -1;
    for (int i = 0; i < route_table_size; i++) {
        if (route_table[i][1] == upstream_sock_fd) {
            conn_sock_fd = route_table[i][0];
            break;
        }
    }

    /* send upstream message to client */
    int bytes_written = 0;
    int message_len = read_n;
    while (bytes_written < message_len) {
        int n = send(conn_sock_fd, buff + bytes_written, message_len - bytes_written, 0);
        bytes_written += n;
    }
}

int get_sock_type(int fd) {
    for (int i = 0; i < route_table_size; i++) {
        if (route_table[i][0] == fd) return CLIENT_SOCK_FD;
        if (route_table[i][1] == fd) return UPSTREAM_SOCK_FD;
    }

    return -1; // Entry Not found in route table
}

void loop_run(int epoll_fd) {
    /* infinite loop and processing epoll events */
    memset(buff, 0, BUFF_SIZE);
    
    while (1) {
        printf("[DEBUG] Epoll wait\n");

        // wait for events
        int n_ready_fds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);

        for (int i = 0; i < n_ready_fds; i++) {
            int curr_fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            /* accept client connection and add to epoll */
            if (curr_fd == listen_sock_fd) {
                accept_connection(listen_sock_fd);
                continue;
            }

            int fd_type = get_sock_type(curr_fd);

            if (ev & EPOLLIN) {
                if (fd_type == CLIENT_SOCK_FD) {
                    handle_client(curr_fd);
                } else if (fd_type == UPSTREAM_SOCK_FD) {
                    handle_upstream(curr_fd);
                } else {
                    fprintf(stderr, "[WARN] EPOLLIN for unknown fd %d\n", curr_fd);
                }
            } else {
                fprintf(stderr, "[ERROR] Unexpected epoll event mask 0x%x for fd %d\n", ev, curr_fd);
            }
        }
    }
}

int main () {
    // Creating listening sock
    listen_sock_fd = create_server();

    // create an epoll instance
    epoll_fd = create_loop();

    // Attach the listening socket to epoll
    loop_attach(epoll_fd, listen_sock_fd, EPOLLIN);

    loop_run(epoll_fd);
    
    close(listen_sock_fd);
    return 0;
}