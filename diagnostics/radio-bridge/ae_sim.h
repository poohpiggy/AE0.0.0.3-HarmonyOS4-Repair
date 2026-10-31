#ifndef CODEX_AE_SIM_H
#define CODEX_AE_SIM_H
#include "ril_abi.h"
#include <stddef.h>
#include <stdint.h>

/* AOSP ril.h v12 ABI layouts. Strings in decoded responses are borrowed from
 * their containing result, which must remain live during OnRequestComplete. */
typedef struct {
    int command, fileid;
    char *path;
    int p1, p2, p3;
    char *data, *pin2, *aidPtr;
} AE_RIL_SIM_IO_v6;
typedef struct { int sw1, sw2; char *simResponse; } AE_RIL_SIM_IO_Response;
#define AE_SIM_STRING_MAX 1024
struct ae_sim_status {
    RIL_CardStatus_v6 card;
    char aid[RIL_CARD_MAX_APPS][AE_SIM_STRING_MAX+1];
    char label[RIL_CARD_MAX_APPS][AE_SIM_STRING_MAX+1];
};
struct ae_sim_io_result { AE_RIL_SIM_IO_Response response; char storage[AE_SIM_STRING_MAX+1]; };

/* Response input excludes the type, serial and error words. Encoding writes
 * only the request arguments; caller supplies request ID and serial. */
int ae_decode_sim_status(const uint8_t *p, size_t n, struct ae_sim_status *out);
int ae_decode_sim_io(const uint8_t *p, size_t n, struct ae_sim_io_result *out);
int ae_encode_sim_io(const AE_RIL_SIM_IO_v6 *in, uint8_t *p, size_t cap, size_t *used);
#endif
