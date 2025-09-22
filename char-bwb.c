
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "log.h"
#include "device.h"

static void storage_writeback(struct storage_device *dev)
{
	size_t chunk_start = 0, chunk_size = 0;
	int i, nchunk, ntotal = 0;
	char *base_ptr = (char *)dev->base;
	char *shadow_ptr = (char *)dev->shadow;

	ldebug("Attempting intelligent buffer write-back");

	for (i = 0; i <= dev->size; i++) {
		if (i < dev->size && (!dev->shadow || base_ptr[i] != shadow_ptr[i])) {
			if (chunk_size == 0) {
				chunk_start = i;
			}
			chunk_size++;
		} else {
			if (chunk_size > 0) {
				ldebug("Writing changed data chunk: offset %zu, size %zu", chunk_start, chunk_size);
				if (lseek(dev->fd, chunk_start, SEEK_SET) == -1) {
					perror("lseek() failed");
					return;
				}

				nchunk = write(dev->fd, &base_ptr[chunk_start], chunk_size);
				if (nchunk != chunk_size) {
					lerror("Failed to write changed data chunk: offset %zu, size %zu", chunk_start, chunk_size);
					return;
				}

				ntotal += nchunk;

				chunk_size = 0;
			}
		}
	}

	if (ntotal > 0) {
		linfo("Intelligent write-back complete: %d bytes written", ntotal);
		fsync(dev->fd);
	}
}

void storage_close(struct storage_device *dev)
{
	storage_writeback(dev);
	close(dev->fd);
	free(dev->base);
	if (dev->shadow)
		free(dev->shadow);
	free(dev);
}

struct storage_device *storage_open(const char *file_name, int pref_size)
{
	struct storage_device *dev;
	struct stat fs;
	int fd = -1;
	int nbytes = 0;
	int file_init, file_size;
	void *base = NULL;

	ldebug("Opening storage memory file %s, preferred size: %d", file_name, pref_size);

	dev = malloc(sizeof(*dev));
	if (!dev) {
		perror("malloc() failed");
		return NULL;
	}

	file_init = access(file_name, F_OK);

	fd = open(file_name, O_CREAT|O_RDWR, 0644);
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
		lerror("Invalid storage size");
		goto fail;
	}

	base = malloc(file_size);
	if (!base) {
		perror("malloc() failed for storage buffer");
		goto fail;
	}

	dev->fd = fd;
	dev->size = file_size;
	dev->base = base;

	/* Read entire content into RAM buffer */
	if (fs.st_size) {
		ldebug("Reading %d bytes from storage into buffer", (int)fs.st_size);
		nbytes = read(fd, dev->base, fs.st_size);
		if (nbytes == -1) {
			perror("read() failed");
			goto fail;
		} else if (nbytes < fs.st_size) {
			linfo("Short read %d/%d storage memory", nbytes, (int)fs.st_size);
		}
	}

	while (nbytes < file_size)
		((char *)dev->base)[nbytes++] = 0xFF;


	/* Initial synchronise file storage  */
	if (fs.st_size < file_size) {
		nbytes = write(dev->fd, dev->base + fs.st_size, file_size - fs.st_size);
		if (nbytes != file_size - fs.st_size)
			lerror("Short write %d/%d initial storage buffer",
			       nbytes, file_size - (int)fs.st_size);
	}

	/* Duplicate of initial data for smart write-back */
	dev->shadow = malloc(file_size);
	if (!dev->shadow) {
		perror("malloc() failed shadow buffer");
		/* Let's continue without smart write-back */
	} else {
		memcpy(dev->shadow, dev->base, file_size);
	}

	ldebug("Storage buffer initialized, size %d, data %d", file_size, (int)fs.st_size);

	return dev;
fail:
	free(dev);
	if (base)
		free(base);
	if (fd != -1)
		close(fd);
	if (file_init)
		unlink(file_name);
	return NULL;
}
