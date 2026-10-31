#include "ae_sim.h"
#include <string.h>

struct reader { const uint8_t *p; size_t n; };
static int word(struct reader *r, uint32_t *v) {
    if(r->n<4) return -1;
    *v=(uint32_t)r->p[0]|(uint32_t)r->p[1]<<8|(uint32_t)r->p[2]<<16|(uint32_t)r->p[3]<<24;
    r->p+=4; r->n-=4; return 0;
}
/* AE currently sends null for SIM labels and hex ASCII for SIM IO. Reject
 * unexpected non-ASCII instead of silently corrupting a Parcel string. */
static int string(struct reader *r, char *buf, char **value) {
    uint32_t n;
    if(word(r,&n)) return -1;
    if(n==UINT32_MAX) { *value=NULL; return 0; }
    if(n>AE_SIM_STRING_MAX) return -1;
    size_t bytes=((size_t)n+1)*2, padded=(bytes+3)&~(size_t)3;
    if(r->n<padded || r->p[2*n] || r->p[2*n+1]) return -1;
    for(size_t i=0;i<n;i++) {
        if(!r->p[2*i] || r->p[2*i]>127 || r->p[2*i+1]) return -1;
        buf[i]=(char)r->p[2*i];
    }
    buf[n]=0; *value=buf; r->p+=padded; r->n-=padded; return 0;
}
static int integer(struct reader *r, int *out) { uint32_t v; if(word(r,&v)) return -1; *out=(int32_t)v; return 0; }
int ae_decode_sim_status(const uint8_t *p, size_t n, struct ae_sim_status *out) {
    if(!p || !out) return -1;
    memset(out,0,sizeof *out); struct reader r={p,n}; RIL_CardStatus_v6 *c=&out->card;
    if(integer(&r,&c->card_state)||integer(&r,&c->universal_pin_state)||
       integer(&r,&c->gsm_umts_subscription_app_index)||integer(&r,&c->cdma_subscription_app_index)||
       integer(&r,&c->ims_subscription_app_index)||integer(&r,&c->num_applications)) return -1;
    if(c->card_state<0 || c->card_state>2 || c->num_applications<0 || c->num_applications>RIL_CARD_MAX_APPS) return -1;
    int indexes[]={c->gsm_umts_subscription_app_index,c->cdma_subscription_app_index,c->ims_subscription_app_index};
    for(size_t i=0;i<3;i++) if(indexes[i]<-1 || indexes[i]>=c->num_applications) return -1;
    for(int i=0;i<c->num_applications;i++) {
        RIL_AppStatus *a=&c->applications[i];
        if(integer(&r,&a->app_type)||integer(&r,&a->app_state)||integer(&r,&a->perso_substate)||
           string(&r,out->aid[i],&a->aid_ptr)||string(&r,out->label[i],&a->app_label_ptr)||
           integer(&r,&a->pin1_replaced)||integer(&r,&a->pin1)||integer(&r,&a->pin2)) return -1;
    }
    return 0;
}
int ae_decode_sim_io(const uint8_t *p,size_t n,struct ae_sim_io_result *out) {
    if(!p || !out) return -1;
    memset(out,0,sizeof *out); struct reader r={p,n};
    if(integer(&r,&out->response.sw1)||integer(&r,&out->response.sw2)||
       string(&r,out->storage,&out->response.simResponse)) return -1;
    if(out->response.sw1<0 || out->response.sw1>255 || out->response.sw2<0 || out->response.sw2>255) return -1;
    if(out->response.simResponse) for(const char *s=out->response.simResponse;*s;s++)
        if(!((*s>='0'&&*s<='9')||(*s>='a'&&*s<='f')||(*s>='A'&&*s<='F'))) return -1;
    return 0;
}
struct writer { uint8_t *p; size_t n, used; };
static int put(struct writer *w,uint32_t v) {
    if(w->n<4) return -1;
    for(int i=0;i<4;i++) w->p[i]=(uint8_t)(v>>(8*i));
    w->p+=4; w->n-=4; w->used+=4; return 0;
}
static int put_string(struct writer *w,const char *s) {
    if(!s) return put(w,UINT32_MAX);
    size_t n=0; while(n<=AE_SIM_STRING_MAX && s[n]) { if((unsigned char)s[n]>127) return -1; n++; }
    if(n>AE_SIM_STRING_MAX || put(w,(uint32_t)n)) return -1;
    size_t bytes=(n+1)*2, padded=(bytes+3)&~(size_t)3;
    if(w->n<padded) return -1;
    memset(w->p,0,padded); for(size_t i=0;i<n;i++) w->p[2*i]=(uint8_t)s[i];
    w->p+=padded; w->n-=padded; w->used+=padded; return 0;
}
int ae_encode_sim_io(const AE_RIL_SIM_IO_v6 *in,uint8_t *p,size_t cap,size_t *used) {
    if(!in || !p || !used) return -1;
    *used=0; struct writer w={p,cap,0};
    if(put(&w,(uint32_t)in->command)||put(&w,(uint32_t)in->fileid)||put_string(&w,in->path)||
       put(&w,(uint32_t)in->p1)||put(&w,(uint32_t)in->p2)||put(&w,(uint32_t)in->p3)||
       put_string(&w,in->data)||put_string(&w,in->pin2)||put_string(&w,in->aidPtr)) return -1;
    *used=w.used; return 0;
}
