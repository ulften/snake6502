/* Logging to the console (stderr), without printf */

#include <string.h>
#include <unistd.h>
#include "log.h"

void log_str(const char *s)
{
    write(STDERR_FILENO, s, strlen(s));
}

void log_error(const char *msg)
{
    log_str("ERROR: ");
    log_str(msg);
    log_str("\r\n");
}

void log_num(const char *label, unsigned n)
{
    char buf[6];
    unsigned char i = sizeof buf - 1;

    buf[i] = 0;
    do
    {
        buf[--i] = '0' + n % 10;
        n /= 10;
    } while (n);
    log_str(label);
    log_str("=");
    log_str(buf + i);
    log_str("\r\n");
}
