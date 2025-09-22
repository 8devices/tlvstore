#ifndef __STORAGE_DEVICE_H
#define __STORAGE_DEVICE_H

struct storage_device {
	int fd;
	void *base;
	size_t size;
	void *shadow;
};

struct storage_device *storage_open(const char *file_name, int pref_size);
void storage_close(struct storage_device *dev);

#endif /* __STORAGE_DEVICE_H */
