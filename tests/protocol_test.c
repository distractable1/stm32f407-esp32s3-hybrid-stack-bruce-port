#include "../shared/hat_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static hat_packet expected;
static unsigned received;
static void check_packet(void *ctx,const hat_packet *p){
 (void)ctx;CHECK(p->channel==expected.channel);CHECK(p->type==expected.type);
 CHECK(p->seq==expected.seq);CHECK(p->length==expected.length);
 CHECK(!memcmp(p->payload,expected.payload,p->length));received++;
}
static void ignore_packet(void *ctx,const hat_packet *p){(void)ctx;(void)p;}
static uint32_t random_state=0x43728abc;
static uint32_t rnd(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
int main(void){
 CHECK(hat_crc((const uint8_t *)"123456789",9)==0x29b1);
 uint8_t wire[HAT_WIRE_MAX],bad[HAT_WIRE_MAX];
 const unsigned sizes[]={0,1,2,63,64,65,255,512,1012,1024};
 for(unsigned size=0;size<sizeof sizes/sizeof *sizes;size++){
  expected.channel=size%3;expected.type=HAT_RADIO_TX;expected.seq=0x7e7d;expected.length=sizes[size];
  for(unsigned i=0;i<expected.length;i++)expected.payload[i]=(uint8_t)i;
  size_t n=hat_encode(&expected,wire,sizeof wire);CHECK(n);
  for(size_t split=0;split<=n;split++){
   hat_parser parser={0};received=0;
   hat_feed(&parser,wire,split,0,check_packet,NULL);
   hat_feed(&parser,wire+split,n-split,10,check_packet,NULL);CHECK(received==1);
   CHECK(parser.good==1&&!parser.bad_crc&&!parser.malformed);
  }
  hat_parser parser={0};received=0;
  for(size_t i=0;i<n;i++)hat_feed(&parser,wire+i,1,1,check_packet,NULL);
  CHECK(received==1);
  hat_feed(&parser,wire,n,2,check_packet,NULL);CHECK(received==2);
  memcpy(bad,wire,n);bad[3]^=0x10;
  received=0;memset(&parser,0,sizeof parser);
  hat_feed(&parser,bad,n,0,check_packet,NULL);CHECK(received==0);CHECK(parser.bad_crc==1);
  hat_feed(&parser,wire,n,1,check_packet,NULL);CHECK(received==1);
 }
 expected.channel=0;expected.type=HAT_WINDOW;expected.seq=7;expected.length=3;
 memset(expected.payload,0x7e,expected.length);
 size_t n=hat_encode(&expected,wire,sizeof wire);
 hat_parser parser={0};received=0;
 hat_feed(&parser,wire,5,UINT32_MAX-10,check_packet,NULL);
 hat_parser_tick(&parser,600);CHECK(parser.timeouts==1);
 hat_feed(&parser,wire,n,601,check_packet,NULL);CHECK(received==1);
 uint8_t oversized[]={0x7e,1,0,1,0,0,0xff,0xff,0,0x7e};
 hat_feed(&parser,oversized,sizeof oversized,700,check_packet,NULL);
 CHECK(parser.malformed==1);
 uint8_t dangling[]={0x7e,1,0x7d,0x7e};
 hat_feed(&parser,dangling,sizeof dangling,701,check_packet,NULL);
 CHECK(parser.malformed==2);
 hat_feed(&parser,wire,n,702,check_packet,NULL);CHECK(received==2);
 struct guarded {uint32_t before;hat_parser parser;uint32_t after;} guard={.before=0xcafebabe,.after=0xfeedface};
 for(unsigned i=0;i<1000000;i++){
  uint8_t v=(uint8_t)rnd();hat_feed(&guard.parser,&v,1,i,ignore_packet,NULL);
  CHECK(guard.before==0xcafebabe&&guard.after==0xfeedface);
  CHECK(guard.parser.used<=HAT_RAW_MAX);
 }
 hat_queue q={0};hat_packet copy;
 for(unsigned i=0;i<HAT_QUEUE_DEPTH;i++){expected.seq=i;CHECK(hat_push(&q,&expected));}
 CHECK(!hat_push(&q,&expected));
 for(unsigned i=0;i<HAT_QUEUE_DEPTH;i++){CHECK(hat_pop(&q,&copy));CHECK(copy.seq==i);}
 CHECK(!hat_pop(&q,&copy));
 expected.length=HAT_MAX_PAYLOAD+1;CHECK(!hat_push(&q,&expected));CHECK(!hat_encode(&expected,wire,sizeof wire));
 puts("PASS: CRC known vector; every split boundary; bytewise/coalesced frames; binary escaping; corruption/resync; timeout/wrap; oversized/dangling frames; 1M fuzz bytes; queue bounds/FIFO");
 return 0;
}
