#include "divisible.h"

bool divisible(uint8_t data, uint8_t div)
{
    if(data%div == 0)
        return true;
    else
        return false;
}
