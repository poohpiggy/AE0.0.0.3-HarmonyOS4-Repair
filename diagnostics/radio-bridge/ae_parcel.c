#include "ae_parcel.h"
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
    if (n>AE_PARCEL_STRING_MAX) return -1;
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
        if (w+k>AE_PARCEL_STRING_MAX) return -1;
        if (k==1) storage[w++]=(char)c;
        else {
            storage[w++]=(char)((k==2?0xc0:k==3?0xe0:0xf0)|(c>>(6*(k-1))));
            for (size_t j=k-1;j>0;j--) storage[w++]=(char)(0x80|((c>>(6*(j-1)))&0x3f));
        }
    }
    storage[w]=0; *value=storage;
    r->p+=padded; r->remaining-=padded; return 0;
}
int ae_decode_string(const uint8_t *payload, size_t len, struct ae_strings *out) {
    if (!payload || !out) return -1;
    memset(out,0,sizeof *out);
    struct reader r={payload,len};
    if (string16(&r,out->storage[0],&out->values[0]) || r.remaining) return -1;
    out->count=1; return 0;
}
int ae_decode_string_list(const uint8_t *payload, size_t len, size_t expected, size_t pad_to, struct ae_strings *out) {
    if (!payload || !out || expected>pad_to || pad_to>AE_PARCEL_MAX_STRINGS) return -1;
    memset(out,0,sizeof *out);
    struct reader r={payload,len}; uint32_t count;
    if (integer(&r,&count) || count!=expected) return -1;
    for (size_t i=0;i<expected;i++) if (string16(&r,out->storage[i],&out->values[i])) return -1;
    if (r.remaining) return -1;
    out->count=pad_to; return 0;
}
int ae_decode_int(const uint8_t *payload, size_t len, int32_t *out) {
    if (!payload || !out) return -1;
    struct reader r={payload,len}; uint32_t count, value;
    if (integer(&r,&count) || count!=1 || integer(&r,&value) || r.remaining) return -1;
    memcpy(out,&value,sizeof value); return 0;
}
