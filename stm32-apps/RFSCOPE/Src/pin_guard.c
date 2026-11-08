#include "pin_guard.h"
pin_result_t pin_guard_request(const pin_info_t *p, pin_request_t *r, uint32_t boot){
 if(!p||!r||p->classification==PIN_CORE||p->classification==PIN_UNKNOWN||p->classification==PIN_RESERVED||p->activity==PIN_FAULT)return PIN_DENIED;
 r->port=p->port;r->number=p->number;r->generation=boot;r->valid=true;
 return (p->owned||p->activity==PIN_ACTIVE)?PIN_PENDING_RESTART:PIN_ALLOWED;
}
bool pin_guard_apply(const pin_info_t *p,const pin_request_t *r,uint32_t boot,bool physical,bool electrical){
 if(!p||!r||!r->valid||p->port!=r->port||p->number!=r->number)return false;
 if(p->classification!=PIN_USER||p->activity!=PIN_IDLE||p->owned)return false;
 if(boot<=r->generation)return false;
 return physical||electrical;
}
