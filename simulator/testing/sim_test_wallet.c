/**
 * @file    sim_test_wallet.c
 * @brief   SIMULATOR ONLY: a wallet backed by a published test mnemonic.
 *
 * The simulator's emulated X1 card (simulator/nfc/applet.c) does not speak
 * the encrypted card protocol the firmware uses, so wallets cannot be created
 * or rebuilt through cards in the simulator. For SDK testing, this file adds
 * a flash entry for a known test mnemonic; reconstruct_seed() derives the
 * seed from the same mnemonic (src/wallet/reconstruct_wallet_flow.c).
 *
 * NEVER use a real seed: the simulator's flash is a plain file on disk.
 */
#include "sim_test_wallet.h"

#if USE_SIMULATOR != 1
#error "sim_test_wallet.c must only be compiled into simulator builds"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flash_api.h"
#include "flash_config.h"
#include "flash_struct.h"
#include "flash_struct_priv.h" /* get_flash_ram_instance() */
#include "wallet_utilities.h"

#define SIM_TEST_WALLET_NAME "SIMTEST"

void sim_test_wallet_load(void) {
  const char *mnemonic = getenv("CYPHEROCK_SIM_TEST_MNEMONIC");
  if (NULL == mnemonic || 0 == mnemonic[0])
    return;

  Flash_Wallet wallet;
  memset(&wallet, 0, sizeof(wallet));
  calculate_wallet_id(wallet.wallet_id, mnemonic);

  uint8_t existing = 0;
  if (SUCCESS_ == get_index_by_name(SIM_TEST_WALLET_NAME, &existing)) {
    const Flash_Struct *flash = get_flash_ram_instance();
    if (0 != memcmp(flash->wallets[existing].wallet_id,
                    wallet.wallet_id,
                    WALLET_ID_SIZE)) {
      fprintf(stderr,
              "[sim] WARNING: wallet '%s' exists for a different mnemonic. "
              "Delete the simulator state files to reload it.\n",
              SIM_TEST_WALLET_NAME);
    }
    return;
  }

  strncpy((char *)wallet.wallet_name,
          SIM_TEST_WALLET_NAME,
          sizeof(wallet.wallet_name) - 1);
  wallet.wallet_info = 0;       /* no PIN, no passphrase */
  wallet.state = VALID_WALLET;
  wallet.cards_states = 0x0F;   /* all 4 cards "hold" a share */
  wallet.is_wallet_locked = 0;

  uint32_t index = 0;
  int status = add_wallet_to_flash(&wallet, &index);
  fprintf(stderr,
          "[sim] TEST WALLET '%s' %s (flash index %u). Test mnemonics only, "
          "never a real seed.\n",
          SIM_TEST_WALLET_NAME,
          SUCCESS_ == status ? "loaded" : "could not be added",
          (unsigned)index);
}
