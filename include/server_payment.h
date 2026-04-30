#ifndef SERVER_PAYMENT_H
#define SERVER_PAYMENT_H

#include <stddef.h>

int run_payment_process(const char* method, float amount, char* payment_status,
                        size_t payment_status_size);

#endif
