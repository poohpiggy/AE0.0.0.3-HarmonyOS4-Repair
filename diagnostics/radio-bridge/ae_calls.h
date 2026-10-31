#ifndef CODEX_AE_CALLS_H
#define CODEX_AE_CALLS_H
#include <stddef.h>
#include <stdint.h>
/* Layout of AOSP RIL_Call (ril.h v12); pointer fields are borrowed from
 * this result and stay valid until it is modified or goes out of scope. */
typedef struct {
    int state, index, toa;
    char isMpty, isMT, als, isVoice, isVoicePrivacy;
    char *number;
    int numberPresentation;
    char *name;
    int namePresentation;
    void *uusInfo;
} AE_RIL_Call;
#define AE_CALL_STRING_MAX 1024
struct ae_calls {
    size_t count;
    AE_RIL_Call call;
    AE_RIL_Call *pointers[1];
    char number[AE_CALL_STRING_MAX + 1];
    char name[AE_CALL_STRING_MAX + 1];
};
/* Payload begins after the solicited type, serial and error. RilStub emits
 * zero or one call; unknown UUS and extra calls are rejected. */
int ae_decode_calls(const uint8_t *payload, size_t len, struct ae_calls *out);
#endif
