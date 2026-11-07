#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
bool nrf_hw_open(void);
void nrf_hw_close(void);
void nrf_hw_ce(bool high);
bool nrf_hw_bus(uint8_t command,const uint8_t *tx,uint8_t *rx,unsigned length);
void nrf_hw_delay(unsigned us);
bool nrf_start(void);
void nrf_stop(void);
bool nrf_tx(const uint8_t *data,unsigned length);
unsigned nrf_rx(uint8_t data[32]);
bool nrf_scan_start(void);
bool nrf_scan_tick(uint32_t now,uint8_t result[126]);
void nrf_info(char *out,size_t size);
