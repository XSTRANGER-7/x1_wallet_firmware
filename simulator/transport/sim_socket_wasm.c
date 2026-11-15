/**
 * @file    sim_socket_wasm.c
 * @brief   sim_socket.h for the WebAssembly build of the simulator.
 *
 * In a browser there is no TCP server. The page that hosts the firmware
 * (the SDK running in the same tab) writes host-to-device bytes with
 * sim_wasm_host_write(), and device-to-host frames are handed to the page's
 * Module.x1OnFrame(Uint8Array) callback. Frames are the same fixed 64-byte
 * frames as the TCP bridge, so sim_transport.c and sim_usb.c are unchanged.
 */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <string.h>

#include "sim_socket.h"

#define RX_CAPACITY (SIM_SOCKET_FRAME_SIZE * 512)

static sim_socket_rx_cb_t on_frame = NULL;
static uint8_t rx_buf[RX_CAPACITY];
static size_t rx_len = 0;
static int host_connected = 0;

/* clang-format off */
EM_JS(void, js_device_frame, (const uint8_t *ptr, int len), {
  if (Module.x1OnFrame) Module.x1OnFrame(HEAPU8.slice(ptr, ptr + len));
});
/* clang-format on */

/** Page to firmware: queue raw bytes (any length; split into 64-byte frames
 * by sim_socket_poll()). Returns the number of bytes accepted. */
EMSCRIPTEN_KEEPALIVE int sim_wasm_host_write(const uint8_t *data, int len) {
  if (len <= 0)
    return 0;
  size_t room = RX_CAPACITY - rx_len;
  size_t n = (size_t)len < room ? (size_t)len : room;
  memcpy(rx_buf + rx_len, data, n);
  rx_len += n;
  host_connected = 1;
  return (int)n;
}

/** Page tells the firmware a host attached (1) or left (0). Leaving drops
 * any partial frame, like a USB cable pulled mid-transfer. */
EMSCRIPTEN_KEEPALIVE void sim_wasm_host_connect(int connected) {
  host_connected = connected ? 1 : 0;
  if (!host_connected)
    rx_len = 0;
}

sim_socket_status_t sim_socket_init(uint16_t port, sim_socket_rx_cb_t rx_cb) {
  (void)port;
  on_frame = rx_cb;
  rx_len = 0;
  return SIM_SOCKET_OK;
}

void sim_socket_poll(void) {
  size_t off = 0;
  while (rx_len - off >= SIM_SOCKET_FRAME_SIZE) {
    uint8_t frame[SIM_SOCKET_FRAME_SIZE];
    memcpy(frame, rx_buf + off, SIM_SOCKET_FRAME_SIZE);
    off += SIM_SOCKET_FRAME_SIZE;
    if (on_frame)
      on_frame(frame, SIM_SOCKET_FRAME_SIZE);
  }
  if (off > 0) {
    memmove(rx_buf, rx_buf + off, rx_len - off);
    rx_len -= off;
  }
}

sim_socket_status_t sim_socket_send(const uint8_t *data, size_t len) {
  if (!host_connected)
    return SIM_SOCKET_ERR_NOT_CONNECTED;
  size_t off = 0;
  do {
    uint8_t frame[SIM_SOCKET_FRAME_SIZE] = {0};
    size_t n = len - off < SIM_SOCKET_FRAME_SIZE ? len - off
                                                 : SIM_SOCKET_FRAME_SIZE;
    if (n > 0)
      memcpy(frame, data + off, n);
    js_device_frame(frame, SIM_SOCKET_FRAME_SIZE);
    off += n;
  } while (off < len);
  return SIM_SOCKET_OK;
}

int sim_socket_is_connected(void) {
  return host_connected;
}

uint16_t sim_socket_port(void) {
  return 0;
}

void sim_socket_deinit(void) {
  host_connected = 0;
  rx_len = 0;
}

#endif /* __EMSCRIPTEN__ */
