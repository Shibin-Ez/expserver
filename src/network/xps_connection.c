#include "xps_connection.h"
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <assert.h>

// Function declaration for read callback of listener
void connection_loop_read_handler(void *ptr);
void connection_loop_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);

xps_connection_t *xps_connection_create(xps_core_t *core, int sock_fd) {

  xps_connection_t *connection = malloc(sizeof(xps_connection_t));
  if (connection == NULL) {
    logger(LOG_ERROR, "xps_connection_create()",
           "malloc() failed for 'connection'");
    return NULL;
  }

  /* attach sock_fd to epoll */
  xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLOUT, connection,
                  connection_loop_read_handler, connection_loop_write_handler,
                  connection_loop_close_handler);

  // Init values
  connection->core = core;
  connection->sock_fd = sock_fd;
  connection->listener = NULL;
  connection->remote_ip = get_remote_ip(sock_fd);
  connection->write_buff_list = xps_buffer_list_create();

  /* add connection to 'connections' list */
  vec_push(&core->connections, connection);

  logger(LOG_DEBUG, "xps_connection_create()", "created connection");
  return connection;
}

void xps_connection_destroy(xps_connection_t *connection) {

  /* validate params */
  assert(connection != NULL);

  /* set connection to NULL in 'connections' list */
  for (int i = 0; i < connection->core->connections.length; i++) {
    xps_connection_t *curr = connection->core->connections.data[i];
    if (curr == connection) {
      connection->core->connections.data[i] = NULL;
      break;
    }
  }

  /* detach connection from loop */
  xps_loop_detach(connection->core->loop, connection->sock_fd);

  /* close connection socket FD */
  close(connection->sock_fd);

  /* free connection->remote_ip */
  free(connection->remote_ip);

  /* free connection instance */
  free(connection);

  logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

// Function to reverse a string in-place
void connection_strrev(char *str) {
  for (int start = 0, end = strlen(str) - 2; start < end; start++, end--) {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
  }
}

// Function definition for read callback for connection
void connection_loop_read_handler(void *ptr) {

  /* validate params */
  assert(ptr != NULL);

  xps_connection_t *connection = ptr;

  char buff[DEFAULT_BUFFER_SIZE];
  memset(buff, 0, sizeof(buff));

  /* read data from client using recv() */
  long read_n = recv(connection->sock_fd, buff, sizeof(buff), 0);

  if (read_n < 0) {
    logger(LOG_ERROR, "xps_connection_read_handler()", "recv() failed");
    perror("Error message");
    xps_connection_destroy(connection);
    return;
  }

  if (read_n == 0) {
    logger(LOG_INFO, "connection_read_handler()", "peer closed connection");
    xps_connection_destroy(connection);
    return;
  }

  buff[read_n] = '\0';

  /* print client message */
  printf("[CLIENT MESSAGE] %s", buff);

  /* reverse client message */
  connection_strrev(buff);

  xps_buffer_t *write_buff = xps_buffer_create(read_n, read_n + 1, NULL);
  memcpy(write_buff->data, buff, read_n);
  xps_buffer_list_append(connection->write_buff_list, write_buff);

  // Sending reversed message to client
  connection_loop_write_handler(connection);
}

void connection_loop_write_handler(void *ptr) {
  assert(ptr != NULL);

  xps_connection_t *connection = ptr;

  if (connection->write_buff_list->len == 0)
    return;

  long bytes_written = 0;
  long message_len = connection->write_buff_list->len;

  xps_buffer_t *buff =
      xps_buffer_list_read(connection->write_buff_list, message_len);

  /* send message using send() */
  long write_n = send(connection->sock_fd, buff->data, buff->len, 0);
  if (write_n > 0) {
    // clear written buffers from buffer list
    xps_buffer_list_clear(connection->write_buff_list, write_n);
  } else if (write_n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // xps_buffer_destroy(buff);
      return;
    } else {
      logger(LOG_ERROR, "xps_connection_write_handler()", "send() failed");
      perror("Error message");
      xps_connection_destroy(connection);
      return;
    }
  }
}

void connection_loop_close_handler(void *ptr) {
  assert(ptr != NULL);

  xps_connection_t *connection = ptr;

  logger(LOG_INFO, "xps_connection_close_handler", "peer closed connection");
  xps_connection_destroy(connection);
}