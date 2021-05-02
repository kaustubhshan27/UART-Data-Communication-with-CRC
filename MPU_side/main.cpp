#include <iostream>
#include <cstring>
#include <cstdint>
#include <cstdbool>
#include "serial/serial.h"
#include "crc16/crc16.h"

#define RW_TIMEOUT 1000
#define STR_SIZE    30

enum
{
	SEND_DATA = 1,
	SEND_CRC = 2,
	GET_DATA = 3,
	GET_CRC = 4,
};

enum
{
	STR_HEADER = 11,
	STR_ENDING = 12,
};

enum
{
	CRC_1 = 21,
	CRC_2 = 22
};

void send_message(void);
void record_data(uint8_t incoming_data);
void display_rx_string(void);
void parse_message(void);

bool get_user_data_flag = true;
bool Tx_data_flag = false;
bool Rx_data_flag = false;
bool data_sent = false;
bool data_received = false;

uint8_t data = 0;
uint8_t crc16_Tx_bytes[2];
uint8_t crc16_Rx_bytes[2];
uint8_t rx_str[STR_SIZE];
uint8_t feedback_status = 0;
uint8_t str_index = 0;

serial::Serial my_serial("/dev/ttyACM0", 2400, serial::Timeout::simpleTimeout(RW_TIMEOUT), serial::eightbits, 
    serial::parity_none, serial::stopbits_one, serial::flowcontrol_none);

int main(void)
{
    while(1)
    {
        if(get_user_data_flag)
        {
            int temp_data;
            std::cout << "Enter user data (0-100)" << std::endl;
            std::cin >> temp_data;
            data = uint8_t(temp_data);
            
            uint16_t crc16_checkvalue = crc16_ccitt(&data, 1);
            crc16_Tx_bytes[0] = ((crc16_checkvalue) & (0xFF));//lower byte
            crc16_Tx_bytes[1] = ((crc16_checkvalue >> 8) & (0xFF));//higher byte

            get_user_data_flag = false;
            Tx_data_flag = true;
        }
        else
        {
            if(Tx_data_flag)
            {            
                if(!data_sent)    
                    send_message();
                
                if(data_sent)
                {
                    if(my_serial.available() > 0)
                    {
                        my_serial.read(&feedback_status, 1);
                        if(feedback_status == 1)
                        {
                            Tx_data_flag = false;
                            Rx_data_flag = true;
                            feedback_status = 0;
                            data_sent = false;
                        }
                        else
                        {
                            data_sent = false;
                        }
                    }
                }
            }
            else if(Rx_data_flag)
            {
                if(!data_received)
                    parse_message();

                if(data_received)
                {
                    if(validate_message(crc16_Rx_bytes, rx_str, str_index))
                    {       
                        Rx_data_flag = false;
                        get_user_data_flag = true;
                        feedback_status = 1;
                        my_serial.write(&feedback_status, 1);

                        display_rx_string();
                    }
                    else
                    {
                        feedback_status = 0;
                        my_serial.write(&feedback_status, 1);
                    }
                    str_index = 0;
                    data_received = false;
                }
            }
        }
    }
    return 0;
}

void send_message(void)
{
	static uint8_t msg_send_state = SEND_DATA;
	static uint8_t crc_state = CRC_1;

    switch(msg_send_state)
    {
        case SEND_DATA:
        {
            my_serial.write(&data, 1);
            msg_send_state = SEND_CRC;
        }
        break;
        case SEND_CRC:
        {
            switch(crc_state)
            {
                case CRC_1:
                {
                    my_serial.write(&crc16_Tx_bytes[0], 1);
                    crc_state = CRC_2;
                }
                break;
                case CRC_2:
                {
                    my_serial.write(&crc16_Tx_bytes[1], 1);
                    crc_state = CRC_1;
                    msg_send_state = SEND_DATA;
                    data_sent = true;
                }
                break;
            }
        }
        break;
    }
}

void record_data(uint8_t incoming_data)
{
    rx_str[str_index] = incoming_data;
    str_index++;
}

void display_rx_string(void)
{
    for(uint8_t index = 0; index < str_index; index++)
    {
        std::cout << rx_str[index];
    }
    std::cout << std::endl;
}

void parse_message(void)
{
    static uint8_t msg_parse_state = GET_DATA;
    static uint8_t rx_str_state = STR_HEADER;
	static uint8_t crc_state = CRC_1;

	if(my_serial.available() > 0)
	{
		switch(msg_parse_state)
		{
			case GET_DATA:
			{
                uint8_t incoming_data;
				switch(rx_str_state)
				{
					case STR_HEADER:
					{
                        my_serial.read(&incoming_data, 1);
						record_data(incoming_data);
						rx_str_state = STR_ENDING;
					}
					break;
					case STR_ENDING:
					{
                        my_serial.read(&incoming_data, 1);
						if(incoming_data == '\0')
						{
							record_data(incoming_data);
							rx_str_state = STR_HEADER;
							msg_parse_state = GET_CRC;
						}
						else
						{
							record_data(incoming_data);
						}
					}
					break;
				}
			}
			break;
			case GET_CRC:
			{
				switch(crc_state)
				{
					case CRC_1:
					{
						my_serial.read(&crc16_Rx_bytes[0], 1);
						crc_state = CRC_2;
					}
					break;
					case CRC_2:
					{
						my_serial.read(&crc16_Rx_bytes[1], 1);
						crc_state = CRC_1;
						msg_parse_state = GET_DATA;
                        data_received = true;
					}
					break;
				}
			}
			break;
		}
	}
}
