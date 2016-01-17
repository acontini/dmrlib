#ifndef _DMR_FEC_HAMMING_H
#define _DMR_FEC_HAMMING_H

#include <inttypes.h>
#include <stdbool.h>

extern void dmr_hamming_13_9_3_encode_bits(bool bits[9], bool parity[4]);
extern bool dmr_hamming_13_9_3_verify_bits(bool bits[13], bool parity[4]);
extern void dmr_hamming_15_11_3_encode_bits(bool bits[11], bool parity[4]);
extern bool dmr_hamming_15_11_3_verify_bits(bool bits[15], bool parity[4]);
extern void dmr_hamming_16_11_4_encode_bits(bool bits[11], bool parity[5]);
extern bool dmr_hamming_16_11_4_verify_bits(bool bits[16], bool parity[5]);
extern void dmr_hamming_17_12_3_encode_bits(bool bits[12], bool parity[5]);
extern bool dmr_hamming_17_12_3_verify_bits(bool bits[17], bool parity[5]);
extern uint8_t dmr_hamming_parity_position(bool *bits, uint8_t len);

#endif // _DMR_FEC_HAMMING_H
