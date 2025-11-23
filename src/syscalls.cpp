#include <circle/bcmrandom.h>
#include <unistd.h>
#include <string.h>

extern "C" {

int _getentropy(void *buffer, size_t length)
{
    CBcmRandomNumberGenerator rng;
    u8* p = (u8*)buffer;
    for (size_t i = 0; i < length; i += 4)
    {
        u32 random = rng.GetNumber();
        size_t to_copy = (length - i < 4) ? length - i : 4;
        memcpy(p + i, &random, to_copy);
    }
    return 0;
}

}
