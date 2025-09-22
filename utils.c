#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

void *afread(const char *file_name, size_t *file_size)
{
	struct stat st;
	FILE *fp;
	void *fdata;
	ssize_t fsize;

	fp = fopen(file_name, "rb");
	if (!fp) {
		perror("fopen() failed");
		return NULL;
	}

	if (fstat(fileno(fp), &st) != 0) {
		perror("fstat() failed");
		fclose(fp);
		return NULL;
	}

	fsize = st.st_size;
	fdata = malloc(fsize);
	if (!fdata) {
		perror("malloc() failed");
		fclose(fp);
		return NULL;
	}

	if (fread(fdata, 1, fsize, fp) != fsize) {
		perror("fread() failed");
		fclose(fp);
		free(fdata);
		return NULL;
	}

	fclose(fp);

	*file_size = fsize;
	return fdata;
}

ssize_t afwrite(const char *file_name, void *data, size_t size)
{
	FILE *fp;
	ssize_t cnt;

	fp = fopen(file_name, "wb");
	if (!fp) {
		perror("fopen() failed");
		return -1;
	}

	cnt = fwrite(data, 1, size, fp);
	if (cnt != size) {
		perror("fwrite() failed");
		fclose(fp);
		return -1;
	}

	fclose(fp);

	return cnt;
}

ssize_t acopy_text(void **data_out, void *data_in, size_t size_in)
{
	if (!data_out)
		return size_in;

	*data_out = malloc(size_in + 1);
	if (!*data_out)
		return -1;

	memcpy(*data_out, data_in, size_in);
	((char *)*data_out)[size_in] = '\0';
	return size_in + 1;
}

ssize_t acopy_data(void **data_out, void *data_in, size_t size_in)
{
	if (!data_out)
		return size_in;

	*data_out = malloc(size_in);
	if (!*data_out)
		return -1;

	memcpy(*data_out, data_in, size_in);
	return size_in;
}

ssize_t aparse_byte_triplet(void **data_out, void *data_in, size_t size_in)
{
	unsigned char *buf;
	unsigned short year;
	unsigned char month, day;

	if (sscanf(data_in, "%hu-%hhu-%hhu", &year, &month, &day) < 3)
		return -1;

	/* Support both 2-digit and 4-digit year formats */
	if (year >= 2000)
		year -= 2000;
	else if (year > 0xff)
		return -1;

	if (!data_out)
		return 3;

	buf = malloc(3);
	if (!buf)
		return -1;

	buf[0] = (unsigned char)year;
	buf[1] = month;
	buf[2] = day;
	*data_out = buf;
	return 3;
}

ssize_t aformat_byte_triplet(void **data_out, void *data_in, size_t size_in)
{
#define DATE_STR_LEN 11
	unsigned char *buf;
	char *line;
	int cnt;

	if (!data_out)
		return DATE_STR_LEN;

	line = malloc(DATE_STR_LEN);
	if (!line)
		return -1;

	buf = data_in;
	cnt = snprintf(line, DATE_STR_LEN, "%04u-%02u-%02u", buf[0] + 2000, buf[1], buf[2]);
	*data_out = line;
	return cnt;
}

ssize_t aparse_mac_address(void **data_out, void *data_in, size_t size_in)
{
	char *buf;
	unsigned char oct[6];
	int cnt, len = sizeof(oct);

	cnt = sscanf(data_in, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		     &oct[0], &oct[1], &oct[2], &oct[3], &oct[4], &oct[5]);
	if (cnt < len)
		return -1;

	if (!data_out)
		return len;

	buf = malloc(len);
	if (!buf)
		return -1;

	memcpy(buf, oct, len);

	*data_out = buf;
	return len;
}

ssize_t aformat_mac_address(void **data_out, void *data_in, size_t size_in)
{
#define MAC_STR_LEN 18
	unsigned char *buf = data_in;
	char *line;
	int cnt;

	if (!data_out)
		return MAC_STR_LEN;

	line = malloc(MAC_STR_LEN);
	if (!line)
		return -1;

	cnt = snprintf(line, MAC_STR_LEN, "%02x:%02x:%02x:%02x:%02x:%02x",
		       buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
	*data_out = line;
	return cnt;
}

char *bcopy_text(char *src, size_t len)
{
	static char *buf;
	static int siz;
	char *dst, *tmp;
	int i;

	if (siz <= len) {
		tmp = realloc(buf, len + 1);
		if (!tmp)
			return NULL;
		buf = tmp;
		siz = len + 1;
	}

	for (i = 0; i < len && src[i] != '\0'; i++)
		dst[i] = src[i];
	dst[i] = '\0';

	return buf;
}

int bempty_data(void *data, size_t size)
{
	unsigned char *ptr = data;

	while (size > 0 && *ptr == 0xFF) {
		ptr++;
		size--;
	}
	return size == 0;
}
