#ifndef NRF_RADIO_H
#define NRF_RADIO_H
#include <stdint.h>
typedef struct {
    uint8_t address[5];
    uint8_t address_length;
    uint8_t channel;
    uint8_t rate; /* 0 = 250 kbps, 1 = 1 Mbps, 2 = 2 Mbps */
    uint8_t payload_length; /* 1..32 for fixed-size packets */
    uint8_t dynamic_payload;
    uint8_t crc_bytes; /* 0, 1 or 2 */
} RadioProfile;
int radio_init(void);
void radio_stop(void);
int radio_receive(const RadioProfile *profile);
int radio_read(uint8_t payload[32], unsigned *length);
unsigned radio_activity(unsigned channel);
int radio_send_once(const RadioProfile *profile, const uint8_t *payload, unsigned length);
#endif
