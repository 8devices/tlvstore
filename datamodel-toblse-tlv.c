/* TobuFi legacy sys-eeprom TLV datamodel */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <arpa/inet.h>
#ifdef HAVE_LZMA_H
#include <lzma.h>
#endif

#include "crc.h"
#include "log.h"
#include "utils.h"
#include "device.h"
#include "tlv.h"
#include "protocol.h"

#define EEPROM_MAGIC	"TLVeppr"
#define EEPROM_VERSION	0x01

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

enum tlv_code {
	/* Product related attributes */
	EEPROM_ATTR_PRODUCT_ID = 1,		/* char*, "ProductXYZ" */
	EEPROM_ATTR_SERIAL_NO,              	/* char*, "" */

	/* PCB related attributes */
	EEPROM_ATTR_PCB_NAME = 16,         	/* char*, "FooBar" */
	EEPROM_ATTR_PCB_REVISION,           	/* char*, "0002" */
	EEPROM_ATTR_PCB_PRDATE,             	/* time_t, seconds */
	EEPROM_ATTR_PCB_PRLOCATION,         	/* char*, "Kaunas" */
	EEPROM_ATTR_PCB_SN,                	/* char*, "" */

	/* MAC infoformation */
	EEPROM_ATTR_MAC = 224,
	EEPROM_ATTR_MAC_FIRST = 224,
	EEPROM_ATTR_MAC_1 = 224,
	EEPROM_ATTR_MAC_2,
	EEPROM_ATTR_MAC_3,
	EEPROM_ATTR_MAC_4,
	EEPROM_ATTR_MAC_5,
	EEPROM_ATTR_MAC_6,
	EEPROM_ATTR_MAC_7,
	EEPROM_ATTR_MAC_8,
	EEPROM_ATTR_MAC_9,
	EEPROM_ATTR_MAC_10,
	EEPROM_ATTR_MAC_11,
	EEPROM_ATTR_MAC_12,
	EEPROM_ATTR_MAC_13,
	EEPROM_ATTR_MAC_14,
	EEPROM_ATTR_MAC_15,
	EEPROM_ATTR_MAC_16 = 239,
	EEPROM_ATTR_MAC_LAST = 239,

	/* Calibration data */
	EEPROM_ATTR_RADIO_CALIBRATION_DATA = 240,
	EEPROM_ATTR_XTAL_CALIBRATION_DATA,
};

enum tlv_spec {
	INPUT_SPEC_NONE,
	INPUT_SPEC_TXT,
	INPUT_SPEC_BIN,
};

struct tlv_code_desc {
	enum tlv_code m_code;
	char *m_name;
	enum tlv_spec m_spec;
};

const struct tlv_code_desc tlv_code_list[] = {
	{ EEPROM_ATTR_PRODUCT_ID, "PRODUCT_ID", INPUT_SPEC_TXT },
	{ EEPROM_ATTR_SERIAL_NO, "SERIAL_NO", INPUT_SPEC_TXT },
	{ EEPROM_ATTR_PCB_NAME, "PCB_NAME", INPUT_SPEC_TXT },
	{ EEPROM_ATTR_PCB_REVISION, "PCB_REVISION", INPUT_SPEC_TXT },
	{ EEPROM_ATTR_PCB_PRDATE, "PCB_PROD_DATE", INPUT_SPEC_TXT },
	{ EEPROM_ATTR_PCB_PRLOCATION, "PCB_PROD_LOCATION", INPUT_SPEC_TXT },
	{ EEPROM_ATTR_PCB_SN, "PCB_SN", INPUT_SPEC_TXT },
	{ EEPROM_ATTR_RADIO_CALIBRATION_DATA, "RADIO_CALIBRATION_DATA", INPUT_SPEC_BIN },
	{ EEPROM_ATTR_XTAL_CALIBRATION_DATA, "XTAL_CALIBRATION_DATA", INPUT_SPEC_BIN },
	{ EEPROM_ATTR_MAC, "GENERIC_MAC", INPUT_SPEC_TXT },
};

/*
 * EEPROM structure:
 *
 *                 MAGIC          VERSION  LENGTH     CRC
 *        |---------------------|  |---| |---------| |----
 * 00000  54 4c 56 65 70 70 72 00  00 01 00 00 00 09 bd 96  |TLVeppr.........|
 *        CRC  TYPE LEN.        DATA
 *        ----||--||---||-----------------|
 * 00010  8b e1 01 00 06 66 6f 6f  62 61 72 00 00 00 00 00  |.....foobar.....|
 * ...
 *
 */

struct __attribute__ ((__packed__)) eeprom_header {
	char magic[8];
	uint16_t version;
	uint32_t totallen;
	uint32_t crc32;
};

static int data_dump(const char *key, void *val, int len, enum tlv_spec type)
{
	int i;

	if (type == INPUT_SPEC_TXT) {
		printf("%s=%s\n", key, (char *)val);
	} else if (type == INPUT_SPEC_BIN) {
		printf("%s[%d] {", key, len);
		for (i = 0; i < len; i++) {
			if (!(i % 16)) {
				putchar('\n');
				putchar(' ');
			}
			printf("%02x ", 0xFF & ((char *)val)[i]);
		}
		puts("\n}");
	}
}

#ifdef HAVE_LZMA_H
static ssize_t decompress_bin(void **data_out, void *data_in, size_t size_in)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_ret ret;
	unsigned char *out_tmp, *out_buf;
	size_t out_next, out_size;
	size_t size_out = 0, size_inc = size_in * 4;

	out_size = size_inc;
	out_buf = malloc(out_size);
	if (!out_buf)
		return -1;

	ret = lzma_auto_decoder(&strm, UINT64_MAX, 0);
	if (ret != LZMA_OK) {
		ldebug("Failed to initialize LZMA decoder, error code: %d", ret);
		free(out_buf);
		return -1;
	}

	strm.next_in = data_in;
	strm.avail_in = size_in;
	strm.next_out = out_buf;
	strm.avail_out = out_size;

	while (1) {
		if (strm.avail_out == 0) {
			out_next = out_size + size_inc;
			out_tmp = realloc(out_buf, out_next);
			if (!out_tmp) {
				perror("realloc() failed");
				lzma_end(&strm);
				free(out_buf);
				return -1;
			}
			strm.next_out = out_tmp + out_size;
			strm.avail_out = size_inc;
			out_buf = out_tmp;
			out_size = out_next;
		}

		ret = lzma_code(&strm, LZMA_RUN);
		if (ret == LZMA_STREAM_END) {
			size_out = out_size - strm.avail_out;
			break;
		}
		if (ret != LZMA_OK) {
			/* Error occurred */
			ldebug("LZMA decompression error: %d", ret);
			lzma_end(&strm);
			free(out_buf);
			return -1;
		}
	}

	lzma_end(&strm);

	out_tmp = realloc(out_buf, size_out);
	if (!out_tmp) {
		free(out_buf);
		return -1;
	}

	*data_out = out_tmp;
	return size_out;
}
#else
static ssize_t decompress_bin(void **data_out, void *data_in, size_t size_in)
{
	void *out_buf;
	size_t out_size = size_in;

	out_buf = realloc(*data_out, out_size);
	if (!out_buf) {
		perror("realloc() failed");
		return -1;
	}
	memcpy(out_buf, data_in, size_in);

	*data_out = out_buf;
	return out_size;
}
#endif

static int toblse_tlv_prop_check(char *key, char *in)
{
	int i;

	/* Special case for MAC addresses with interface names */
	if (strncmp(key, "GENERIC_MAC_", 12) == 0) {
		return strlen(key) <= 12;
	}

	for (i = 0; i < ARRAY_SIZE(tlv_code_list); i++) {
		if (strcmp(key, tlv_code_list[i].m_name) == 0) {
			return 0;
		}
	}

	return 1;
}

static int toblse_tlv_prop_print_all(void *sp)
{
	struct tlv_store *tlvs = (struct tlv_store *)sp;
	struct tlv_iterator iter;
	struct tlv_field *tlv;
	enum tlv_spec spec;
	char *key = NULL, *val = NULL;
	char *cval = NULL;
	ssize_t cval_len;
	ssize_t key_len = 0, val_len = 0;
	size_t tmp_len, len;
	int i;
	int fail = 0;

	tlvs_iter_init(&iter, tlvs);

	while ((tlv = tlvs_iter_next(&iter)) != NULL) {
		len = ntohs(tlv->length);
		if (tlv->type >= EEPROM_ATTR_MAC_FIRST && tlv->type <= EEPROM_ATTR_MAC_LAST) {
			if (len <= 6) {
				lerror("Invalid MAC address length %zu", len);
				fail = 1;
				continue;
			}
			tmp_len = strlen("GENERIC_MAC_") + len - 6 + 1;
			key = realloc(key, tmp_len);
			if (!key) {
				perror("realloc() failed");
				fail = 1;
				continue;
			}
			key_len = tmp_len;
			snprintf(key, key_len, "GENERIC_MAC_%s", tlv->value + 6);

			val = realloc(val, 18);
			if (!val) {
				perror("realloc() failed");
				fail = 1;
				continue;
			}
			val_len = 18;
			sprintf(val, "%02X:%02X:%02X:%02X:%02X:%02X",
				(unsigned char)tlv->value[0], (unsigned char)tlv->value[1],
				(unsigned char)tlv->value[2], (unsigned char)tlv->value[3],
				(unsigned char)tlv->value[4], (unsigned char)tlv->value[5]);
			data_dump(key, val, val_len, INPUT_SPEC_TXT);
		} else if (tlv->type == EEPROM_ATTR_RADIO_CALIBRATION_DATA) {
			for (i = 0; i < ARRAY_SIZE(tlv_code_list); i++) {
				if (tlv->type == tlv_code_list[i].m_code) {
					if (key_len) {
						free(key);
						key_len = 0;
					}
					key = tlv_code_list[i].m_name;
					break;
				}
			}

			cval_len = decompress_bin((void *)&cval, tlv->value, len);
			if (cval_len < 0) {
				lerror("Failed to decompress caldata");
				continue;
			}

			data_dump(key, cval, cval_len, INPUT_SPEC_BIN);
			free(cval);
		} else {
			for (i = 0; i < ARRAY_SIZE(tlv_code_list); i++) {
				if (tlv->type == tlv_code_list[i].m_code) {
					if (key_len) {
						free(key);
						key_len = 0;
					}
					key = tlv_code_list[i].m_name;
					spec = tlv_code_list[i].m_spec;
					break;
				}
			}

			if (!key) {
				lerror("Unknown property type %i", tlv->type);
				fail = 1;
				continue;
			}

			val = realloc(val, len + 1);
			if (!val) {
				perror("realloc() failed");
				fail = 1;
				continue;
			}
			val_len = len + 1;

			if (spec == INPUT_SPEC_TXT) {
				strncpy(val, tlv->value, len);
				val[len] = 0;
			} else {
				memcpy(val, tlv->value, len);
			}
			data_dump(key, val, len, spec);
		}
	}

	if (key_len)
		free(key);
	if (val_len)
		free(val);

	return fail;
}

static int toblse_tlv_prop_print(void *sp, char *key, char *out)
{
	struct tlv_store *tlvs = (struct tlv_store *)sp;
	enum tlv_spec spec;
	char buf[getpagesize()];
	char *ifname, *val = NULL;
	size_t len;
	int i, type;

	if (!key)
		return toblse_tlv_prop_print_all(sp);

	if (strncmp(key, "GENERIC_MAC_", 12) == 0) {
		ifname = key + 12;

		/* Iterate through all MAC address properties */
		for (type = EEPROM_ATTR_MAC_FIRST; type <= EEPROM_ATTR_MAC_LAST; type++) {
			len = tlvs_get(tlvs, type, sizeof(buf), buf);
			if (len <= 6)
				return -1;

			if (strcmp(buf + 6, ifname))
				continue;

			spec = INPUT_SPEC_TXT;
			len = 18;
			val = realloc(val, len);
			if (!val) {
				perror("realloc() failed");
				return -1;
			}
			sprintf(val, "%02X:%02X:%02X:%02X:%02X:%02X",
				(unsigned char)buf[0], (unsigned char)buf[1],
				(unsigned char)buf[2], (unsigned char)buf[3],
				(unsigned char)buf[4], (unsigned char)buf[5]);
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(tlv_code_list); i++) {
			if (!strcmp(key, tlv_code_list[i].m_name)) {
				type = tlv_code_list[i].m_code;
				spec = tlv_code_list[i].m_spec;
				break;
			}
		}

		len = tlvs_get(tlvs, type, sizeof(buf), buf);
		if (len < 0)
			return -1;

		if (spec == INPUT_SPEC_TXT) {
			val = realloc(val, len + 1);
			if (!val) {
				perror("realloc() failed");
				return -1;
			}
			strncpy(val, buf, len);
		} else if (type == EEPROM_ATTR_RADIO_CALIBRATION_DATA) {
			len = decompress_bin((void *)&val, buf, len);
			if (len < 0) {
				lerror("Failed to decompress caldata");
				return -1;
			}
		} else {
			val = realloc(val, len);
			if (!val) {
				perror("realloc() failed");
				return -1;
			}
			memcpy(val, buf, len);
		}
	}

	if (!val) {
		lerror("Failed TLV property '%s' get", key);
		return 1;
	}

	if (out && out[0] == '@')
		afwrite(out + 1, val, len);
	else if (out)
		data_dump(out, val, len, spec);
	else
		data_dump(key, val, len, spec);

	free(val);

	return 0;
}

static void toblse_tlv_prop_list(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tlv_code_list); i++) {
		if (tlv_code_list[i].m_code == EEPROM_ATTR_MAC)
			printf("%s*\n", tlv_code_list[i].m_name);
		else
			printf("%s\n", tlv_code_list[i].m_name);
	}
}

static void toblse_tlv_free(void *sp)
{
	tlvs_free((struct tlv_store *)sp);
}

static void *toblse_tlv_init(struct storage_device *dev, int force)
{
	struct eeprom_header *hdr;
	struct tlv_store *tlvs;
	unsigned int crc;

	if (dev->size <= sizeof(*hdr)) {
		lerror("Storage is too small %zu/%zu", dev->size, sizeof(*hdr));
		return NULL;
	}

	hdr = dev->base;
	if (strncmp(hdr->magic, EEPROM_MAGIC, sizeof(hdr->magic)) ||
	    ntohs(hdr->version) != EEPROM_VERSION)
		return NULL;

	crc = crc_32((unsigned char *)dev->base + sizeof(*hdr), ntohl(hdr->totallen));
	if (crc != ntohl(hdr->crc32)) {
		lerror("Storage CRC validation failed (expected 0x%08x, got 0x%08x)", ntohl(hdr->crc32), crc);
		return NULL;
	}

	tlvs = tlvs_init(dev->base + sizeof(*hdr), dev->size - sizeof(*hdr));
	if (!tlvs)
		lerror("Failed to initialize TLV store");

	return tlvs;
}

static struct storage_protocol toblse_tlv_model = {
	.name = "toblse-tlv",
	.init = toblse_tlv_init,
	.free = toblse_tlv_free,
	.list = toblse_tlv_prop_list,
	.check = toblse_tlv_prop_check,
	.print = toblse_tlv_prop_print,
};

static void __attribute__((constructor)) toblse_tlv_register(void)
{
	eeprom_register(&toblse_tlv_model);
}
