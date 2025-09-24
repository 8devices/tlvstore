#ifndef __STORAGE_DEVICE_H
#define __STORAGE_DEVICE_H

struct storage_device {
	int fd;
	void *base;
	size_t size;
	size_t offset;
	void *shadow;
	void *orig_base;
	size_t orig_size;
};

struct storage_device *storage_open(const char *file_name, int pref_size, int data_offset);
void storage_close(struct storage_device *dev);

#endif /* __STORAGE_DEVICE_H */
