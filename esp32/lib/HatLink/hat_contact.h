#pragma once
#include <stdint.h>
#include <stdbool.h>
/* One action per physical contact. Lost release packets cannot hold a key
 * forever: the device renews its state at 100 ms; host lease is 350 ms. */
typedef struct { bool held; } hat_contact;
static inline bool hat_contact_step(hat_contact *c,bool pressed,uint32_t at,uint32_t now){
 if(!pressed || (uint32_t)(now-at)>350u){c->held=false;return false;}
 if(c->held)return false;
 c->held=true;return true;
}
