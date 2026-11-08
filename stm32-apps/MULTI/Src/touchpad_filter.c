#include "touchpad_filter.h"

#include <stdlib.h>
static int gain=3;static unsigned smoothing=2;
static bool active,candidate;static int rx,ry,cx,cy;
static int xs[3],ys[3];static unsigned index;
static float fx,fy,anchorx,anchory,pendingx,pendingy;
static int offeredx,offeredy;static uint32_t last_tick,last_sample;
static int median(int a,int b,int c){if(a>b){int t=a;a=b;b=t;}if(b>c){int t=b;b=c;c=t;}return a>b?a:b;}
void pad_reset(void){active=candidate=false;pendingx=pendingy=0;offeredx=offeredy=0;}
void pad_stop(void){pad_reset();}
void pad_gain(int q){if(q<1)q=1;if(q>16)q=16;gain=q;pendingx=pendingy=0;}
int pad_gain_value(void){return gain;}
void pad_smoothing(unsigned l){smoothing=l%4;pendingx=pendingy=0;}
unsigned pad_smoothing_value(void){return smoothing;}
static void anchor(int x,int y,uint32_t now){rx=x;ry=y;fx=anchorx=x;fy=anchory=y;for(unsigned i=0;i<3;++i){xs[i]=x;ys[i]=y;}index=0;pendingx=pendingy=0;candidate=false;last_tick=last_sample=now;active=true;}
void pad_point(int x,int y,uint32_t now){if(x<0||x>=320||y<0||y>=240)return;if(!active){anchor(x,y,now);return;}
 if(abs(x-rx)>36||abs(y-ry)>36){if(candidate&&abs(x-cx)<=12&&abs(y-cy)<=12&&(uint32_t)(now-last_sample)<80){anchor(x,y,now);}else{candidate=true;cx=x;cy=y;last_sample=now;}return;}
 candidate=false;rx=x;ry=y;xs[index]=x;ys[index]=y;index=(index+1)%3;last_sample=now;}
bool pad_poll(uint32_t now,bool connected,int*dx,int*dy){*dx=*dy=0;if(!active)return false;if(!connected){anchor(rx,ry,now);return false;}if((uint32_t)(now-last_tick)<10)return false;last_tick=now;
 float alpha=smoothing==0?1.0f:smoothing==1?0.5f:smoothing==2?0.25f:0.125f;
 int x=smoothing?median(xs[0],xs[1],xs[2]):rx;int y=smoothing?median(ys[0],ys[1],ys[2]):ry;
 fx+=(x-fx)*alpha;fy+=(y-fy)*alpha;float ex=fx-anchorx,ey=fy-anchory;
 if((ex<0?-ex:ex)>=0.8f){pendingx+=ex*gain/4.0f;anchorx=fx;}if((ey<0?-ey:ey)>=0.8f){pendingy+=ey*gain/4.0f;anchory=fy;}
 if(pendingx>24){pendingx=24;}if(pendingx< -24){pendingx=-24;}if(pendingy>24){pendingy=24;}if(pendingy< -24){pendingy=-24;}
 offeredx=pendingx>=0?(int)(pendingx+0.5f):(int)(pendingx-0.5f);offeredy=pendingy>=0?(int)(pendingy+0.5f):(int)(pendingy-0.5f);
 if(offeredx>12){offeredx=12;}if(offeredx< -12){offeredx=-12;}
 if(offeredy>12){offeredy=12;}if(offeredy< -12){offeredy=-12;}*dx=offeredx;*dy=offeredy;return offeredx||offeredy;}
void pad_commit(bool accepted){if(accepted){pendingx-=offeredx;pendingy-=offeredy;}offeredx=offeredy=0;}
