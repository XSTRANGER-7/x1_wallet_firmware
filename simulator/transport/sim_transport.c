/**
 * @file    sim_transport.c
 * @brief   Routes frames between the TCP bridge and the simulator USB device.
 *
 * sim_socket.c knows nothing about Cypherock; this file adds the runtime
 * switches (loopback, trace) and hands every frame to the receive callback
 * that simulator/USB/sim_usb.c registers.
 */
#include "sim_transport.h"

#include <stdio.h>
#include <stdlib.h>

#include "sim_socket.h"

static sim_transport_rx_cb_t firmware_rx = NULL;
static int loopback = 0; /* CYPHEROCK_SIM_LOOPBACK=1 */
static int trace = 0;    /* CYPHEROCK_SIM_VERBOSE=1 */
static int in_tick = 0;

static void dump(const char *dir, const uint8_t *buf, size_t len) {
  if (!trace)
    return;
  fprintf(stderr, "[sim_transport] %s %2u:", dir, (unsigned)len);
  for (size_t i = 0; i < len; i++)
    fprintf(stderr, " %02x", buf[i]);
  fputc('\n', stderr);
  fflush(stderr);
}

static void on_frame(const uint8_t *frame, size_t len) {
  dump("rx", frame, len);
  if (loopback) {
    sim_socket_send(frame, len);
    return;
  }
  if (firmware_rx)
    firmware_rx(frame, len);
}

int sim_transport_init(sim_transport_rx_cb_t rx_cb) {
  const char *lb = getenv("CYPHEROCK_SIM_LOOPBACK");
  const char *vb = getenv("CYPHEROCK_SIM_VERBOSE");
  loopback = (lb && lb[0] == '1');
  trace = (vb && vb[0] == '1');
  firmware_rx = rx_cb;

  if (sim_socket_init(0, on_frame) != SIM_SOCKET_OK) {
    fprintf(stderr,
            "[sim_transport] bridge not started (port busy?); running without "
            "a host\n");
    return -1;
  }
  if (loopback)
    fprintf(stderr,
            "[sim_transport] LOOPBACK: frames are echoed, firmware is not "
            "called\n");
  return 0;
}

void sim_transport_tick(void) {
  if (in_tick) /* never re-enter while the parser is running */
    return;
  in_tick = 1;
  sim_socket_poll();
  in_tick = 0;
}

int sim_transport_write(const uint8_t *data, size_t len) {
  dump("tx", data, len);
  return sim_socket_send(data, len) == SIM_SOCKET_OK ? 0 : -1;
}

int sim_transport_is_connected(void) {
  return sim_socket_is_connected();
}
