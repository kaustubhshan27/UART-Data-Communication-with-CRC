#ifndef _BUFF_H_
#define _BUFF_H_

#include <stdint.h>

struct Buffer
{
    uint8_t front;
    uint8_t rear;
    uint8_t *arr;
};

void buffer_init(struct Buffer *buff, uint8_t *buff_name);
void buffer_add(struct Buffer *buff, uint8_t data);
uint8_t buffer_get(struct Buffer *buff);
uint8_t buffer_space(struct Buffer *buff);

#endif //_BUFF_H_
