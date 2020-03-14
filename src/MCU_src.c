#include "stdbool.h"
#include "TM4C123.h"
#include "driverlib/sysctl.h"
#include "driverlib/eeprom.h" 

//CRC8 polynomial
#define POLYNOMIAL 0xE7

void UART_init(void);
void UART_config(void);
void portF_LED_init(void);
void E2PROM_config(void);
uint8_t read(void);
void write(uint8_t c);
void writestring(char *string);
void receive_data(void);
void transmit_data(void);
uint8_t crc8_checkvalue_generation(uint8_t message);
uint8_t crc8_verification(uint8_t *byte_arr, uint8_t rxd_ch, uint8_t rxd_checkvalue);

//address used to write to EEPROM module
static uint32_t e2prom_write_addr = 0x00000000;
//address used to read from EEPROM module
static uint32_t e2prom_read_addr = 0x00000000; 

//to store the 8-bit data and 8 bit checkvalue
static uint8_t codeword[2];

int main(void)
{
	//initialization and configuration of required modules and peripherals
	UART_init();
	UART_config();
	portF_LED_init();
	E2PROM_config();

	//receiving data from PC
	receive_data();

	//transmitting data to PC
	transmit_data();

	while(1)
	{}
}

void UART_init(void)
{
	//Enable UART module being used. UART0 enabled.
	SYSCTL->RCGCUART |= (1 << 0);
	
	//Enable GPIO for port A
	SYSCTL->RCGCGPIO |= (1 << 0);
	
	//Enable alternate functions for PA0 and PA1
	GPIOA->AFSEL |= 0x03;

	//Configure the PMCn fields in the GPIOPCTL register to assign the UART signals to the appropriate
  //pins
	GPIOA->PCTL |= 0x11;
	
	//Digital enabling of UART pins PA0 and PA1
	GPIOA->DEN |= 0x03;
}

void UART_config(void)
{
	/*
	HSE=0 bit in UARTCTL register. ClkDiv=16
	Finding baud rate divisor
	BRD = 16,000,000/ (16 * 2400) = 416.6666667
	IBRD = 416
	FBRD => integer(0.6666667 * 64 + 0.5) = 43 
	*/
	
	//Disable the UART by clearing the UARTEN bit in the UARTCTL register
	UART0->CTL &= ~(1 << 0);
	
	//The UART is clocked using the system clock divided by 16
	UART0->CTL &= ~(1 << 5);
	
	//The UARTIBRD register is the integer part of the baud-rate divisor value.
	UART0->IBRD = 416;
	
	//The UARTFBRD register is the fractional part of the baud-rate divisor value.
	UART0->FBRD = 43;
	
	//Write the desired serial parameters in UARTLCRH register (UART Line Control)
	UART0->LCRH = (0x03 << 5) | (1 << 4); //8bit data, no parity, 1 stop bit, FIFO buffer enabled
	
  //Set clock configuration for UART. Default System Clock is PIOSC which has 16MHz frequency. System Clock used here for UART clock source.
	UART0->CC = 0x05;

	//Enable the UART by setting the UARTEN bit in the UARTCTL register. Also enabling UART Rx and Tx.
	UART0->CTL |= (1 << 0)|(1 << 8)|(1 << 9);
}

void portF_LED_init(void)
{
	//Enable Port F On-board LEDs. Used for debugging purposes
	SYSCTL->RCGCGPIO |= (1 << 5);
	GPIOF->DIR |= (1 << 1)|(1 << 2)|(1 << 3);
	GPIOF->DEN |= (1 << 1)|(1 << 2)|(1 << 3);
	GPIOF->DATA &= ~((1 << 1)|(1 << 2)|(1 << 3));
}

void E2PROM_config(void)
{
	//SYSCTL->RCGCEEPROM |= (1 << 0); 
	//Enabling clock for EEPROM module
	SysCtlPeripheralEnable(SYSCTL_PERIPH_EEPROM0);
	
	//Initialization of EEPROM module & checks for errors in EEPROM. If not called may lead to data loss. 
	EEPROMInit();
	
	//Erases EEPROM to default condition. EEPROM will contain value 0xFFFF_FFFF
	EEPROMMassErase();
}
 
uint8_t read(void)
{
	uint8_t c;
	
	//checking RXFE, waiting for data to be received from PC
	while((UART0->FR & (1 << 4)) != 0); 
	c = UART0->DR;
	
	return c;
}

void write(uint8_t c)
{
	//checking TXFF, waiting to transmit data to PC to display on serial monitor
	while((UART0->FR & (1 << 5)) != 0);
	
	UART0->DR = c;
}

void writestring(char *string)
{
	while(*string)
		write(*(string++));
}

void receive_data(void)
{
	//to store received data
	uint8_t rxd_ch = 0;
	//to store checkvalue for character received
	uint8_t rxd_checkvalue = 0;
	//CRC8 verification feedback to PC
	uint8_t status_flag = '0';
	//to keep track of number of bytes 
	uint32_t byte_count = 0;
	//final 32 bit word which will consist of 4 characters to be stored in single eeprom address
	uint32_t store_word_eeprom = 0x0;
	
	while((rxd_ch = read()) != '\0')
	{
		rxd_checkvalue = read();
		if(crc8_verification(codeword, rxd_ch, rxd_checkvalue) == 0x00)
		{
			//byte has NOT undergone corruption. Success status sent to PC to transmit next character 
			status_flag = '1';
			write(status_flag);
			
			store_word_eeprom |= (rxd_ch << (8*byte_count));
			byte_count = (byte_count + 1) % 4;
			
			//if 4 consecutive characters are stored in a word
			if(byte_count == 0)
			{
				while(EEPROMProgram(&store_word_eeprom, e2prom_write_addr, sizeof(store_word_eeprom)) != 0);
				store_word_eeprom = 0x0;
				e2prom_write_addr += 0x04;
			}
		}
		else
		{
			//byte received has undergone corruption. Fail status sent to PC to request re-transmission
			status_flag = '0';
			write(status_flag);
		}
	}
	
	//to store the final 32 bit word in eeprom which will contain NULL chracter '\0'
	rxd_checkvalue = read();
	while(crc8_verification(codeword, rxd_ch, rxd_checkvalue) != 0x00)
	{
		//byte received has undergone corruption. Fail status sent to PC to request re-transmission
		status_flag = '0';
		write(status_flag);
		rxd_ch = read();
		rxd_checkvalue = read();
		
	}
	//byte has NOT undergone corruption. Success status sent to PC to transmit next character
	status_flag = '1';
	write(status_flag);
	
	store_word_eeprom |= (rxd_ch << (8*byte_count));
	while(EEPROMProgram(&store_word_eeprom, e2prom_write_addr, sizeof(store_word_eeprom)) != 0);
}

void transmit_data(void)
{
	//character to be transmitted
	uint8_t txd_ch = 0;
	//checkvalue of character to be transmitted
	uint8_t txd_checkvalue = 0;
	//status flag to check if retransmission of character is required
	uint8_t status_flag = '0';
	//to retrieve 32-bit word from eeprom 
	uint32_t retrieve_word_eeprom = 0x0;
	
	while(e2prom_read_addr <= e2prom_write_addr)
	{
		//extracting the last word stored in eeprom which will contain NULL '\0' character
		if(e2prom_read_addr == e2prom_write_addr)
		{
			EEPROMRead(&retrieve_word_eeprom, e2prom_read_addr, sizeof(retrieve_word_eeprom));
			uint32_t byte_count = 0;
			txd_ch = retrieve_word_eeprom >> 8*byte_count;
			txd_checkvalue = crc8_checkvalue_generation(txd_ch);
			
			while(txd_ch != '\0')
			{
				status_flag = '0';
				while(status_flag == '0')
				{
					write(txd_ch);
					write(txd_checkvalue);
					//reading CRC8 verification status flag sent from PC
					//If status_flag='1', successful transmission
					//If if status_flag='0', falied transmission. Transmit to PC again
					status_flag = read();
				}
				
				byte_count = (byte_count + 1) % 4;
				txd_ch = retrieve_word_eeprom >> 8*byte_count;
				txd_checkvalue = crc8_checkvalue_generation(txd_ch);
			}
			
			//transmitting NULL character to PC
			txd_checkvalue = crc8_checkvalue_generation(txd_ch);//calculation of checksum of NULL character
			status_flag = '0';
			while(status_flag == '0')
			{
				write(txd_ch);
				write(txd_checkvalue);
				//reading CRC8 verification status flag from PC
				//If status_flag='1', successful transmission
				//If if status_flag='0', falied transmission. Transmit to PC again
				status_flag = read();
			}
			e2prom_read_addr += 0x04;
		}
		else
		{
			EEPROMRead(&retrieve_word_eeprom, e2prom_read_addr, sizeof(retrieve_word_eeprom));
			for(uint8_t char_count = 0; char_count < 4; char_count++)
			{
				txd_ch = retrieve_word_eeprom >> 8*char_count;
				txd_checkvalue = crc8_checkvalue_generation(txd_ch);
				status_flag = '0';
				
				while(status_flag == '0')
				{
					write(txd_ch);
					write(txd_checkvalue);
					//reading CRC8 verification status flag from PC
					//If status_flag='1', successful transmission
					//If if status_flag='0', falied transmission. Transmit to PC again
					status_flag = read();
				}
			}
			e2prom_read_addr += 0x04;
		}
	}
}

uint8_t crc8_checkvalue_generation(uint8_t message)
{
	uint8_t crc = message;
	for(uint8_t bits = 0; bits < 8; bits++)
  {
     if((crc & 0x80) != 0)
     {
				crc = ((crc << 1) ^ POLYNOMIAL);
     }
     else
     {
        crc <<= 1;
     }
  }
  return crc;
}

uint8_t crc8_verification(uint8_t *byte_arr, uint8_t rxd_ch, uint8_t rxd_checkvalue)
{
    uint8_t crc = 0;
		byte_arr[0] = rxd_ch;
		byte_arr[1] = rxd_checkvalue;
    for(uint8_t index = 0; index < 2; index++)
    {
        crc ^= byte_arr[index]; 
        for(uint8_t bits = 0; bits < 8; bits++)
        {
            if((crc & 0x80) != 0)
            {
                crc = ((crc << 1) ^ POLYNOMIAL);
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}