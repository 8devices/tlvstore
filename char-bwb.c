
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "log.h"
#include "device.h"

static ssize_t storage_read(struct storage_device *dev, void *buf, size_t count, size_t offset)
{
	ssize_t bytes_read;
	ssize_t total_read = 0;

	if (offset >= dev->size) {
		lerror("Read offset %zu exceeds storage size %zu", offset, dev->size);
		return -1;
	}

	if (offset + count > dev->size) {
		lwarning("Read count truncated to %zu bytes", count);
		count = dev->size - offset;
	}

	/* Seek to the actual file position (device offset + requested offset) */
	if (lseek(dev->fd, dev->offset + offset, SEEK_SET) == -1) {
		lerror("lseek() failed to offset %zu: %s", dev->offset + offset, strerror(errno));
		return -1;
	}

	while (total_read < count) {
		bytes_read = read(dev->fd, (char *)buf + total_read, count - total_read);
		if (bytes_read == -1) {
			lerror("read() failed %zu bytes at offset %zu: %s", count - total_read,
			       dev->offset + offset + total_read, strerror(errno));
			break;
		} else if (bytes_read == 0) {
			break;
		}

		total_read += bytes_read;
	}

	if (bytes_read == -1 && total_read == 0) {
		lerror("read() failed %zu bytes at offset %zu", count, offset);
		return -1;
	}

	ldebug("Read %zd bytes from file at offset %zu", total_read, offset);
	return total_read;
}

static ssize_t storage_write(struct storage_device *dev, const void *buf, size_t count, size_t offset)
{
	ssize_t bytes_written;
	ssize_t total_written = 0;

	if (offset >= dev->size) {
		lerror("Write offset %zu exceeds storage size %zu", offset, dev->size);
		return -1;
	}

	if (offset + count > dev->size) {
		lwarning("Write count truncated to %zu bytes", count);
		count = dev->size - offset;
	}

	/* Seek to the actual file position (device offset + requested offset) */
	if (lseek(dev->fd, dev->offset + offset, SEEK_SET) == -1) {
		lerror("lseek() failed to offset %zu: %s", dev->offset + offset, strerror(errno));
		return -1;
	}

	while (total_written < count) {
		bytes_written = write(dev->fd, (const char *)buf + total_written, count - total_written);
		if (bytes_written == -1) {
			lerror("write() failed %zu bytes at offset %zu: %s", count - total_written,
			       dev->offset + offset + total_written, strerror(errno));
			break;
		} else if (bytes_written == 0) {
			break;
		}

		total_written += bytes_written;
	}

	if (bytes_written == -1 && total_written == 0) {
		lerror("write() failed %zu bytes at offset: %zu", count, offset);
		return -1;
	}

	if (total_written != (ssize_t)count) {
		lerror("Short write %zd / %zu bytes at offset %zu", total_written, count, offset);
	}

	ldebug("Wrote %zd bytes to file at offset %zu", total_written, offset);
	return total_written;
}

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
				nchunk = storage_write(dev, &base_ptr[chunk_start], chunk_size, chunk_start);
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

struct storage_device *storage_open(const char *file_name, int pref_size, int data_offset)
{
	struct storage_device *dev;
	struct stat fs;
	int fd = -1;
	int nbytes = 0;
	int file_init, file_size;
	int base_size, data_size;
	void *base = NULL;

	ldebug("Opening storage memory file %s, preferred size: %d, offset: %d", file_name, pref_size, data_offset);

	dev = malloc(sizeof(*dev));
	if (!dev) {
		perror("malloc() failed");
		return NULL;
	}
	memset(dev, 0, sizeof(*dev));

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

	/* Size parameters description:
	 * * pref_size -- preferred storage size, required only when initialising storage
	 * * file_size -- full storage file size without excluding the offset
	 * * base_size -- base storage size excluding the offset
	 * * data_size -- received storage data size, up to base_size
	 */
	base_size = file_size - data_offset;

	if (base_size <= 0) {
		lerror("Insufficient storage size");
		goto fail;
	}

	base = malloc(base_size);
	if (!base) {
		perror("malloc() failed for storage buffer");
		goto fail;
	}

	dev->fd = fd;
	dev->size = base_size;
	dev->offset = data_offset;
	dev->base = base;
	dev->orig_base = NULL;
	dev->orig_size = 0;

	/* Read the storage contents when storage file has valid size */
	if (fs.st_size > data_offset) {
		/* Best-effort initial read storage data, storage may be
		 * shorter or larger depending on prefered size argument. */
		ldebug("Initial read storage file data %d/%d bytes", base_size, file_size);
		nbytes = storage_read(dev, dev->base, base_size, 0);
		if (nbytes == -1) {
			lerror("Failed to read initial storage file data");
			goto fail;
		}
	}

	data_size = nbytes;

	/* Backfill storage and sync the back if necessary */
	memset(dev->base + data_size, 0xFF, base_size - data_size);

	if (data_size < base_size) {
		ldebug("Initial storage file data update %d/%d bytes", data_size, base_size);
		nbytes = storage_write(dev, dev->base + data_size, base_size - data_size, data_size);
		if (nbytes != (base_size - data_size)) {
			lerror("Failed to update initial storage file data");
		}
	}

	/* Duplicate of initial data for smart write-back */
	dev->shadow = malloc(base_size);
	if (!dev->shadow) {
		perror("malloc() failed shadow buffer");
		/* Let's continue without smart write-back */
	} else {
		memcpy(dev->shadow, dev->base, base_size);
	}

	ldebug("Storage buffer initialized, size %d, data %d", base_size, data_size);

	return dev;
fail:
	if (dev->base)
		free(dev->base);
	if (dev->shadow)
		free(dev->shadow);
	free(dev);
	if (fd != -1)
		close(fd);
	if (file_init)
		unlink(file_name);
	return NULL;
}
