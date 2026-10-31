#include "ae_event.h"

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int is_hex(uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

int ae_decode_event(const uint8_t *payload, size_t len, struct ae_event *event) {
    if (!payload || !event) return -1;
    event->type = AE_EVENT_IGNORED;
    event->sms_pdu[0] = '\0';
    if (len < 8) return -1;
    if (read_le32(payload) != 1) return 0;
    switch (read_le32(payload + 4)) {
    case 1001: event->type = AE_EVENT_CALL_CHANGED; return 0;
    case 1018: event->type = AE_EVENT_CALL_RING; return 0;
    case 1003: break;
    default: return 0;
    }
    if (len < 12) return -1;
    uint32_t count = read_le32(payload + 8);
    if (!count || count > AE_MAX_SMS_PDU_HEX || (count & 1)) return -1;
    size_t bytes = ((size_t)count + 1) * 2;
    size_t padded = (bytes + 3) & ~(size_t)3;
    if (len - 12 < padded) return -1;
    for (size_t i = 0; i < count; ++i) {
        if (payload[12 + 2*i + 1] || !is_hex(payload[12 + 2*i])) return -1;
    }
    if (payload[12 + 2*count] || payload[12 + 2*count + 1]) return -1;
    for (size_t i = 0; i < count; ++i) event->sms_pdu[i] = (char)payload[12 + 2*i];
    event->sms_pdu[count] = '\0';
    event->type = AE_EVENT_SMS;
    return 0;
}
