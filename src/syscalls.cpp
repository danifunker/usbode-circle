#include <unistd.h>

extern "C" {

int _getentropy(void *buffer, size_t length)
{
    // This is a stub implementation.
    // A real implementation would need to get entropy from a hardware source.
    return 0;
}

}
