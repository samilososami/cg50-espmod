#ifndef CASIOESP_WIRE_H
#define CASIOESP_WIRE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* One bounded frame at a time, resynchronizing on '@'. SSIDs are hex encoded.
 * The checksum detects damage; request IDs and row indices reject stale data. */
#define WIRE_CAP 240
typedef struct { char data[WIRE_CAP]; unsigned used; int active; } wire_rx;

static uint32_t wire_hash(const char *s, size_t n)
{
    uint32_t h = 2166136261u;
    while(n--) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}
static int wire_hex(char c)
{
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static int wire_pack(char *out, const char *payload)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = strlen(payload);
    uint32_t h;
    unsigned i;
    if(n + 15 > WIRE_CAP) return 0;
    h = wire_hash(payload, n);
    memcpy(out, "\n@@@", 4); /* Resync even if the first wake-up byte is lost. */
    memcpy(out + 4, payload, n);
    out[n + 4] = '*';
    for(i = 0; i < 8; i++) out[n + 5 + i] = hex[(h >> (28 - 4*i)) & 15];
    out[n + 13] = '\n'; out[n + 14] = 0;
    return (int)n + 14;
}
/* 1: complete validated payload in rx->data; -1: damaged; 0: incomplete. */
static int wire_feed(wire_rx *rx, unsigned char c)
{
    unsigned n, i;
    uint32_t h = 0;
    if(c == '@') { rx->active = 1; rx->used = 0; return 0; }
    if(!rx->active || c == '\r') return 0;
    if(c == '\n') {
        rx->active = 0;
        n = rx->used;
        if(n < 9 || rx->data[n-9] != '*') return -1;
        for(i = n-8; i < n; i++) {
            int v = wire_hex(rx->data[i]);
            if(v < 0) return -1;
            h = (h << 4) | (unsigned)v;
        }
        if(h != wire_hash(rx->data, n-9)) return -1;
        rx->data[n-9] = 0;
        return 1;
    }
    if(c < 32 || c > 126 || rx->used >= WIRE_CAP-1) {
        rx->active = 0; return -1;
    }
    rx->data[rx->used++] = (char)c;
    /* The checksum already fixes the end; a lost newline must not lose a reply. */
    if(rx->used >= 9 && rx->data[rx->used-9] == '*') return wire_feed(rx, '\n');
    return 0;
}
static inline int wire_split(char *text, char **fields, int capacity)
{
    int n=1; fields[0]=text;
    for(; *text; text++) if(*text==':') {
        if(n==capacity) return 0;
        *text=0; fields[n++]=text+1;
    }
    return n;
}
static inline int wire_number(const char *text, uint32_t *out)
{
    uint32_t n=0;
    if(!*text) return 0;
    while(*text) {
        unsigned d=(unsigned char)*text++-'0';
        if(d>9 || n>(UINT32_MAX-d)/10) return 0;
        n=n*10+d;
    }
    *out=n; return 1;
}
static inline void wire_encode(char *out,const char *text)
{
    const char *hex="0123456789ABCDEF";
    while(*text) { unsigned c=(unsigned char)*text++; *out++=hex[c>>4]; *out++=hex[c&15]; }
    *out=0;
}
static inline int wire_decode(char *out,int cap,const char *hex)
{
    int n=0;
    while(*hex) {
        int a=wire_hex(*hex++), b;
        if(!*hex) return 0;
        b=wire_hex(*hex++);
        if(a<0 || b<0 || n>=cap-1 || a*16+b==0) return 0;
        out[n++]=(char)(a*16+b);
    }
    out[n]=0; return 1;
}
#endif
