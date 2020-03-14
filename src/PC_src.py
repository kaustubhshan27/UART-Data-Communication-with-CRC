import serial
import time
import threading

#crc8 polynomial used
polynomial = 0xE7
#to count number of bytes read
byte_count = 0
#to know when to start/stop bps_calculation() function
bps_calculation_status = 'start'

def crc8_checkvalue_generation(message):
    crc = message
    for bit in range(0, 8):
        if ((crc & 0x80) != 0):
            crc = ((crc << 1) ^ polynomial)
        else:
            crc = crc << 1
    return (crc & 0xFF)


def crc8_verification(message):
    crc = 0
    for index in range(0, 2):
        crc = crc ^ message[index]
        for bit in range(0, 8):
            if ((crc & 0x80) != 0):
                crc = ((crc << 1) ^ polynomial)
            else:
                crc = crc << 1
    return (crc & 0xFF)


#this function is called every 0.5secs
def bps_calculation():
    global byte_count
    global bps_calculation_status
    #total number of bytes received since last call = number of bytes read + number of bytes currently in input buffer
    bps_value = float((byte_count + serialPort.in_waiting)*10)/1
    byte_count = 0
    if(bps_calculation_status == 'start'):
        print('Transmission Speed (bits/sec) = ', bps_value)
        threading.Timer(1, bps_calculation).start()

    
#initializing and opening serial port
with serial.Serial('COM3', baudrate=2400, bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE, timeout=2) as serialPort:
    with open('data.txt', 'r') as dataFile:
        print('Sending data to uC...')
        

#Transmitting data to uC
        char_from_file = dataFile.read(1)
        checkvalue = crc8_checkvalue_generation(ord(char_from_file)).to_bytes(1, 'little')
        #converting string to byte to send over serial
        char_to_send = char_from_file.encode('utf-8')

        #status_flag - feedback being sent from uC
        status_flag = '0'
        while (status_flag == '0'):
            serialPort.write(char_to_send)
            serialPort.write(checkvalue)
            #status_flag='0', failed transmission. Retransmit data
            #status_flag='1', successful transmission. Transmit next character
            status_flag = serialPort.read(1).decode('ascii')
			
        #at the end of the file "dataFile.read(1)" will return an empty string 
        while (len(char_from_file) > 0):
            char_from_file = dataFile.read(1)            
            if (len(char_from_file) > 0):
                #converting string to byte to send over serial
                char_to_send = char_from_file.encode('utf-8')
                checkvalue = crc8_checkvalue_generation(ord(char_from_file)).to_bytes(1, 'little')
                #status_flag - feedback being sent from uC 
                status_flag = '0'
                while (status_flag == '0'):
                    serialPort.write(char_to_send)
                    serialPort.write(checkvalue)
                    #status_flag='0', failed transmission. Retransmit data
                    #status_flag='1', successful transmission. Transmit next character
                    status_flag = serialPort.read(1).decode('ascii')
				
        #for end of file indication. Sending NULL character
        checkvalue = crc8_checkvalue_generation(ord('\0')).to_bytes(1, 'little')
        #status_flag - feedback being sent from uC 
        status_flag = '0'
        while (status_flag == '0'):
                serialPort.write(b'\0')
                serialPort.write(checkvalue)
                #status_flag='0', failed transmission. Retransmit data
                #status_flag='1', successful transmission. Transmit next character
                status_flag = serialPort.read(1).decode('ascii')
        print('\nTransmission DONE!')
        


#Receiving data from uC
        print('\nReceiving data back from uC...\n')
        received_data = []
        bps_calculation()
        char_received = serialPort.read(1).decode('ascii')
        byte_count = byte_count + 1
        checkvalue = ord(serialPort.read(1))
        byte_count = byte_count + 1
        message = [ord(char_received), checkvalue]

        #verifying data sent from uC
        while(crc8_verification(message) != 0x00):
            #status_flag='0', failed transmission
            status_flag = '0'
            #status_flag sent to PC as feedback
            serialPort.write(status_flag.encode('utf-8'))
            #character and checkvalue retransmitted from uC
            char_received = serialPort.read(1).decode('ascii')
            byte_count = byte_count + 1
            checkvalue = ord(serialPort.read(1))
            byte_count = byte_count + 1
            message = [ord(char_received), checkvalue]

        #status_flag='1', successful transmission. Print on console    
        #print(char_received, end='')
        received_data.append(char_received)
        status_flag = '1'
        serialPort.write(status_flag.encode('utf-8'))
		
        while (char_received != '\0'):
            char_received = serialPort.read(1).decode('ascii')
            byte_count = byte_count + 1
            checkvalue = ord(serialPort.read(1))
            byte_count = byte_count + 1
            message = [ord(char_received), checkvalue]
            #verifying data sent from uC
            while(crc8_verification(message) != 0x00):
                #status_flag='0', failed transmission
                status_flag = '0'
                #status_flag sent to PC as feedback
                serialPort.write(status_flag.encode('utf-8'))
                #character and checkvalue retransmitted from uC
                char_received = serialPort.read(1).decode('ascii')
                byte_count = byte_count + 1
                checkvalue = ord(serialPort.read(1))
                byte_count = byte_count + 1
                message = [ord(char_received), checkvalue]
            #status_flag='1', successful transmission. Print on console    
            #print(char_received, end='')
            received_data.append(char_received)
            status_flag = '1'
            #status_flag sent to PC as feedback
            serialPort.write(status_flag.encode('utf-8'))

        #stop the periodic bps_calculation function. Now print content received from uC
        bps_calculation_status = 'stop'
        
        print('\n\nReceiving DONE!\n')

        #printing received data on the console
        print('\nData Received:\n')
        for ch in received_data:
            print(ch, end='')
        


print('\n\n\n\t\t\t\t\t\t\t\t\t\tTHE END')