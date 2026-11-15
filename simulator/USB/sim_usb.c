/**
 * @file    sim_usb.c
 * @brief   Simulator USB device: carries USB reports over the localhost TCP
 *          bridge (simulator/transport) instead of a USB cable.
 *
 * Replaces the old file-based stub (/tmp/cypherock_device_*.bin), whose
 * receive path was a TODO and whose poll was never called.
 */
#include "sim_usb.h"

#include "../testing/sim_test_wallet.h"
#include "sim_transport.h"
#include "usb_api_priv.h" /* comm_packet_parser() */

/* Same job as the hardware USB receive path: give the report to the parser
 * that usb_init() registers with lusb on a real device (usb_api.c:132-136).
 * The interface must stay the same for a whole command, or the firmware
 * answers APP_BUSY_WITH_OTHER_INTERFACE (usb_internals.c:191-196). */
static int8_t SIM_Receive_FS(const uint8_t *Buf, const uint32_t *Len) {
  comm_packet_parser(Buf, (uint16_t)*Len, COMM_LIBUSB__HID);
  return USBD_OK;
}

static void on_frame_from_host(const uint8_t *frame, size_t len) {
  uint32_t n = (uint32_t)len; /* always SIM_SOCKET_FRAME_SIZE (64) */
  SIM_Receive_FS(frame, &n);
}

void SIM_USB_DEVICE_Init() {
  /* Optional SDK test fixture; no-op unless CYPHEROCK_SIM_TEST_MNEMONIC is set */
  sim_test_wallet_load();
  sim_transport_init(on_frame_from_host);
}

/* Called by comm_write_packet() (usb_internals.c:423) and by the parser for
 * the SDK version reply (usb_rx_parser.c:181). size is 16..64 bytes. */
void SIM_Transmit_FS(uint8_t *data, uint8_t size) {
  /* With no host connected the packet is dropped, like a device with no USB
   * host. The SDK always starts a new exchange after connecting. */
  (void)sim_transport_write(data, size);
}

/* Called from BSP_DelayMs() in simulator/BSP/board.c. */
void usbsim_continue_loop() {
  sim_transport_tick();
}
