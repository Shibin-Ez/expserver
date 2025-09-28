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

// Function to reverse a string in-place
void strrev(char *str) {
    for (int start = 0, end = strlen(str) - 2; start < end; start++, end--) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
    }
}

int main () {
    // Creating listening sock
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
    bind(listen_sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    // Starting to listen
    listen(listen_sock_fd, MAX_ACCEPT_BACKLOG);
    printf("[INFO] Server listening on port %d\n", PORT);

    // Creating an object of struct socketaddr_in
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    // create an epoll instance
    int epoll_fd = epoll_create1(0);

    struct epoll_event event, events[MAX_EPOLL_EVENTS];

    event.events = EPOLLIN;
    event.data.fd = listen_sock_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock_fd, &event);

    char buff[BUFF_SIZE];
    memset(buff, 0, BUFF_SIZE);
    
    while (1) {
        printf("[DEBUG] Epoll wait\n");

        // wait for events
        int n_ready_fds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);

        for (int i=0; i<n_ready_fds; i++) {
            int curr_fd = events[i].data.fd;

            /* accept client connection and add to epoll */
            if (curr_fd == listen_sock_fd) {
                // event on listening socket

                /* accept connection */
                int new_conn_fd = accept(listen_sock_fd, (struct sockaddr *)&client_addr, &client_addr_len);
                
                /* add client socket to epoll */
                event.events = EPOLLIN;
                event.data.fd = new_conn_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_conn_fd, &event);

                printf("[INFO] New client connected to server\n");
            } else {
                // event on connection socket

                /* read message from client */
                ssize_t read_n = recv(curr_fd, buff, sizeof(buff), 0);
                if (read_n < 0) {
                    printf("[INFO] Error occured. Closing server\n");
                    close(curr_fd);
                    exit(1);
                }
                else if (read_n == 0) {
                    printf("[INFO] Client Disconnected. Closing connection\n");
                    close(curr_fd);
                    break;
                }

                // Print message from client
                printf("[CLIENT MESSAGE] %s", buff);

                /* reverse message */
                strrev(buff);

                /* send reversed message to client */
                send(curr_fd, buff, read_n, 0);

                memset(buff, 0, BUFF_SIZE);
            }
        }
    }
    
}