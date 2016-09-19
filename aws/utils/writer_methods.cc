#include "writer_methods.h"

#include <unistd.h>

#define DISPLAY_SIZE 21

namespace aws {
namespace utils {
namespace writer {


void write_number(uint64_t number) {
    char display[DISPLAY_SIZE];
    for (int i = 0; i < DISPLAY_SIZE; ++i) {
        display[i] = ' ';
    }
    size_t pos = DISPLAY_SIZE - 1;
    do {
        uint64_t tail = number % 10;
        display[pos--] = (char) (tail + '0');
        number = number / 10;
    } while (number > 0 && pos > 0);
    size_t offset = pos + 1;
    char* head = display + offset;
    size_t len = DISPLAY_SIZE - offset;
    write(STDERR_FILENO, head, len);
}

void write_number_checked(int number, const char *error_message, size_t error_message_size) {
    if (number <= 0) {
        write(STDERR_FILENO, error_message, error_message_size);
    } else {
        write_number((uint64_t) number);
    }
}




void write_pointer(void *pointer) {
    if (pointer == NULL) {
        WRITE_MESSAGE("NULL")
    } else {
        write_number((uint64_t) pointer);
    }
}

void write_message(const char* message, size_t size) {
    write(STDERR_FILENO, message, size);
}

}
}
}
