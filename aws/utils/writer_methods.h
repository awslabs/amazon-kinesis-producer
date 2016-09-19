#ifndef WRITER_METHODS_H
#define WRITER_METHODS_H

#include <cstdint>
#include <cstddef>

namespace aws {
namespace utils {
namespace writer {

void write_number_checked(int number, const char *error_message, size_t error_message_size);
void write_signal_descriptiong(int signal);
void write_pointer(void *pointer);
void write_number(uint64_t number);
void write_message(const char* message, size_t message_size);

}
}
}


#define WRITE_NUM_CHECKED(number, err_msg) ::aws::utils::writer::write_number_checked(number, err_msg, sizeof(err_msg) - 1);
#define WRITE_CODE(number) WRITE_NUM_CHECKED(number, "Code <= 0");
#define WRITE_POINTER(pointer) ::aws::utils::writer::write_pointer(pointer);
#define WRITE_NUMBER(number) ::aws::utils::writer::write_number(number);
#define WRITE_MESSAGE(message) ::aws::utils::writer::write_message(message, sizeof(message) - 1);

#endif // WRITER_METHODS_H
