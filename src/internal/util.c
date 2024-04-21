#include <stddef.h>

size_t j9660_util_first_pow2(size_t capacity)
{
    for(size_t power = 0; power <= 24; ++power)
    {
        if(((size_t)1 << power) >= capacity)
            return ((size_t)1 << power);
    }

    return capacity;
}
