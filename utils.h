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

char *bcopy_text(char *src, size_t len);
int bempty_data(void *data, size_t size);

#endif /* __STORAGE_UTILS_H */
