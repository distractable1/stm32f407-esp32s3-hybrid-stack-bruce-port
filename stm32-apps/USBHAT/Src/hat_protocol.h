#ifndef HAT_PROTOCOL_H
#define HAT_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* HDLC-style byte stuffing; all multi-byte fields are big endian.
 * 7e | version channel type seq[2] length[2] payload crc16[2] | 7e
 * CRC-16/CCITT-FALSE covers the unescaped header and payload. */
#define HAT_MAX_PAYLOAD 1024u
#define HAT_RAW_MAX (HAT_MAX_PAYLOAD + 9u)
#define HAT_WIRE_MAX (2u * HAT_RAW_MAX + 2u)
#define HAT_VID 0xcafeu
#define HAT_PID 0x4071u /* Bench IDs only; obtain assigned IDs for distribution. */
enum { HAT_DISPLAY=0, HAT_RADIO=1, HAT_SYSTEM=2 };
enum { HAT_WINDOW=1, HAT_TOUCH=2, HAT_RADIO_SELECT=5,
       HAT_RADIO_TX=6, HAT_RADIO_RX=7, HAT_COMMAND=8,
       HAT_STATUS=9, HAT_ACK=10, HAT_PING=11 };
enum { HAT_OK=0, HAT_BAD_PACKET=1, HAT_UNSUPPORTED=2, HAT_BUSY=3 };
typedef struct {
    uint8_t channel, type;
    uint16_t seq, length;
    uint8_t payload[HAT_MAX_PAYLOAD];
} hat_packet;
typedef void (*hat_receive_fn)(void *, const hat_packet *);
typedef struct {
    uint8_t raw[HAT_RAW_MAX];
    size_t used;
    bool escaped, discard, active;
    uint32_t last_ms, good, bad_crc, malformed, timeouts;
} hat_parser;
static inline uint16_t hat_get16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);
}
static inline void hat_put16(uint8_t *p, uint16_t n) {
    p[0]=(uint8_t)(n>>8); p[1]=(uint8_t)n;
}
static inline uint16_t hat_crc(const uint8_t *p, size_t n) {
    uint16_t c=0xffff;
    while(n--) { c^=(uint16_t)*p++<<8;
        for(unsigned i=0;i<8;i++) c=(uint16_t)((c<<1)^((c&0x8000)?0x1021:0));
    }
    return c;
}
static inline size_t hat_encode(const hat_packet *p, uint8_t *out, size_t cap) {
    uint8_t raw[HAT_RAW_MAX];
    if(p->channel>2 || p->length>HAT_MAX_PAYLOAD || cap<HAT_WIRE_MAX) return 0;
    raw[0]=1; raw[1]=p->channel; raw[2]=p->type;
    hat_put16(raw+3,p->seq); hat_put16(raw+5,p->length);
    memcpy(raw+7,p->payload,p->length);
    hat_put16(raw+7+p->length,hat_crc(raw,7+p->length));
    size_t k=0; out[k++]=0x7e;
    for(size_t i=0;i<9u+p->length;i++) {
        uint8_t v=raw[i];
        if(v==0x7e || v==0x7d) {out[k++]=0x7d; v^=0x20;}
        out[k++]=v;
    }
    out[k++]=0x7e; return k;
}
static inline void hat_parser_tick(hat_parser *s, uint32_t now) {
    if(s->active && (s->used || s->escaped || s->discard) && (uint32_t)(now-s->last_ms)>500) {
        s->active=false; s->used=0; s->escaped=false; s->discard=false; s->timeouts++;
    }
}
static inline void hat_feed(hat_parser *s,const uint8_t *data,size_t n,
                            uint32_t now,hat_receive_fn cb,void *ctx) {
    hat_parser_tick(s,now);
    while(n--) {
        uint8_t v=*data++; s->last_ms=now;
        if(v==0x7e) {
            if(s->active && s->used && !s->discard && !s->escaped) {
                size_t z=s->used;
                if(z<9 || s->raw[0]!=1 || s->raw[1]>2 ||
                   hat_get16(s->raw+5)>HAT_MAX_PAYLOAD || z!=9u+hat_get16(s->raw+5)) s->malformed++;
                else if(hat_crc(s->raw,z-2)!=hat_get16(s->raw+z-2)) s->bad_crc++;
                else {
                    hat_packet p; p.channel=s->raw[1]; p.type=s->raw[2];
                    p.seq=hat_get16(s->raw+3); p.length=hat_get16(s->raw+5);
                    memcpy(p.payload,s->raw+7,p.length); s->good++; cb(ctx,&p);
                }
            } else if(s->active && (s->discard || s->escaped)) s->malformed++;
            s->active=true; s->used=0; s->escaped=false; s->discard=false; continue;
        }
        if(!s->active || s->discard) continue;
        if(s->escaped) {v^=0x20; s->escaped=false;}
        else if(v==0x7d) {s->escaped=true; continue;}
        if(s->used==sizeof s->raw) {s->discard=true; continue;}
        s->raw[s->used++]=v;
        /* Reject impossible lengths before accumulating the rest. */
        if(s->used==7 && hat_get16(s->raw+5)>HAT_MAX_PAYLOAD) s->discard=true;
    }
}

#define HAT_QUEUE_DEPTH 4u
typedef struct {hat_packet slot[HAT_QUEUE_DEPTH]; unsigned head,count;} hat_queue;
static inline bool hat_push(hat_queue *q,const hat_packet *p) {
    if(q->count==HAT_QUEUE_DEPTH || p->length>HAT_MAX_PAYLOAD) return false;
    q->slot[(q->head+q->count)%HAT_QUEUE_DEPTH]=*p; q->count++; return true;
}
static inline bool hat_pop(hat_queue *q,hat_packet *p) {
    if(!q->count) return false;
    *p=q->slot[q->head]; q->head=(q->head+1)%HAT_QUEUE_DEPTH; q->count--; return true;
}
#endif
