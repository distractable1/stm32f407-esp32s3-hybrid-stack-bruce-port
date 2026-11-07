#include "../esp32/lib/HatLink/hat_contact.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 hat_contact c={0};
 assert(!hat_contact_step(&c,false,0,0));
 assert(hat_contact_step(&c,true,10,10));
 /* A stationary hold and any coordinate jitter must not repeat actions. */
 for(unsigned t=20;t<2000;t+=10)assert(!hat_contact_step(&c,true,t,t));
 assert(!hat_contact_step(&c,false,2000,2000));
 assert(hat_contact_step(&c,true,2050,2050));
 /* A dropped release expires; an old pressed packet cannot trigger an action. */
 assert(!hat_contact_step(&c,true,2050,2401));
 assert(!hat_contact_step(&c,true,2050,2500));
 assert(hat_contact_step(&c,true,2600,2600));
 assert(!hat_contact_step(&c,false,2700,2700));
 assert(hat_contact_step(&c,true,UINT32_MAX-100,50));
 assert(!hat_contact_step(&c,true,UINT32_MAX-100,251));
 puts("PASS: one action per contact; holds/heartbeat do not repeat; release rearms; stale press expires; timer wrap");
}
