#ifndef __LOG_H
#define __LOG_H

#include <stdio.h>

#define LOG_DEBUG 0
#define LOG_INFO 1
#define LOG_WARNING 2
#define LOG_ERROR 3

extern int g_log_level;

#if defined(DEBUG_STRIP)
#define ldebug(fmt, ...) ((void)0)
#else
#define ldebug(fmt, ...) \
	do { if (g_log_level <= LOG_DEBUG) fprintf(stderr, "DEBUG: " fmt "\n", ##__VA_ARGS__); } while(0)
#endif

#define linfo(fmt, ...) \
	do { if (g_log_level <= LOG_INFO) fprintf(stderr, "INFO: " fmt "\n", ##__VA_ARGS__); } while(0)

#define lwarning(fmt, ...) \
	do { if (g_log_level <= LOG_WARNING) fprintf(stderr, "WARNING: " fmt "\n", ##__VA_ARGS__); } while(0)

#define lerror(fmt, ...) \
	do { if (g_log_level <= LOG_ERROR) fprintf(stderr, "ERROR: " fmt "\n", ##__VA_ARGS__); } while(0)

#endif /* __LOG_H */
