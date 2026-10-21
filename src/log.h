/* Logging to the console (stderr), without printf */

#ifndef LOG_H
#define LOG_H

void log_str(const char *s);

/* Writes "ERROR: msg" on a line of its own */
void log_error(const char *msg);

/* Writes "label=n" on a line of its own, e.g. log_num("score", score) */
void log_num(const char *label, unsigned n);

#endif
