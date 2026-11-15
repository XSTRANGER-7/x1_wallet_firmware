/**
 * @file    sim_test_wallet.h
 * @brief   SIMULATOR ONLY: load a wallet entry for a published test mnemonic.
 */
#ifndef SIM_TEST_WALLET_H
#define SIM_TEST_WALLET_H

/**
 * If CYPHEROCK_SIM_TEST_MNEMONIC is set, make sure the simulator's flash holds
 * a wallet named "SIMTEST" whose id matches that mnemonic. Does nothing when
 * the variable is unset. Pairs with the shortcut in reconstruct_seed().
 */
void sim_test_wallet_load(void);

#endif /* SIM_TEST_WALLET_H */
