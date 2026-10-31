#ifndef CODEX_AE_PARCEL_H
#define CODEX_AE_PARCEL_H
#include <stddef.h>
#include <stdint.h>
/* Output buffers own all strings/pointers. Do not move or copy output after
 * decoding; keep it alive until the RIL callback returns. */
#define AE_PARCEL_MAX_STRINGS 16
#define AE_PARCEL_STRING_MAX 1024
struct ae_strings {
    size_t count;
    char *values[AE_PARCEL_MAX_STRINGS];
    char storage[AE_PARCEL_MAX_STRINGS][AE_PARCEL_STRING_MAX+1];
};
int ae_decode_string(const uint8_t *, size_t, struct ae_strings *);
/* Require exactly expected input strings; append NULLs up to pad_to. */
int ae_decode_string_list(const uint8_t *, size_t, size_t expected, size_t pad_to, struct ae_strings *);
/* A Parcel int array, expected to contain exactly one element. */
int ae_decode_int(const uint8_t *, size_t, int32_t *);
#endif
