/**
 * @file    sim_socket.h
 * @brief   Non-blocking TCP server that carries USB-style frames between the
 *          Cypherock SDK (hw-simulator package) and the X1 firmware simulator.
 *
 * The simulator runs a single-threaded main loop. This module never blocks:
 * call sim_socket_poll() once per loop iteration and it will accept a client,
 * read whatever bytes are available, rebuild fixed-size frames and hand each
 * complete frame to the callback.
 *
 * Copy into: x1_wallet_firmware/simulator/transport/
 */
#ifndef SIM_SOCKET_H
#define SIM_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Must match FRAME_SIZE in packages/hw-simulator/src/constants.ts. */
#ifndef SIM_SOCKET_FRAME_SIZE
#define SIM_SOCKET_FRAME_SIZE 64
#endif

/** Default TCP port; override with env CYPHEROCK_SIM_PORT at runtime. */
#ifndef SIM_SOCKET_DEFAULT_PORT
#define SIM_SOCKET_DEFAULT_PORT 52640
#endif

/** Called once per complete frame received from the SDK. */
typedef void (*sim_socket_rx_cb_t)(const uint8_t *frame, size_t len);

typedef enum {
  SIM_SOCKET_OK = 0,
  SIM_SOCKET_ERR_SOCKET = -1,
  SIM_SOCKET_ERR_BIND = -2,
  SIM_SOCKET_ERR_LISTEN = -3,
  SIM_SOCKET_ERR_NOT_CONNECTED = -4,
  SIM_SOCKET_ERR_WRITE = -5,
} sim_socket_status_t;

/**
 * Start listening on 127.0.0.1:port. Pass port 0 to use
 * CYPHEROCK_SIM_PORT or SIM_SOCKET_DEFAULT_PORT.
 */
sim_socket_status_t sim_socket_init(uint16_t port, sim_socket_rx_cb_t rx_cb);

/** Accept/read without blocking. Call every main-loop iteration. */
void sim_socket_poll(void);

/**
 * Send data to the connected SDK client, split into zero-padded frames of
 * SIM_SOCKET_FRAME_SIZE bytes.
 */
sim_socket_status_t sim_socket_send(const uint8_t *data, size_t len);

/** 1 if an SDK client is connected, else 0. */
int sim_socket_is_connected(void);

/** Port actually bound (useful when 0 was requested in tests). */
uint16_t sim_socket_port(void);

/** Close client and listener. */
void sim_socket_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_SOCKET_H */
