#include "crc16/crc16.h"

uint16_t crc16_ccitt(const uint8_t *data, uint8_t len)
{
    uint16_t crc;
    crc = 0xFFFF ^ 0xFFFF;
    while (len > 0)
    {
        crc = table[*data ^ (uint8_t)crc] ^ (crc >> 8);
        data++;
        len--;
    }
    crc = crc ^ 0xFFFF;

    return crc;
}

bool validate_message(const uint8_t *crc16_Rx_bytes, const uint8_t *data, uint8_t len)
{
    uint16_t received_crc = ((crc16_Rx_bytes[1] << 8) | (crc16_Rx_bytes[0]));
	uint16_t calculated_crc = crc16_ccitt(data, len);
	if(received_crc == calculated_crc)
		return true;
	else
		return false;
}