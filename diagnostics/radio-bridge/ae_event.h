#ifndef CODEX_AE_EVENT_H
#define CODEX_AE_EVENT_H
#include <stddef.h>
#include <stdint.h>

/* Decode one AE RilStub frame payload, excluding its four-byte big-endian
 * length. SMS PDU is valid only until the next call using the same output. */
enum ae_event_type { AE_EVENT_IGNORED, AE_EVENT_CALL_CHANGED, AE_EVENT_CALL_RING, AE_EVENT_SMS };
#define AE_MAX_SMS_PDU_HEX 1024
struct ae_event { enum ae_event_type type; char sms_pdu[AE_MAX_SMS_PDU_HEX + 1]; };
/* Returns 0 for valid/ignored, -1 for malformed input. */
int ae_decode_event(const uint8_t *payload, size_t len, struct ae_event *event);
#endif
