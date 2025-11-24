/* MTD (Memory Technology Device) storage backend */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>
#include <limits.h>

#include "log.h"
#include "device.h"

static char *storage_resolve_device(const char *spec)
{
	static char path[PATH_MAX];
	FILE *fp;
	char line[256];
	int mtd_num;
	char mtd_name[128];
	const char *name;

	/* Resolve MTD device by part name */
	if (strncmp(spec, "part:", 5))
		return (char *)spec;

	name = spec + 5;

	ldebug("Trying to resolve '%s' MTD partition", name);

	fp = fopen("/proc/mtd", "r");
	if (!fp) {
		ldebug("Cannot open /proc/mtd to resolve MTD partition name");
		lerror("MTD partition '%s' not found", name);
		return NULL;
	}

	/* Header line */
	if (!fgets(line, sizeof(line), fp)) {
		fclose(fp);
		lerror("MTD partition '%s' not found", name);
		return NULL;
	}

	while (fgets(line, sizeof(line), fp)) {
		/* Format: mtd0: 00080000 00020000 "partition-name" */
		if (sscanf(line, "mtd%d: %*x %*x \"%127[^\"]\"", &mtd_num, mtd_name) == 2) {
			if (!strcmp(mtd_name, name)) {
				snprintf(path, sizeof(path), "/dev/mtd%d", mtd_num);
				linfo("Found MTD partition '%s' file path %s", name, path);
				fclose(fp);
				return path;
			}
		}
	}

	fclose(fp);
	lerror("MTD partition '%s' not found", name);
	return NULL;
}

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

	/* Seek to the actual device position (device offset + requested offset) */
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

	ldebug("Read %zd bytes from MTD device at offset %zu", total_read, offset);
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

	/* Seek to the actual device position (device offset + requested offset) */
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

	ldebug("Wrote %zd bytes to MTD device at offset %zu", total_written, offset);
	return total_written;
}

static int storage_erase(struct storage_device *dev, size_t offset)
{
	struct erase_info_user erase;

	if (offset >= dev->size) {
		lerror("Erase offset %zu exceeds storage size %zu", offset, dev->size);
		return -1;
	}

	if (offset + dev->ebsize > dev->size) {
		lerror("Erase region exceeds storage size");
		return -1;
	}

	erase.start = dev->offset + offset;
	erase.length = dev->ebsize;
	if (ioctl(dev->fd, MEMERASE, &erase) != 0) {
		lerror("Failed to erase MTD block %zu - %zu: %s",
		       offset + dev->offset,
		       offset + dev->offset + dev->ebsize,
		       strerror(errno));
		return -1;
	}

	ldebug("Erased MTD block %zu - %zu",
	       offset + dev->offset,
	       offset + dev->offset + dev->ebsize);
	return 0;
}

static void storage_sync(struct storage_device *dev)
{
	char *base_ptr = (char *)dev->base;
	char *shadow_ptr = (char *)dev->shadow;
	size_t block_offset, block_size;
	size_t nblocks_erased = 0, nblocks_written = 0;
	int i, nwritten;
	int need_erase, need_write;

	ldebug("Updating MTD storage data");

	for (block_offset = 0; block_offset < dev->size; block_offset += dev->ebsize) {
		block_size = (block_offset + dev->ebsize <= dev->size) ?
			dev->ebsize : (dev->size - block_offset);

		/* Check if erase is needed: (base & shadow) != base means bits need to change from 0 to 1 */
		need_erase = !shadow_ptr;
		need_write = !shadow_ptr;

		if (shadow_ptr) {
			need_erase = 0;
			need_write = 0;
			for (i = 0; i < block_size; i++) {
				if ((base_ptr[block_offset + i] & shadow_ptr[block_offset + i]) != base_ptr[block_offset + i]) {
					need_erase = 1;
					need_write = 1;
					break;
				}
				if (base_ptr[block_offset + i] != shadow_ptr[block_offset + i]) {
					need_write = 1;
				}
			}
		}

		if (need_erase) {
			ldebug("Block at offset %zu needs erase", block_offset);
			if (storage_erase(dev, block_offset) != 0) {
				lerror("Failed to erase block at offset %zu", block_offset);
				return;
			}
			nblocks_erased++;
		}

		if (need_write) {
			ldebug("Block at offset %zu needs write", block_offset);
			nwritten = storage_write(dev, &base_ptr[block_offset], block_size, block_offset);
			if (nwritten != block_size) {
				lerror("Failed to write block at offset %zu", block_offset);
				return;
			}
			nblocks_written++;
		}
	}

	if (nblocks_erased > 0 || nblocks_written > 0) {
		linfo("Update MTD blocks complete, erased %zu, written %zu",
		      nblocks_erased, nblocks_written);
		fsync(dev->fd);
	} else {
		ldebug("No MTD blocks changed, write-back skipped");
	}
}

void storage_close(struct storage_device *dev)
{
	storage_sync(dev);
	close(dev->fd);
	free(dev->base);
	if (dev->shadow)
		free(dev->shadow);
	free(dev);
}

struct storage_device *storage_open(const char *file_spec, int pref_size, int data_offset)
{
	struct storage_device *dev;
	struct mtd_info_user mtd_info;
	int fd = -1;
	int nbytes = 0;
	int device_size;
	int base_size, data_size;
	void *base = NULL;
	char *file_name;

	file_name = storage_resolve_device(file_spec);
	if (!file_name) {
		lerror("Failed to resolve storage device path");
		return NULL;
	}

	ldebug("Opening MTD storage device %s, data offset: %d", file_spec, data_offset);

	dev = malloc(sizeof(*dev));
	if (!dev) {
		perror("malloc() failed");
		return NULL;
	}
	memset(dev, 0, sizeof(*dev));

	fd = open(file_name, O_RDWR);
	if (fd == -1) {
		perror("open() failed");
		goto fail;
	}

	if (ioctl(fd, MEMGETINFO, &mtd_info) != 0) {
		lerror("MEMGETINFO ioctl failed: %s", strerror(errno));
		goto fail;
	}

	device_size = mtd_info.size;
	linfo("MTD device: size=%d, erasesize=%d, writesize=%d, type=%d",
	      mtd_info.size, mtd_info.erasesize, mtd_info.writesize, mtd_info.type);

	/* TODO: allow offset to non-EB boundary. This has usage restrictions,
	 * however it significantly simplifies data updates and extensions. */
	if (data_offset % mtd_info.erasesize != 0) {
		lerror("Storage offset %d is not aligned to erase block size %d",
		       data_offset, mtd_info.erasesize);
		goto fail;
	}

	if (pref_size && pref_size != device_size) {
		lwarning("MTD device size (%d) differs from requested size (%d), using device size",
			 device_size, pref_size);
	}

	/* Size parameters description:
	 * * pref_size -- ignored for MTD devices (device size is fixed)
	 * * device_size -- MTD device size from ioctl
	 * * base_size -- base storage size excluding the offset
	 * * data_size -- received storage data size, up to base_size
	 */
	base_size = device_size - data_offset;

	if (base_size <= 0) {
		lerror("Insufficient storage size after offset");
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
	dev->ebsize = mtd_info.erasesize;
	dev->base = base;
	dev->orig_base = NULL;
	dev->orig_size = 0;

	ldebug("Initial read MTD device data %d bytes", base_size);
	nbytes = storage_read(dev, dev->base, base_size, 0);
	if (nbytes == -1) {
		lerror("Failed to read initial MTD device data");
		goto fail;
	}

	data_size = nbytes;

	dev->shadow = malloc(base_size);
	if (!dev->shadow) {
		perror("malloc() failed for shadow buffer");
		/* Let's continue without smart write-back */
	} else {
		memcpy(dev->shadow, dev->base, base_size);
	}

	ldebug("MTD storage buffer initialized, size %d, data %d", base_size, data_size);

	return dev;
fail:
	if (dev->base)
		free(dev->base);
	if (dev->shadow)
		free(dev->shadow);
	free(dev);
	if (fd != -1)
		close(fd);
	return NULL;
}
