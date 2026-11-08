#ifndef CC1101_H
#define CC1101_H
#include <stdbool.h>
#include <stdint.h>
extern uint8_t cc_part,cc_version;
bool cc_init(uint32_t khz,unsigned power,bool ook);
bool cc_rssi(int *dbm);
bool cc_send(uint32_t sequence);
bool cc_poll(bool *done);
bool cc_receive(uint32_t *packets);
void cc_stop(void);
void cc_close(void);
#endif
