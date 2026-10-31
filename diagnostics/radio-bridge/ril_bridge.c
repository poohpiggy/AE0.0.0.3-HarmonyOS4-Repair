#include "ril_abi.h"
#include "ae_event.h"
#include "ae_socket.h"
#include "ae_calls.h"
#include "ae_sim.h"
#include "ae_parcel.h"
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* Experimental single-slot bridge. Only requests explicitly translated below
 * are supported; a complete, usable telephony stack requires more requests. */
#define MAX_PENDING 64
#define FRAME_CAP (1024u * 1024u)
#ifndef AE_REQUEST_TIMEOUT
#define AE_REQUEST_TIMEOUT 15
#endif
#ifndef AE_RECONNECT_DELAY
#define AE_RECONNECT_DELAY 15
#endif
#if defined(AE_ENABLE_SIGNAL_POLL) && !defined(AE_SIGNAL_POLL_INTERVAL)
#define AE_SIGNAL_POLL_INTERVAL 20
#endif
#define MAX_QUEUED_SMS 256
struct pending { uint32_t serial; RIL_Token token; int request; time_t deadline; int internal; };
static struct pending pending[MAX_PENDING];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static const struct RIL_Env *ril_env;
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)] = "/dev/socket/rild";
static int sock = -1, radio_state = 1;
static uint32_t next_serial = 1;
struct sms_node { struct sms_node *next; char pdu[AE_MAX_SMS_PDU_HEX+1]; };
static struct sms_node *sms_head, *sms_tail;
static int sms_inflight, framework_ready;
static size_t sms_queued;
#ifdef AE_ENABLE_SIGNAL_POLL
static time_t next_signal_poll;
/* An internal poll must never consume a framework request slot: an ACK is
 * allowed even when 63 ordinary requests and a poll are in flight. */
static struct pending signal_pending;
#endif

static void deliver_next_sms(void) {
    struct sms_node *node=NULL;
    pthread_mutex_lock(&mutex);
    if (framework_ready && !sms_inflight && sms_head) {
        node=sms_head; sms_head=node->next;
        --sms_queued;
        if (!sms_head) sms_tail=NULL;
        sms_inflight=1;
    }
    pthread_mutex_unlock(&mutex);
    if (node) { ril_env->OnUnsolicitedResponse(1003,node->pdu,strlen(node->pdu),0); free(node); }
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static time_t monotonic_seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec; }
static int supports(int req) {
    switch(req) { case 1: case 9: case 11: case 12: case 13: case 14: case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 28: case 37: case 38: case 39: case 40: case 45: case 51: case 74: case 98: case 100: case 108: return 1; default: return 0; }
}
static void request(int req, void *data, size_t len, RIL_Token token) {
    if (!supports(req)) { ril_env->OnRequestComplete(token, 6, NULL, 0); return; }
    size_t ints = req == 12 || req == 23 ? 1 : req == 37 ? 2 : 0;
    if (req==28 ? (!data || len!=sizeof(AE_RIL_SIM_IO_v6)) :
        req==11 ? (!data || len!=sizeof(char *)) : (len != ints*sizeof(int) || (ints && !data))) {
        ril_env->OnRequestComplete(token, 44, NULL, 0); return;
    }
    /* An incoming radio request indicates that the HIDL framework installed
     * callbacks at least once. This does not detect later framework restarts. */
    pthread_mutex_lock(&mutex); framework_ready=1; pthread_mutex_unlock(&mutex);
    deliver_next_sms();
    uint8_t out[16384]; size_t outlen=8; put32(out,(uint32_t)req);
    if (req==28) { size_t written=0;
        if (ae_encode_sim_io(data,out+8,sizeof out-8,&written)<0) { ril_env->OnRequestComplete(token,44,NULL,0); return; }
        outlen+=written;
    }
    if (ints) { put32(out+outlen,(uint32_t)ints); outlen+=4;
        for (size_t i=0; i<ints; ++i) { int v; memcpy(&v,(char *)data+i*sizeof(int),sizeof v); put32(out+outlen,(uint32_t)v); outlen+=4; }
    }
    pthread_mutex_lock(&mutex);
    size_t i; for (i=0; i<MAX_PENDING && pending[i].serial; ++i) {}
    if (sock<0 || i==MAX_PENDING) {
        if (req==37 && sock>=0) shutdown(sock,SHUT_RDWR);
        pthread_mutex_unlock(&mutex); ril_env->OnRequestComplete(token,1,NULL,0); return;
    }
    uint32_t serial = next_serial++; if (!serial) serial = next_serial++;
    pending[i]=(struct pending){serial,token,req,monotonic_seconds()+AE_REQUEST_TIMEOUT,0}; put32(out+4,serial);
    int ok=ae_write_frame(sock,out,outlen);
    if (ok<0) shutdown(sock,SHUT_RDWR);
    pthread_mutex_unlock(&mutex);
}
static int state_request(void) { pthread_mutex_lock(&mutex); int s=radio_state; pthread_mutex_unlock(&mutex); return s; }
static void cancel(RIL_Token t) { (void)t; }
static const char *version(void) { return "AE experimental RIL bridge"; }
static const RIL_RadioFunctions functions={RIL_VERSION,request,state_request,supports,cancel,version};

static void response(const uint8_t *p, size_t n) {
    if (n<12) return;
    uint32_t serial=get32(p+4); struct pending found={0};
    pthread_mutex_lock(&mutex);
    for (size_t i=0; i<MAX_PENDING; ++i) if (pending[i].serial==serial) { found=pending[i]; pending[i].serial=0; break; }
#ifdef AE_ENABLE_SIGNAL_POLL
    if (!found.serial && serial && signal_pending.serial==serial) {
        found=signal_pending; signal_pending.serial=0;
    }
#endif
    pthread_mutex_unlock(&mutex);
    if (!found.serial) return;
    int err=(int)get32(p+8);
    if (!err && (found.request==11 || found.request==20 || found.request==21 || found.request==22 || found.request==38 || found.request==39 || found.request==51 || found.request==98 || found.request==100)) {
        struct ae_strings strings;
        int parsed;
        int single=found.request==11 || found.request==38 || found.request==39 || found.request==51 || found.request==100;
        if (single) parsed=ae_decode_string(p+12,n-12,&strings);
        else if (found.request==20) parsed=ae_decode_string_list(p+12,n-12,4,15,&strings);
        else if (found.request==21) parsed=ae_decode_string_list(p+12,n-12,6,6,&strings);
        else if (found.request==98) parsed=ae_decode_string_list(p+12,n-12,4,4,&strings);
        else parsed=ae_decode_string_list(p+12,n-12,3,3,&strings);
        if (parsed<0 || (single && !strings.values[0])) { ril_env->OnRequestComplete(found.token,2,NULL,0); return; }
        if (single) ril_env->OnRequestComplete(found.token,0,strings.values[0],strlen(strings.values[0]));
        else ril_env->OnRequestComplete(found.token,0,strings.values,strings.count*sizeof(strings.values[0]));
        return;
    }
    if (!err && (found.request==108 || found.request==18 || found.request==45 || found.request==74)) {
        int32_t value;
        if (ae_decode_int(p+12,n-12,&value)<0) { ril_env->OnRequestComplete(found.token,2,NULL,0); return; }
        ril_env->OnRequestComplete(found.token,0,&value,sizeof value); return;
    }
    if (!err && found.request==19) {
        /* AE supplies the 13 legacy GW/CDMA/EVDO/LTE ints; AOSP's
         * RIL_SignalStrength_v10 appends a TD-SCDMA rscp int. */
        int32_t signal[14];
        if (n!=12+4+13*4 || get32(p+12)!=13) { if (!found.internal) ril_env->OnRequestComplete(found.token,2,NULL,0); return; }
        for(size_t i=0;i<13;i++){ uint32_t value=get32(p+16+i*4); memcpy(&signal[i],&value,sizeof value); }
        /* Unlike RSRP/RSRQ/CQI, libril does not normalize these two fields. */
        if (signal[10]==-1) signal[10]=INT_MAX;
        if (signal[12]==-1) signal[12]=INT_MAX;
        signal[13]=INT_MAX;
        if (found.internal) {
            pthread_mutex_lock(&mutex); int ready=radio_state==10 && sock>=0; pthread_mutex_unlock(&mutex);
            if (ready) ril_env->OnUnsolicitedResponse(1009,signal,sizeof signal,0);
        } else ril_env->OnRequestComplete(found.token,0,signal,sizeof signal);
        return;
    }
    if (!err && found.request==1) {
        struct ae_sim_status sim;
        if (ae_decode_sim_status(p+12,n-12,&sim)<0) { ril_env->OnRequestComplete(found.token,2,NULL,0); return; }
        ril_env->OnRequestComplete(found.token,0,&sim.card,sizeof sim.card); return;
    }
    if (!err && found.request==28) {
        struct ae_sim_io_result io;
        if (ae_decode_sim_io(p+12,n-12,&io)<0) { ril_env->OnRequestComplete(found.token,2,NULL,0); return; }
        ril_env->OnRequestComplete(found.token,0,&io.response,sizeof io.response); return;
    }
    if (!err && found.request==9) {
        struct ae_calls calls;
        if (ae_decode_calls(p+12,n-12,&calls)<0) { ril_env->OnRequestComplete(found.token,2,NULL,0); return; }
        ril_env->OnRequestComplete(found.token,0,calls.count ? calls.pointers : NULL,
                                   calls.count * sizeof calls.pointers[0]);
        return;
    }
    if (!found.internal) ril_env->OnRequestComplete(found.token,err,NULL,0);
    if (found.request==37 && !err) {
        pthread_mutex_lock(&mutex); sms_inflight=0; pthread_mutex_unlock(&mutex);
        deliver_next_sms();
    } else if (found.request==37) {
        pthread_mutex_lock(&mutex); if (sock>=0) shutdown(sock,SHUT_RDWR); pthread_mutex_unlock(&mutex);
    }
}
static void indication(const uint8_t *p, size_t n) {
    if (n<8) return;
    uint32_t code=get32(p+4);
    if (code==1000 && n>=12) {
        int state=(int)get32(p+8);
        if (state!=0 && state!=1 && state!=10) return;
        pthread_mutex_lock(&mutex); radio_state=state;
#ifdef AE_ENABLE_SIGNAL_POLL
        if (state!=10) {
            signal_pending.serial=0;
            next_signal_poll=0;
        }
#endif
        pthread_mutex_unlock(&mutex);
        ril_env->OnUnsolicitedResponse(1000,NULL,0,0); return;
    }
    if (code==1002 || code==1019) { ril_env->OnUnsolicitedResponse((int)code,NULL,0,0); return; }
    struct ae_event event;
    if (ae_decode_event(p,n,&event)<0) return;
    if (event.type==AE_EVENT_CALL_CHANGED) ril_env->OnUnsolicitedResponse(1001,NULL,0,0);
    else if (event.type==AE_EVENT_CALL_RING) ril_env->OnUnsolicitedResponse(1018,NULL,0,0);
    else if (event.type==AE_EVENT_SMS) {
        struct sms_node *node=malloc(sizeof *node);
        if (!node) { fprintf(stderr,"AE RIL: cannot queue SMS\n"); return; }
        node->next=NULL; strcpy(node->pdu,event.sms_pdu);
        pthread_mutex_lock(&mutex);
        if (sms_queued==MAX_QUEUED_SMS) { pthread_mutex_unlock(&mutex); free(node); fprintf(stderr,"AE RIL: SMS queue full\n"); return; }
        if (sms_tail) sms_tail->next=node; else sms_head=node;
        sms_tail=node; ++sms_queued;
        pthread_mutex_unlock(&mutex);
        deliver_next_sms();
    }
}
static void disconnected(int fd) {
    struct pending failed[MAX_PENDING]; size_t count=0;
    struct sms_node *abandoned;
    pthread_mutex_lock(&mutex);
    if (sock==fd) { sock=-1; radio_state=1; }
#ifdef AE_ENABLE_SIGNAL_POLL
    next_signal_poll=0;
    signal_pending.serial=0;
#endif
    abandoned=sms_head; sms_head=sms_tail=NULL; sms_queued=0; sms_inflight=0;
    for (size_t i=0; i<MAX_PENDING; ++i) if (pending[i].serial) { failed[count++]=pending[i]; pending[i].serial=0; }
    pthread_mutex_unlock(&mutex); close(fd);
    if (abandoned) fprintf(stderr,"AE RIL: dropping queued SMS after disconnect\n");
    while (abandoned) { struct sms_node *next=abandoned->next; free(abandoned); abandoned=next; }
    for (size_t i=0; i<count; ++i) if (!failed[i].internal) ril_env->OnRequestComplete(failed[i].token,1,NULL,0);
    ril_env->OnUnsolicitedResponse(1000,NULL,0,0);
}
static void *reader(void *unused) {
    (void)unused; uint8_t *buf=malloc(FRAME_CAP); if (!buf) return NULL;
    /* RIL_Init returns before rild registers the callbacks with libril.
     * Give registration time to finish before consuming initial indications. */
    sleep(1);
    for (;;) {
        int fd=socket(AF_UNIX,SOCK_STREAM,0);
        if (fd<0) { sleep(AE_RECONNECT_DELAY); continue; }
        struct timeval send_timeout={.tv_sec=2,.tv_usec=0};
        if (setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&send_timeout,sizeof send_timeout)<0) { close(fd); sleep(AE_RECONNECT_DELAY); continue; }
        struct sockaddr_un addr={.sun_family=AF_UNIX}; strcpy(addr.sun_path,socket_path);
        if (connect(fd,(struct sockaddr *)&addr,sizeof addr)<0) { close(fd); sleep(AE_RECONNECT_DELAY); continue; }
        pthread_mutex_lock(&mutex); sock=fd; pthread_mutex_unlock(&mutex);
        size_t n;
        while (ae_read_frame(fd,buf,FRAME_CAP,&n)==0) {
            if (n>=4 && get32(buf)==0) response(buf,n);
            else if (n>=4 && get32(buf)==1) indication(buf,n);
        }
        /* AE enters safe mode after >5 accepted connections in 60 seconds. */
        disconnected(fd); sleep(AE_RECONNECT_DELAY);
    }
}
static void *watch_timeouts(void *unused) {
    (void)unused;
    for (;;) {
        sleep(1); time_t now=monotonic_seconds();
        struct pending expired[MAX_PENDING]; size_t count=0;
        pthread_mutex_lock(&mutex);
        for (size_t i=0;i<MAX_PENDING;i++) if (pending[i].serial && now>=pending[i].deadline) {
            if (pending[i].request==37 && sock>=0) shutdown(sock,SHUT_RDWR);
            expired[count++]=pending[i]; pending[i].serial=0;
        }
#ifdef AE_ENABLE_SIGNAL_POLL
        if (signal_pending.serial && now>=signal_pending.deadline) signal_pending.serial=0;
        if (framework_ready && radio_state==10 && sock>=0 && now>=next_signal_poll) {
            if (!signal_pending.serial) {
                /* A single nonblocking frame send cannot stall the shared
                 * lock while the peer's receive buffer is full. */
                uint8_t out[12]={0,0,0,8}; put32(out+4,19);
                uint32_t serial=next_serial++; if (!serial) serial=next_serial++;
                put32(out+8,serial);
                ssize_t sent=send(sock,out,sizeof out,MSG_DONTWAIT|MSG_NOSIGNAL);
                if (sent==(ssize_t)sizeof out) {
                    signal_pending=(struct pending){serial,NULL,19,now+AE_REQUEST_TIMEOUT,1};
                    next_signal_poll=now+AE_SIGNAL_POLL_INTERVAL;
                } else if (sent<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)) {
                    next_signal_poll=now+1;
                } else {
                    /* A short frame cannot be retried without corrupting the
                     * stream. Force the normal disconnect/recovery path. */
                    shutdown(sock,SHUT_RDWR);
                    next_signal_poll=now+AE_SIGNAL_POLL_INTERVAL;
                }
            }
        }
#endif
        pthread_mutex_unlock(&mutex);
        for (size_t i=0;i<count;i++) if (!expired[i].internal) ril_env->OnRequestComplete(expired[i].token,1,NULL,0);
    }
}
const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc, char **argv) {
    if (!env || !env->OnRequestComplete || !env->OnUnsolicitedResponse) return NULL;
    if (argc==3 && argv && strcmp(argv[1],"--socket")==0) {
        if (strlen(argv[2])>=sizeof socket_path) return NULL;
        strcpy(socket_path,argv[2]);
    }
    ril_env=env; pthread_t thread, timeout_thread;
    if (pthread_create(&timeout_thread,NULL,watch_timeouts,NULL)!=0) return NULL;
    pthread_detach(timeout_thread);
    if (pthread_create(&thread,NULL,reader,NULL)!=0) return NULL;
    pthread_detach(thread); return &functions;
}
