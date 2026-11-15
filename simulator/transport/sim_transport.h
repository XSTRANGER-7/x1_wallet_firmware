/**
 * @file    sim_transport.h
 * @brief   Glue between the TCP frame server (sim_socket) and the simulator's
 *          USB device (simulator/USB/sim_usb.c).
 *
 * Environment variables (read once at start-up):
 *   CYPHEROCK_SIM_PORT=52640   TCP port on 127.0.0.1
 *   CYPHEROCK_SIM_VERBOSE=1    hex trace of every frame on stderr
 *   CYPHEROCK_SIM_LOOPBACK=1   echo frames back instead of calling firmware
 */
#ifndef SIM_TRANSPORT_H
#define SIM_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Called once for every 64-byte frame that arrives from the SDK. */
typedef void (*sim_transport_rx_cb_t)(const uint8_t *frame, size_t len);

/**
 * Listen on 127.0.0.1:52640 (or $CYPHEROCK_SIM_PORT). Returns 0 on success.
 * On failure it logs and returns -1; the simulator keeps running without a
 * host, like a device with no USB cable.
 */
int sim_transport_init(sim_transport_rx_cb_t rx_cb);

/** Non-blocking: accept, read and dispatch complete frames. Safe to call
 * often; re-entrant calls are ignored. */
void sim_transport_tick(void);

/** Firmware to SDK. Zero-pads into 64-byte frames. Returns 0 on success. */
int sim_transport_write(const uint8_t *data, size_t len);

/** 1 if an SDK client is connected, else 0. */
int sim_transport_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_TRANSPORT_H */
