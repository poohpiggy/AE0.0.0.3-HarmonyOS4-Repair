#include "ae_calls.h"
#include <string.h>

struct reader { const uint8_t *p; size_t remaining; };
static int integer(struct reader *r, uint32_t *out) {
    if (r->remaining < 4) return -1;
    const uint8_t *p=r->p;
    *out=(uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
    r->p+=4; r->remaining-=4; return 0;
}
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
/* Convert the NUL-terminated Parcel String16 into UTF-8. */
static int string16(struct reader *r, char *storage, char **value) {
    uint32_t n;
    if (integer(r,&n)) return -1;
    if (n==UINT32_MAX) { *value=NULL; return 0; }
    if (n>AE_CALL_STRING_MAX) return -1;
    size_t bytes=((size_t)n+1)*2, padded=(bytes+3)&~(size_t)3;
    if (r->remaining<padded || u16(r->p+2*n)!=0) return -1;
    size_t w=0;
    for (size_t i=0;i<n;i++) {
        uint32_t c=u16(r->p+2*i);
        if (!c || (c>=0xdc00 && c<=0xdfff)) return -1;
        if (c>=0xd800 && c<=0xdbff) {
            if (++i>=n) return -1;
            uint32_t d=u16(r->p+2*i);
            if (d<0xdc00 || d>0xdfff) return -1;
            c=0x10000+((c-0xd800)<<10)+(d-0xdc00);
        }
        size_t k=c<0x80?1:c<0x800?2:c<0x10000?3:4;
        if (w+k>AE_CALL_STRING_MAX) return -1;
        if (k==1) storage[w++]=(char)c;
        else {
            storage[w++]=(char)((k==2?0xc0:k==3?0xe0:0xf0)|(c>>(6*(k-1))));
            for (size_t j=k-1;j>0;j--) storage[w++]=(char)(0x80|((c>>(6*(j-1)))&0x3f));
        }
    }
    storage[w]=0; *value=storage;
    r->p+=padded; r->remaining-=padded; return 0;
}
int ae_decode_calls(const uint8_t *payload, size_t len, struct ae_calls *out) {
    if (!payload || !out) return -1;
    memset(out,0,sizeof *out);
    struct reader r={payload,len};
    uint32_t n,v;
    if (integer(&r,&n) || n>1) return -1;
    if (!n) return 0;
    if (integer(&r,&v) || v>5) return -1;
    out->call.state=(int)v;
    if (integer(&r,&v) || v==0 || v>INT32_MAX) return -1;
    out->call.index=(int)v;
    if (integer(&r,&v) || v>INT32_MAX) return -1;
    out->call.toa=(int)v;
    if (integer(&r,&v)) return -1; out->call.isMpty=(char)(v!=0);
    if (integer(&r,&v)) return -1; out->call.isMT=(char)(v!=0);
    if (integer(&r,&v) || v>127) return -1; out->call.als=(char)v;
    if (integer(&r,&v)) return -1; out->call.isVoice=(char)(v!=0);
    if (integer(&r,&v)) return -1; out->call.isVoicePrivacy=(char)(v!=0);
    if (string16(&r,out->number,&out->call.number)) return -1;
    if (integer(&r,&v) || v>3) return -1; out->call.numberPresentation=(int)v;
    if (string16(&r,out->name,&out->call.name)) return -1;
    if (integer(&r,&v) || v>3) return -1; out->call.namePresentation=(int)v;
    if (integer(&r,&v) || v!=0) return -1;
    out->pointers[0]=&out->call;
    out->count=1;
    return 0;
}
