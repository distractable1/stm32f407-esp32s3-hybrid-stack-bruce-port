#ifndef TOOL_RADIO_H
#define TOOL_RADIO_H
#include <stdbool.h>
#include <stdint.h>

typedef enum ToolRadio { RADIO_OFF, RADIO_NRF24, RADIO_CC1101 } ToolRadio;
typedef struct RadioProfile {
    ToolRadio family;
    uint32_t frequency_khz, rate_bps;
    uint8_t channel, payload_length, address[5], crc_bytes;
} RadioProfile;
typedef struct RadioPacket {
    uint32_t timestamp;
    uint8_t length, data[64];
    int16_t rssi_dbm;
    uint8_t lqi;
} RadioPacket;

void tool_radio_set_channels(unsigned count);
void tool_radio_defaults(ToolRadio family, RadioProfile *profile);
bool tool_radio_open(const RadioProfile *profile);
void tool_radio_close(void);
bool tool_radio_scan_start(void);
bool tool_radio_receive_start(void);
void tool_radio_stop(void);
void tool_radio_tick(uint32_t now);
bool tool_radio_packet(RadioPacket *packet);
const uint8_t *tool_radio_bins(unsigned *count);
unsigned tool_radio_scan_position(void);
const char *tool_radio_status(void);
bool tool_radio_present(void);
bool tool_radio_scanning(void);
bool tool_radio_receiving(void);
uint32_t tool_radio_drops(void);

/* Shared SPI connector requires the correct module-specific adapter wiring.
 * This module has no transmit operation, acknowledgement or replay path.
 * Frequency is in kHz, data rate in bits/second (CC preset=2400).
 * NRF packet rssi_dbm=INT16_MIN means unavailable; its scan is binary RPD.
 */
#endif
