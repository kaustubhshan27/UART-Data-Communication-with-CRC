#include "buffer.h"

//Size of buffer
#define BUFFER_SIZE 50
//Circular buffer EMPTY condition
#define BUFF_EMPTY  0
//Circular buffer FULL condition
#define BUFF_FULL   1
//Circular buffer NOT FULL condition
#define BUFF_NOT_FULL   2

void buffer_init(struct Buffer *buff, uint8_t *buff_name)
{
    buff->front = 0;
    buff->rear = 0;
    buff->arr = buff_name;
}

void buffer_add(struct Buffer *buff, uint8_t data)
{
    buff->rear = (buff->rear + 1) % BUFFER_SIZE;
    buff->arr[buff->rear] = data;
}

uint8_t buffer_get(struct Buffer *buff)
{
    buff->front = (buff->front + 1) % BUFFER_SIZE;
    return buff->arr[buff->front];
}

uint8_t buffer_space(struct Buffer *buff)
{
    //the array index where "front" points to is always empty. Therefore, effective buffer space = BUFFER_SIZE - 1
    if(buff->front == (buff->rear + 1) % BUFFER_SIZE)//buffer is full
        return BUFF_FULL;
    else if(buff->rear == buff->front)//buffer is empty
        return BUFF_EMPTY;
    else
        return BUFF_NOT_FULL;
}

