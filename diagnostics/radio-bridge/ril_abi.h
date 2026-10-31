#ifndef CODEX_RIL_ABI_H
#define CODEX_RIL_ABI_H

/* Minimal ABI subset from Android 8.1's telephony/ril.h (RIL_VERSION 12).
 * Its C ABI is shared by single- and multi-SIM callers on AArch64: callers
 * may pass an extra socket id; these callbacks deliberately ignore it. */
#include <stddef.h>

enum { RIL_VERSION = 12, RIL_CARD_MAX_APPS = 8 };
enum { RIL_E_SUCCESS = 0, RIL_E_REQUEST_NOT_SUPPORTED = 6,
       RIL_E_INVALID_ARGUMENTS = 44 };
enum { RADIO_STATE_OFF = 0, RADIO_STATE_ON = 10 };
enum { RIL_REQUEST_GET_SIM_STATUS = 1, RIL_REQUEST_RADIO_POWER = 23,
       RIL_REQUEST_BASEBAND_VERSION = 51 };
enum { RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED = 1000 };
typedef void *RIL_Token;
typedef struct {
    int app_type, app_state, perso_substate;
    char *aid_ptr, *app_label_ptr;
    int pin1_replaced, pin1, pin2;
} RIL_AppStatus;
typedef struct {
    int card_state, universal_pin_state;
    int gsm_umts_subscription_app_index, cdma_subscription_app_index;
    int ims_subscription_app_index, num_applications;
    RIL_AppStatus applications[RIL_CARD_MAX_APPS];
} RIL_CardStatus_v6;
typedef struct {
    int version;
    void (*onRequest)(int, void *, size_t, RIL_Token);
    int (*onStateRequest)(void);
    int (*supports)(int);
    void (*onCancel)(RIL_Token);
    const char *(*getVersion)(void);
} RIL_RadioFunctions;
struct RIL_Env {
    void (*OnRequestComplete)(RIL_Token, int, void *, size_t);
    /* Fourth socket argument is supplied for multi-SIM and ignored for single-SIM. */
    void (*OnUnsolicitedResponse)(int, const void *, size_t, int);
    void (*RequestTimedCallback)(void (*)(void *), void *, const void *);
    void (*OnRequestAck)(RIL_Token);
};
const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *, int, char **);
#endif
