#include <errno.h>

extern "C" {

int _getentropy(void *buffer, size_t length) {
    errno = EIO;
    return -1;
}

}
