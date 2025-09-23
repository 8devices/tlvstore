#ifndef __LOG_H
#define __LOG_H

#include <stdio.h>

#ifdef DEBUG
#define ldebug(fmt, ...) fprintf(stderr, "DEBUG: " fmt "\n", ##__VA_ARGS__)
#else
#define ldebug(fmt, ...) ((void)0)
#endif

#define linfo(fmt, ...) fprintf(stderr, "INFO: " fmt "\n", ##__VA_ARGS__)
#define lwarning(fmt, ...) fprintf(stderr, "WARNING: " fmt "\n", ##__VA_ARGS__)
#define lerror(fmt, ...) fprintf(stderr, "ERROR: " fmt "\n", ##__VA_ARGS__)

#endif /* __LOG_H */
