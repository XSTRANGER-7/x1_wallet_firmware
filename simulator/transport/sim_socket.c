/**
 * @file    sim_socket.c
 * @brief   Non-blocking TCP frame server for the X1 firmware simulator.
 *
 * Carries fixed-size USB-style frames between the Cypherock SDK
 * (@cypherock/sdk-hw-simulator) and the simulator. Single-threaded and
 * non-blocking: call sim_socket_poll() often from the simulator's main
 * thread. Binds to 127.0.0.1 only.
 *
 * Builds with POSIX sockets (Linux, macOS) and Winsock (Windows / MinGW).
 * WebAssembly builds use sim_socket_wasm.c instead (same API).
 */
#ifndef __EMSCRIPTEN__
#include "sim_socket.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET sim_fd_t;
#define SIM_INVALID_FD INVALID_SOCKET
#define SIM_SEND_FLAGS 0
#define sim_close_fd closesocket

static int sim_would_block(void) {
  return WSAGetLastError() == WSAEWOULDBLOCK;
}

static void sim_sleep_short(void) {
  Sleep(1);
}

static int set_non_blocking(sim_fd_t fd) {
  u_long mode = 1;
  return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
}
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int sim_fd_t;
#define SIM_INVALID_FD (-1)
#ifdef MSG_NOSIGNAL
#define SIM_SEND_FLAGS MSG_NOSIGNAL
#else
#define SIM_SEND_FLAGS 0 /* macOS: SO_NOSIGPIPE is set on the client socket */
#endif
#define sim_close_fd close

static int sim_would_block(void) {
  return errno == EAGAIN || errno == EWOULDBLOCK;
}

static void sim_sleep_short(void) {
  usleep(100);
}

static int set_non_blocking(sim_fd_t fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
#endif

#define SIM_LOG(...)                                                           \
  do {                                                                         \
    if (sim_socket_verbose) {                                                  \
      fprintf(stderr, "[sim_socket] ");                                        \
      fprintf(stderr, __VA_ARGS__);                                            \
      fprintf(stderr, "\n");                                                   \
    }                                                                          \
  } while (0)

/* Give up on a client that stops reading for about 2 seconds. */
#define SIM_SOCKET_MAX_WRITE_WAITS 20000

static sim_fd_t listen_fd = SIM_INVALID_FD;
static sim_fd_t client_fd = SIM_INVALID_FD;
static uint16_t bound_port = 0;
static sim_socket_rx_cb_t on_frame = NULL;
static uint8_t rx_buf[SIM_SOCKET_FRAME_SIZE];
static size_t rx_len = 0;
static int sim_socket_verbose = 0;
#ifdef _WIN32
static int wsa_started = 0;
#endif

static void close_client(void) {
  if (client_fd != SIM_INVALID_FD) {
    sim_close_fd(client_fd);
    client_fd = SIM_INVALID_FD;
    rx_len = 0;
    SIM_LOG("client disconnected");
  }
}

static void close_listener(void) {
  if (listen_fd != SIM_INVALID_FD) {
    sim_close_fd(listen_fd);
    listen_fd = SIM_INVALID_FD;
  }
}

sim_socket_status_t sim_socket_init(uint16_t port, sim_socket_rx_cb_t rx_cb) {
  const char *verbose = getenv("CYPHEROCK_SIM_VERBOSE");
  sim_socket_verbose = (verbose && verbose[0] == '1');

  if (port == 0) {
    const char *env_port = getenv("CYPHEROCK_SIM_PORT");
    port = env_port ? (uint16_t)atoi(env_port) : SIM_SOCKET_DEFAULT_PORT;
  }

#ifdef _WIN32
  if (!wsa_started) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
      return SIM_SOCKET_ERR_SOCKET;
    wsa_started = 1;
  }
#endif

  on_frame = rx_cb;
  listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_fd == SIM_INVALID_FD)
    return SIM_SOCKET_ERR_SOCKET;

#ifndef _WIN32
  /* On Windows SO_REUSEADDR lets another process steal the port; skip it. */
  int yes = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* never beyond localhost */
  addr.sin_port = htons(port);

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    close_listener();
    return SIM_SOCKET_ERR_BIND;
  }
  if (listen(listen_fd, 1) != 0 || set_non_blocking(listen_fd) != 0) {
    close_listener();
    return SIM_SOCKET_ERR_LISTEN;
  }

  socklen_t addr_len = sizeof(addr);
  getsockname(listen_fd, (struct sockaddr *)&addr, &addr_len);
  bound_port = ntohs(addr.sin_port);

  /* Always printed: scripts/start-simulator.sh waits for this line. */
  fprintf(stderr, "[sim_socket] listening on 127.0.0.1:%u\n", bound_port);
  fflush(stderr);
  return SIM_SOCKET_OK;
}

static void accept_client(void) {
  sim_fd_t fd = accept(listen_fd, NULL, NULL);
  if (fd == SIM_INVALID_FD)
    return; /* would block: nobody waiting */

  if (client_fd != SIM_INVALID_FD) {
    /* One client at a time, like one USB host. Newest wins. */
    close_client();
  }
  set_non_blocking(fd);
  int yes = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes));
#ifdef SO_NOSIGPIPE
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
  client_fd = fd;
  rx_len = 0;
  SIM_LOG("client connected");
}

static void read_client(void) {
  uint8_t chunk[512];
  for (;;) {
    int n = (int)recv(client_fd, (char *)chunk, (int)sizeof(chunk), 0);
    if (n == 0) {
      close_client();
      return;
    }
    if (n < 0) {
      if (!sim_would_block())
        close_client();
      return;
    }
    for (int i = 0; i < n; i++) {
      rx_buf[rx_len++] = chunk[i];
      if (rx_len == SIM_SOCKET_FRAME_SIZE) {
        rx_len = 0;
        if (on_frame)
          on_frame(rx_buf, SIM_SOCKET_FRAME_SIZE);
        if (client_fd == SIM_INVALID_FD)
          return; /* the callback may have closed us */
      }
    }
  }
}

void sim_socket_poll(void) {
  if (listen_fd == SIM_INVALID_FD)
    return;
  accept_client();
  if (client_fd != SIM_INVALID_FD)
    read_client();
}

static int write_all(const uint8_t *buf, size_t len) {
  size_t sent = 0;
  int waits = 0;
  while (sent < len) {
    int n = (int)send(
        client_fd, (const char *)buf + sent, (int)(len - sent), SIM_SEND_FLAGS);
    if (n < 0) {
      if (sim_would_block() && waits++ < SIM_SOCKET_MAX_WRITE_WAITS) {
        sim_sleep_short();
        continue;
      }
      return -1;
    }
    sent += (size_t)n;
  }
  return 0;
}

sim_socket_status_t sim_socket_send(const uint8_t *data, size_t len) {
  if (client_fd == SIM_INVALID_FD)
    return SIM_SOCKET_ERR_NOT_CONNECTED;

  uint8_t frame[SIM_SOCKET_FRAME_SIZE];
  size_t offset = 0;
  do {
    size_t take = len - offset;
    if (take > SIM_SOCKET_FRAME_SIZE)
      take = SIM_SOCKET_FRAME_SIZE;
    memset(frame, 0, sizeof(frame));
    if (take > 0)
      memcpy(frame, data + offset, take);
    if (write_all(frame, sizeof(frame)) != 0) {
      close_client();
      return SIM_SOCKET_ERR_WRITE;
    }
    offset += take;
  } while (offset < len);
  return SIM_SOCKET_OK;
}

int sim_socket_is_connected(void) {
  return client_fd != SIM_INVALID_FD;
}

uint16_t sim_socket_port(void) {
  return bound_port;
}

void sim_socket_deinit(void) {
  close_client();
  close_listener();
  bound_port = 0;
}

#endif /* __EMSCRIPTEN__ */
