#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RESET "\x1B[0m"
#define RED "\x1B[31m"
#define GREEN "\x1B[32m"
#define YELLOW "\x1B[33m"
#define BLUE "\x1B[34m"
#define MAGENTA "\x1B[35m"
#define CYAN "\x1B[36m"
#define WHITE "\x1B[37m"
#define BOLD "\x1B[1m"
#define BRIGHT_YELLOW "\x1B[93m"
#define BRIGHT_BLUE "\x1B[94m"

extern char current_user[50];
extern int is_admin;

void print_header(const char* title);
void log_action(const char* username, const char* action);
void trim_newline(char* str);

#endif
