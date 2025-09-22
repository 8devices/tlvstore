#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "crc.h"

#include "log.h"
#include "utils.h"
#include "device.h"
#include "protocol.h"
#include "datamodel-firmux-fields.h"

#define EEPROM_MAGIC "FXDMFLD1"

static int dirty;

static int data_dump(const char *key, void *val, int len)
{
	printf("%s=%s\n", key, (char *)val);
}

#define propsizeof(type, member) (sizeof(((type *)0)->member))
static struct firmux_property plain_props[] = {
	{ "PRODUCT_ID", offsetof(struct firmux_fields, product_id), propsizeof(struct firmux_fields, product_id), acopy_text, acopy_text },
	{ "PRODUCT_NAME", offsetof(struct firmux_fields, product_name), propsizeof(struct firmux_fields, product_name), acopy_text, acopy_text },
	{ "SERIAL_NO", offsetof(struct firmux_fields, serial_no), propsizeof(struct firmux_fields, serial_no), acopy_text, acopy_text },
	{ "PCB_NAME", offsetof(struct firmux_fields, pcb_name), propsizeof(struct firmux_fields, pcb_name), acopy_text, acopy_text },
	{ "PCB_REVISION", offsetof(struct firmux_fields, pcb_revision), propsizeof(struct firmux_fields, pcb_revision), acopy_text, acopy_text },
	{ "PCB_PRDATE", offsetof(struct firmux_fields, pcb_prdate), propsizeof(struct firmux_fields, pcb_prdate), aparse_byte_triplet, aformat_byte_triplet },
	{ "PCB_PRLOCATION", offsetof(struct firmux_fields, pcb_prlocation), propsizeof(struct firmux_fields, pcb_prlocation), acopy_text, acopy_text },
	{ "PCB_SN", offsetof(struct firmux_fields, pcb_serial), propsizeof(struct firmux_fields, pcb_serial), acopy_text, acopy_text },
	{ "MAC_ADDR", offsetof(struct firmux_fields, mac_addr), propsizeof(struct firmux_fields, mac_addr), aparse_mac_address, aformat_mac_address },
	{ NULL, 0, 0, NULL, NULL }
};

static int firmux_fields_prop_is_set(void *sp, struct firmux_property *pprop)
{
	char *data = sp + pprop->fp_offset;
	int size = pprop->fp_size;

	while (size > 0 && (char)*data == (char)0xFF) {
		data++;
		size--;
	}

	return size > 0;
}

static struct firmux_property *firmux_fields_prop_find(char *key)
{
	struct firmux_property *pprop;

	pprop = &plain_props[0];
	while (pprop->fp_name) {
		if (!strcmp(pprop->fp_name, key))
			break;
		pprop++;
	}

	if (!pprop->fp_name)
		return NULL;

	return pprop;
}

static int firmux_fields_prop_check(char *key, char *in)
{
	struct firmux_property *pprop;
	char *val = NULL;
	size_t len = 0;
	int ret = -1;

	if (in && in[0] == '@') {
		val = afread(in + 1, &len);
	} else if (in) {
		val = in;
		len = strlen(in);
	}

	pprop = firmux_fields_prop_find(key);
	if (pprop) {
		if (!val)
			return 0;

		ret = pprop->fp_parse(NULL, val, len) == -1;
	}

	if (in && in[0] == '@')
		free(val);

	return ret;
}

static int firmux_fields_prop_print_all(void *sp)
{
	void *base = sp;
	struct firmux_property *pprop;
	void *val = NULL;
	ssize_t len;

	pprop = &plain_props[0];
	while (pprop->fp_name) {
		if (!firmux_fields_prop_is_set(sp, pprop))
			goto next;

		len = pprop->fp_format(&val, base + pprop->fp_offset, pprop->fp_size);
		if (len < 0)
			return -1;

		data_dump(pprop->fp_name, val, len);

		free(val);
next:
		pprop++;
	}

	return 0;
}

static int firmux_fields_prop_print(void *sp, char *key, char *out)
{
	void *base = sp;
	struct firmux_property *pprop;
	void *val = NULL;
	ssize_t len;

	if (!key)
		return firmux_fields_prop_print_all(sp);

	pprop = firmux_fields_prop_find(key);
	if (!pprop)
		return -1;

	if (!firmux_fields_prop_is_set(sp, pprop))
		return 1;

	len = pprop->fp_format(&val, base + pprop->fp_offset, pprop->fp_size);
	if (len < 0)
		return -1;

	if (out && out[0] == '@')
		afwrite(out + 1, val, len);
	else if (out)
		data_dump(out, val, len);
	else
		data_dump(key, val, len);

	free(val);

	return len;
}

static int firmux_fields_prop_store(void *sp, char *key, char *in)
{
	void *base = sp;
	struct firmux_property *pprop;
	char *val;
	void *data = NULL;
	ssize_t size, len;

	pprop = firmux_fields_prop_find(key);
	if (!pprop)
		return -1;

	if (!in)
		return -1;

	if (in[0] == '@') {
		val = afread(in + 1, &len);
		if (!val) {
			lerror("Failed to read file '%s'", in + 1);
			return -1;
		}
	} else {
		val = in;
		len = strlen(in);
	}

	size = pprop->fp_parse(&data, val, len);
	if (size < 0)
		return -1;

	if (size > pprop->fp_size) {
		free(data);
		return -1;
	}

	memcpy(base + pprop->fp_offset, data, size);
	dirty = 1;

	free(data);
	if (in[0] == '@')
		free(val);

	return 0;
}

static void firmux_fields_prop_list(void)
{
	struct firmux_property *pprop;

	pprop = &plain_props[0];
	while (pprop->fp_name) {
		printf("%s\n", pprop->fp_name);
		pprop++;
	}
}

static int firmux_fields_flush(void *sp)
{
	struct firmux_header *fh = sp - sizeof(*fh);
	unsigned int crc;

	if (dirty) {
		dirty = 0;
		crc = crc_32(sp, sizeof(struct firmux_fields));
		fh->crc = htonl(crc);
	}

	return 0;
}

static void firmux_fields_free(void *sp)
{
}

static void *firmux_fields_init(struct storage_device *dev, int force)
{
	struct firmux_header *fh;
	int empty, idx = 0;
	unsigned int crc;

	if (dev->size <= sizeof(*fh) + sizeof(struct firmux_fields)) {
		lerror("Storage is too small %zu/%zu", dev->size,
			   sizeof(*fh) + sizeof(struct firmux_fields));
		return NULL;
	}

	fh = dev->base;
	if (!strncmp(fh->magic, EEPROM_MAGIC, sizeof(fh->magic)))
		goto done;

	empty = 1;
	while (idx < sizeof(*fh)) {
		if (((unsigned char *)dev->base)[idx] != 0xFF) {
			empty = 0;
			break;
		}
		idx++;
	}

	if (empty || force) {
		if (force)
			ldebug("Reinitialising non-empty storage");
		memset(fh, 0, sizeof(*fh));
		memcpy(fh->magic, EEPROM_MAGIC, sizeof(fh->magic));
		crc = crc_32(dev->base + sizeof(*fh), sizeof(struct firmux_fields));
		fh->crc = htonl(crc);
	} else {
		ldebug("Unknown storage signature");
		return NULL;
	}

done:
	crc = crc_32(dev->base + sizeof(*fh), sizeof(struct firmux_fields));
	if (crc != ntohl(fh->crc)) {
		lerror("Invalid storage crc\n");
		return NULL;
	}

	return dev->base + sizeof(*fh);
}

static struct storage_protocol firmux_fields_model = {
	.name = "firmux-fields",
	.init = firmux_fields_init,
	.free = firmux_fields_free,
	.list = firmux_fields_prop_list,
	.check = firmux_fields_prop_check,
	.print = firmux_fields_prop_print,
	.store = firmux_fields_prop_store,
	.flush = firmux_fields_flush,
};

static void __attribute__((constructor)) firmux_fields_register(void)
{
	eeprom_register(&firmux_fields_model);
}
