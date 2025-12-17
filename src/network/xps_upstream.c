#include "xps_upstream.h"

xps_connection_t *xps_upstream_create(xps_core_t *core, const char *host,
                                      u_int port) {
  /* validate parameter */
  assert(core != NULL);
  assert(host != NULL);
  assert(is_valid_port(port));

  /* create a socket and connect to host and port to upstream using
   * xps_getaddrinfo and connect function */
  int upstream_sock_fd = socket(AF_INET, SOCK_STREAM, 0);

  struct addrinfo *addr_info = xps_getaddrinfo(host, UPSTREAM_PORT);

  if (addr_info == NULL) {
    logger(LOG_ERROR, "xps_upstream_create()", "xps_getaddrinfo() failed");
    close(upstream_sock_fd);
    return NULL;
  }

  int connect_error =
      connect(upstream_sock_fd, addr_info->ai_addr, addr_info->ai_addrlen);

  if (!(connect_error == OK || errno == EINPROGRESS)) {
    logger(LOG_ERROR, "xps_upstream_create()", "connect() failed");
    perror("Error message");
    close(upstream_sock_fd);
    return NULL;
  }

  /* create a connection to upstream with core and sock_fd*/
  xps_connection_t *connection = xps_connection_create(core, upstream_sock_fd);

  return connection;
}