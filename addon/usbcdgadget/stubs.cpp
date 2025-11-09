#include <unistd.h>

extern "C" {

int _getentropy(void *buffer, size_t length) {
    return -1;
}

}
