#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "crc16.h"
#include "divisible.h"
#include "messages.h"
#include "buffer.h"
#include "inc/hw_gpio.h"
#include "inc/hw_uart.h"
#include "inc/hw_memmap.h"
#include "inc/tm4c123gh6pm.h"
#include "driverlib/pin_map.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"

enum
{
    SEND_DATA = 1, SEND_CRC = 2, GET_DATA = 3, GET_CRC = 4,
};

enum
{
    STR_HEADER = 11, STR_ENDING = 12,
};

enum
{
    CRC_1 = 21, CRC_2 = 22
};

//buffer size 50
#define BUFFER_SIZE 50
//System Clock Freq.
#define CLK_FREQ 16000000
//total blinking time
#define PERIOD 10
//Max. string size
#define STR_SIZE 30
//Size of buffer
#define BUFFER_SIZE 50
//Circular buffer EMPTY condition
#define BUFF_EMPTY  0
//Circular buffer FULL condition
#define BUFF_FULL   1
//Circular buffer NOT FULL condition
#define BUFF_NOT_FULL   2

void portF_config(void);
void timer1A_config(void);
void timer0B_config(void);
void uDMA_init(void);
void uDMA_config_UART0(void);
void uDMA_Error_Handler(void);
void UART_init(void);
void UART_config(void);
void UART0_Handler(void);
void send_data(uint8_t outgoing_data);
void parse_message(void);
void select_str(void);
void send_message(void);
void onBoardLED(uint8_t duty_cycle);

//1024-byte aligned channel control table
#pragma DATA_ALIGN(uc_control_table, 1024)
uint32_t uc_control_table[256];

struct Buffer buffRx;
struct Buffer buffTx;

bool Tx_data_flag = false;
bool Rx_data_flag = true;
bool data_sent = false;
bool data_received = false;

uint8_t user_data; //duty cycle - from user
uint8_t Rx_buffer[BUFFER_SIZE];
uint8_t Tx_buffer[BUFFER_SIZE];
uint8_t crc16_Tx_bytes[2];
uint8_t crc16_Rx_bytes[2];
char send_str[STR_SIZE];
uint8_t feedback_status = 0;

int main(void)
{
    buffer_init(&buffRx, Rx_buffer);
    buffer_init(&buffTx, Tx_buffer);

    portF_config();
    timer1A_config();
    timer0B_config();
    UART_init();
    UART_config();

    IntMasterEnable();

    while(1)
    {
        if(Rx_data_flag)
        {
            if(!data_received)
                parse_message();

            if(data_received)
            {
                if(validate_message(crc16_Rx_bytes, &user_data, 1))
                {
                    Rx_data_flag = false;
                    Tx_data_flag = true;
                    feedback_status = 1;
                    send_data(feedback_status);

                    select_str();
                    uint16_t Tx_crc16 = crc16_ccitt((uint8_t *)send_str, strlen(send_str)+1);
                    crc16_Tx_bytes[0] = (Tx_crc16 & 0xFF);
                    crc16_Tx_bytes[1] = ((Tx_crc16 >> 8) & 0xFF);
                }
                else
                {
                    feedback_status = 0;
                    send_data(feedback_status);
                }
                data_received = false;
            }
        }
        else if(Tx_data_flag)
        {
            if (!data_sent)
                send_message();

            if(data_sent)
            {
                if (buffer_space(&buffRx) != BUFF_EMPTY)
                {
                    feedback_status = buffer_get(&buffRx);
                    if (feedback_status == 1)
                    {
                        Tx_data_flag = false;
                        Rx_data_flag = true;
                        feedback_status = 0;
                        data_sent = false;

                        onBoardLED(100 - user_data);
                    }
                    else
                    {
                        data_sent = false;
                    }
                }
            }
        }
    }
}

void portF_config(void)
{
    //Enabling portF
    SYSCTL_RCGCGPIO_R |= (0x01 << 5);
    //PF1 as configured as input
    GPIO_PORTF_DIR_R &= ~(0x02);
    //PF1 alternate function - On board Red LED
    GPIO_PORTF_AFSEL_R |= (0x01 << 1);
    //PF1 controlled by Timer0 B (T0CCP1)
    GPIO_PORTF_PCTL_R |= (0x07 << 4);
    GPIOPinTypeTimer(GPIO_PORTF_BASE, GPIO_PIN_1);
}

void UART_init(void)
{
    //Enable UART module being used. UART0 enabled.
    SYSCTL_RCGCUART_R |= (1 << 0);

    //Enable GPIO for port A
    SYSCTL_RCGCGPIO_R |= (1 << 0);

    //Enable alternate functions for PA0 and PA1
    GPIO_PORTA_AFSEL_R |= 0x03;

    //Configure the PMCn fields in the GPIOPCTL register to assign the UART signals to the appropriate
    //pins
    GPIO_PORTA_PCTL_R |= 0x11;

    //Digital enabling of UART pins PA0 and PA1
    GPIO_PORTA_DEN_R |= 0x03;
}

void uDMA_init(void)
{
    //providing clock uDMA module
    SYSCTL_RCGCDMA_R |= (1 << 0);

    //to enable the uDMA controller
    UDMA_CFG_R |= (1 << 0);

    //to set the channel control table
    UDMA_CTLBASE_R = (uint32_t)uc_control_table;

    //set the interrupt priority to 2
    IntPrioritySet(INT_UDMAERR, 2);

    //registers a function to be called when an uDMA error interrupt occurs
    IntRegister(INT_UDMAERR, uDMA_Error_Handler);

    //enable uDMA interrupt
    IntEnable(INT_UDMAERR);
}

void uDMA_config_UART0(void)
{
    //default priority for channel UART0
    UDMA_PRIOCLR_R |= (1 << 8);

    //alternate table not used, use primary control
    UDMA_ALTCLR_R |= (1 << 8);

    //UART only supports single requests
    UDMA_USEBURSTSET_R &= ~(1 << 8);

    //allow uDMA controller to recognize requests from UART
    UDMA_REQMASKCLR_R |= (1 << 8);

    //address of last byte of source buffer
    uc_control_table[8*4] = (uint32_t)(UART0_BASE+UART_O_DR);

    //address of last byte of destination buffer
    uint8_t *uart_value_address = &user_data;
    uc_control_table[8*4 + 1] = (uint32_t)uart_value_address;

    //control word configuration
    /*
    * DSTINC - NONE
    * DSTSIZE - word(32 bits)
    * SRCINC - NONE
    * SRCSIZE - word(32 bits)
    * ARBSIZE - 1
    * XFERSIZE - 1
    * NXTUSEBURST - NONE
    * XFERMODE - BASIC
    */
    uc_control_table[8*4 + 2] = 0xEE000001;

    UDMA_ENASET_R |= (1 << 8);
}

void uDMA_Error_Handler(void)
{
    static uint32_t uDMA_error_count = 0;
    if( ((UDMA_ERRCLR_R) & (1 << 0)) == 1)
    {
        UDMA_ERRCLR_R = 1;//clear uDMA error
        uDMA_error_count++;
    }
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
    UART0_CTL_R &= ~(1 << 0);

    //The UART is clocked using the system clock divided by 16
    UART0_CTL_R &= ~(1 << 5);

    //The UARTIBRD register is the integer part of the baud-rate divisor value.
    UART0_IBRD_R = 416;

    //The UARTFBRD register is the fractional part of the baud-rate divisor value.
    UART0_FBRD_R = 43;

    //Write the desired serial parameters in UARTLCRH register (UART Line Control)
    UART0_LCRH_R = (0x03 << 5); //8bit data, no parity, 1 stop bit, FIFO buffer disabled

    //Set clock configuration for UART. Default System Clock is PIOSC which has 16MHz frequency. System Clock used here for UART clock source.
    UART0_CC_R = 0x05;

    //Enable Receive DMA and DMA on error
    UART0_DMACTL_R |= (1 << 0)|(1 << 2);

    //Receive interrupt and Transmit interrupt enabled
    UART0_IM_R |= (1 << 4) | (1 << 5);

    // Set the priority to 0
    IntPrioritySet(INT_UART0, 0);

    //Registers a function to be called when an interrupt occurs
    IntRegister(INT_UART0, UART0_Handler);

    //Enable the NVIC for the UART0
    IntEnable(INT_UART0);

    //Enable the UART by setting the UARTEN bit in the UARTCTL register. Also enabling UART Rx and Tx.
    UART0_CTL_R |= (1 << 0) | (1 << 8) | (1 << 9);
}

void UART0_Handler(void)
{
    if ((((UART0_MIS_R) & (1 << 4)) == (1 << 4))) //Receive Interrupt
    {
        if ((((UART0_FR_R) & (1 << 4)) == 0)
                && (buffer_space(&buffRx) != BUFF_FULL)) //if(Rx_FIFO != EMPTY && Rx_circular_buffer != FULL)
        {
            uint8_t data = UART0_DR_R;
            buffer_add(&buffRx, data);
        }

        UART0_ICR_R |= (1 << 4); //clearing Receive Interrupt flag
        return;
    }
    else if (((UART0_MIS_R) & (1 << 5)) == (1 << 5)) //Transmit Interrupt
    {
        if (buffer_space(&buffTx) == BUFF_EMPTY) //no data present in the circular buffer to add to the Tx FIFO
        {
            UART0_IM_R &= ~(1 << 5); //disabling transmit interrupt
            UART0_ICR_R |= (1 << 5); //clearing Transmit Interrupt Flag
            return;
        }
        else
        {
            if ((((UART0_FR_R) & (1 << 5)) != (1 << 5))) //if(Tx_FIFO != FULL)
            {
                uint8_t data = buffer_get(&buffTx); //add data to Tx FIFO
                UART0_DR_R = data;
            }
            UART0_ICR_R |= (1 << 5); //clearing Transmit Interrupt Flag
            return;
        }
    }
}

void timer1A_config(void)
{
    //Enable Timer 1
    SYSCTL_RCGCTIMER_R |= (0x1 << 1);
    //Timer 1A disabled
    TIMER1_CTL_R &= ~(0x01 << 0);
    //Timer 1 module configured as 32-bit timer (concatenated)
    TIMER1_CFG_R = 0x00000000;
    //Timer 1A set mode - Periodic, Count Down
    TIMER1_TAMR_R |= 0x02;
    TIMER1_TAMR_R &= ~(0x1 << 4);
    /*Load Value calculation
     *  Clk=16MHz
     *  Total period=10s
     *  load value=160,000,000
     */
    TIMER1_TAILR_R = PERIOD * CLK_FREQ; //(10*16,000,000)
}

void timer0B_config(void)
{
    //Enable Timer 0
    SYSCTL_RCGCTIMER_R |= (0x1 << 0);
    //Timer 0B disabled
    TIMER0_CTL_R &= ~(0x01 << 8);
    //Timer 0 module configured as 16-bit timer
    TIMER0_CFG_R |= (0x04);
    //Timer 0B set mode
    TIMER0_TBMR_R |= (0x2 << 0) | (0x1 << 3); //TBMR=0x2(periodic), TBAMS=0x1(PWM)
    TIMER0_TBMR_R &= ~(0x4); //For PWM, TBCMR to be cleared
    //Non-inverted PWM
    TIMER0_CTL_R &= ~(0x1 << 14);
    /*Load Value calculation
     *  Clk=16MHz
     *  Blink period=1s
     *  load value=65535
     *  prescale value=255
     */
    TIMER0_TBILR_R = 0xFFFF;
    TIMER0_TBPR_R = 0xFF;
}

void parse_message(void)
{
    static uint8_t msg_parse_state = GET_DATA;
    static uint8_t crc_state = CRC_1;

    if(buffer_space(&buffRx) != BUFF_EMPTY)
    {
        switch (msg_parse_state)
        {
            case GET_DATA:
            {
                user_data = buffer_get(&buffRx);
                msg_parse_state = GET_CRC;
            }
            break;
            case GET_CRC:
            {
                switch (crc_state)
                {
                    case CRC_1:
                    {
                        crc16_Rx_bytes[0] = buffer_get(&buffRx);
                        crc_state = CRC_2;
                    }
                    break;
                    case CRC_2:
                    {
                        crc16_Rx_bytes[1] = buffer_get(&buffRx);
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

void select_str(void)
{
    if(divisible(user_data, div1) && divisible(user_data, div2))
        memcpy(send_str, str3, sizeof(str3));
    else
    {
        if(divisible(user_data, div1))
            memcpy(send_str, str1, sizeof(str1));
        else if(divisible(user_data, div2))
            memcpy(send_str, str2, sizeof(str2));
        else
            sprintf(send_str, "%u", user_data);
    }
}

void send_message(void)
{
    static uint8_t msg_send_state = SEND_DATA;
    static uint8_t tx_str_state = STR_HEADER;
    static uint8_t crc_state = CRC_1;
    static uint8_t str_index = 0;

    if(buffer_space(&buffTx) != BUFF_FULL)
    {
        switch(msg_send_state)
        {
            case SEND_DATA:
            {
                uint8_t outgoing_data;
                switch(tx_str_state)
                {
                    case STR_HEADER:
                    {
                        outgoing_data = send_str[str_index];
                        send_data(outgoing_data);
                        str_index++;
                        tx_str_state = STR_ENDING;
                    }
                    break;
                    case STR_ENDING:
                    {
                        outgoing_data = send_str[str_index];
                        if(outgoing_data == '\0')
                        {
                            send_data(outgoing_data);
                            tx_str_state = STR_HEADER;
                            msg_send_state = SEND_CRC;
                            str_index = 0;
                        }
                        else
                        {
                            send_data(outgoing_data);
                            str_index++;
                        }
                    }
                    break;
                }
            }
            break;
            case SEND_CRC:
            {
                switch (crc_state)
                {
                    case CRC_1:
                    {
                        send_data(crc16_Tx_bytes[0]);
                        crc_state = CRC_2;
                    }
                    break;
                    case CRC_2:
                    {
                        send_data(crc16_Tx_bytes[1]);
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
}

void send_data(uint8_t outgoing_data)
{
    if(buffer_space(&buffTx) == BUFF_EMPTY)
    {
        if(((UART0_FR_R) & (1 << 5)) != (1 << 5))//if(Tx_FIFO != FULL)
        {
            UART0_DR_R = outgoing_data;
            UART0_IM_R |= (1 << 5);//enable Tx interrupt
        }
        else
        {
            UART0_IM_R &= ~(1 << 5);//disable Tx interrupt
            buffer_add(&buffTx, outgoing_data);
            UART0_IM_R |= (1 << 5);//enable Tx interrupt
        }
    }
    else
    {
        UART0_IM_R &= ~(1 << 5);//disable Tx interrupt
        buffer_add(&buffTx, outgoing_data);
        UART0_IM_R |= (1 << 5);//enable Tx interrupt
    }
}

void onBoardLED(uint8_t duty_cycle_complement)
{
    GPIO_PORTF_DIR_R &= ~(0x02);
    GPIO_PORTF_AFSEL_R |= (0x01 << 1);

    if(duty_cycle_complement == 100)
    {
        GPIO_PORTF_AFSEL_R &= ~(0x01 << 1);
        GPIO_PORTF_DIR_R |= (0x02);
        GPIO_PORTF_DATA_R = 0;
    }
    else
    {
        uint32_t d = (duty_cycle_complement * 0xFFFFFF) / 100;
        //Macth value for Timer 0B according to duty cycle
        TIMER0_TBMATCHR_R = (d & 0x0000FFFF);
        TIMER0_TBPMR_R = ((d & 0x00FF0000) >> 16);
    }

    //Timer 0B enabled
    TIMER0_CTL_R |= (0x01 << 8);
    //Timer 1A enabled
    TIMER1_CTL_R |= (0x01 << 0);

    while ((TIMER1_RIS_R & 0x01) == 0);


    //clear timer 1A flag
    TIMER1_ICR_R |= (0x01);
    //Timer 0B disabled
    TIMER0_CTL_R &= ~(0x01 << 8);
    //Timer 1A disabled
    TIMER1_CTL_R &= ~(0x01 << 0);

    GPIO_PORTF_AFSEL_R &= ~(0x01 << 1);
    GPIO_PORTF_DIR_R |= (0x02);
    GPIO_PORTF_DATA_R = 0;

    //reload value in the timers
    TIMER0_TBILR_R = 0xFFFF;
    TIMER0_TBPR_R = 0xFF;
    TIMER1_TAILR_R = PERIOD * CLK_FREQ;
}
