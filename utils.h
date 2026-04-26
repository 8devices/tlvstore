#ifndef __STORAGE_UTILS_H
#define __STORAGE_UTILS_H

#include <sys/types.h>

void *afread(const char *file_name, size_t *file_size);
ssize_t afwrite(const char *file_name, void *data, size_t size);

ssize_t acopy_text(void **data_out, void *data_in, size_t size_in);
ssize_t acopy_data(void **data_out, void *data_in, size_t size_in);
ssize_t aparse_byte_triplet(void **data_out, void *data_in, size_t size_in);
ssize_t aformat_byte_triplet(void **data_out, void *data_in, size_t size_in);
ssize_t aparse_mac_address(void **data_out, void *data_in, size_t size_in);
ssize_t aformat_mac_address(void **data_out, void *data_in, size_t size_in);

/* Compress/decompress arbitrary binary data using LZMA. When the build
 * was configured without liblzma the stubs simply return -1. */
#ifdef HAVE_LZMA_H
ssize_t acompress_data(void **data_out, void *data_in, size_t size_in);
ssize_t adecompress_data(void **data_out, void *data_in, size_t size_in);
#else
static inline ssize_t acompress_data(void **data_out, void *data_in, size_t size_in) { return -1; }
static inline ssize_t adecompress_data(void **data_out, void *data_in, size_t size_in) { return -1; }
#endif

char *bcopy_text(char *src, size_t len);
int bempty_data(void *data, size_t size);

#endif /* __STORAGE_UTILS_H */
