
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "log.h"
#include "device.h"

void storage_close(struct storage_device *dev)
{
	fsync(dev->fd);
	if (munmap(dev->base, dev->size))
		perror("munmap() failed");
	close(dev->fd);
}

struct storage_device *storage_open(const char *file_name, int pref_size)
{
	struct storage_device *dev;
	struct stat fs;
	int fd = -1;
	int file_init, file_size, last_size;

	ldebug("Opening storage memory file %s, preferred size: %d", file_name, pref_size);

	dev = malloc(sizeof(*dev));
	if (!dev) {
		perror("malloc() failed");
		return NULL;
	}

	file_init = access(file_name, F_OK);

	fd = open(file_name, O_CREAT|O_SYNC|O_RDWR, 0644);
	if (fd == -1) {
		perror("open() failed");
		goto fail;
	}

	if (fstat(fd, &fs)) {
		perror("fstat() failed");
		goto fail;
	}

	file_size = fs.st_size;
	if (pref_size) {
		if (ftruncate(fd, pref_size))
			perror("ftruncate() failed");
		else
			file_size = pref_size;
	}

	if (!file_size) {
		fprintf(stderr, "Invalid storage size\n");
		goto fail;
	}

	dev->fd = fd;
	dev->size = file_size;
	dev->base = mmap(NULL, file_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (dev->base == MAP_FAILED) {
		perror("mmap() failed");
		goto fail;
	}

	last_size = fs.st_size;
	while (last_size < pref_size)
		((char *)dev->base)[last_size++] = 0xFF;

	return dev;
fail:
	if (dev)
		free(dev);
	if (fd != -1)
		close(fd);
	if (file_init)
		unlink(file_name);
	return NULL;
}
