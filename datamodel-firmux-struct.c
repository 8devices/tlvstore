#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#include "crc.h"

#include "log.h"
#include "utils.h"
#include "device.h"
#include "protocol.h"
#include "datamodel-firmux-struct.h"

#define EEPROM_MAGIC "FXDMSRT1"

static int dirty;

static int data_dump(const char *key, void *val, int len)
{
	printf("%s=%s\n", key, (char *)val);
}

static int firmux_struct_prop_is_set(void *sp, char *key)
{
	struct firmux_fields *model = sp;

	if (!strcmp(key, "PRODUCT_ID")) {
		return !bempty_data(model->product_id, sizeof(model->product_id));
	} else if (!strcmp(key, "PRODUCT_NAME")) {
		return !bempty_data(model->product_name, sizeof(model->product_name));
	} else if (!strcmp(key, "SERIAL_NO")) {
		return !bempty_data(model->serial_no, sizeof(model->serial_no));
	} else if (!strcmp(key, "PCB_NAME")) {
		return !bempty_data(model->pcb_name, sizeof(model->pcb_name));
	} else if (!strcmp(key, "PCB_REVISION")) {
		return !bempty_data(model->pcb_revision, sizeof(model->pcb_revision));
	} else if (!strcmp(key, "PCB_PRDATE")) {
		return !bempty_data(model->pcb_prdate, sizeof(model->pcb_prdate));
	} else if (!strcmp(key, "PCB_PRLOCATION")) {
		return !bempty_data(model->pcb_prlocation, sizeof(model->pcb_prlocation));
	} else if (!strcmp(key, "PCB_SN")) {
		return !bempty_data(model->pcb_serial, sizeof(model->pcb_serial));
	} else if (!strcmp(key, "MAC_ADDR")) {
		return !bempty_data(model->mac_addr, sizeof(model->mac_addr));
	} else {
		return 0;
	}
}

static int firmux_struct_prop_check(char *key, char *in)
{
	char *val = NULL;
	size_t len = 0;
	int ret = -1;

	if (!key)
		return -1;

	/* Input processing */
	if (in && in[0] == '@') {
		val = afread(in + 1, &len);
	} else if (in) {
		val = in;
		len = strlen(in);
	}

	if (!val)
		return 0;

	if (!strcmp(key, "PRODUCT_ID")) {
		ret = sizeof(((struct firmux_fields *)0)->product_id) >= len;
	} else if (!strcmp(key, "PRODUCT_NAME")) {
		ret = sizeof(((struct firmux_fields *)0)->product_name) >= len;
	} else if (!strcmp(key, "SERIAL_NO")) {
		ret = sizeof(((struct firmux_fields *)0)->serial_no) >= len;
	} else if (!strcmp(key, "PCB_NAME")) {
		ret = sizeof(((struct firmux_fields *)0)->pcb_name) >= len;
	} else if (!strcmp(key, "PCB_REVISION")) {
		ret = sizeof(((struct firmux_fields *)0)->pcb_revision) >= len;
	} else if (!strcmp(key, "PCB_PRDATE")) {
		unsigned char date[3];
		return sscanf(val, "%hhu-%hhu-%hhu", &date[0], &date[1], &date[2]) != 3;
	} else if (!strcmp(key, "PCB_PRLOCATION")) {
		ret = sizeof(((struct firmux_fields *)0)->pcb_prlocation) >= len;
	} else if (!strcmp(key, "PCB_SN")) {
		ret = sizeof(((struct firmux_fields *)0)->pcb_serial) >= len;
	} else if (!strcmp(key, "MAC_ADDR")) {
		unsigned char mac[6];
		return sscanf(val, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
			      &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 6;
	}

	if (in && in[0] == '@')
		free(val);

	return ret;
}

static char *_firmux_struct_prop_print(void *sp, char *key)
{
	struct firmux_fields *model = sp;

	if (!strcmp(key, "PRODUCT_ID")) {
		if (bempty_data(model->product_id, sizeof(model->product_id)))
			return NULL;

		return bcopy_text(model->product_id, sizeof(model->product_id));
	} else if (!strcmp(key, "PRODUCT_NAME")) {
		if (bempty_data(model->product_name, sizeof(model->product_name)))
			return NULL;

		return bcopy_text(model->product_name, sizeof(model->product_name));
	} else if (!strcmp(key, "SERIAL_NO")) {
		if (bempty_data(model->serial_no, sizeof(model->serial_no)))
			return NULL;

		return bcopy_text(model->serial_no, sizeof(model->serial_no));
	} else if (!strcmp(key, "PCB_NAME")) {
		if (bempty_data(model->pcb_name, sizeof(model->pcb_name)))
			return NULL;

		return bcopy_text(model->pcb_name, sizeof(model->pcb_name));
	} else if (!strcmp(key, "PCB_REVISION")) {
		if (bempty_data(model->pcb_revision, sizeof(model->pcb_revision)))
			return NULL;

		return bcopy_text(model->pcb_revision, sizeof(model->pcb_revision));
	} else if (!strcmp(key, "PCB_PRDATE")) {
		static char date_str[10];
		if (bempty_data(model->pcb_prdate, sizeof(model->pcb_prdate)))
			return NULL;

		snprintf(date_str, sizeof(date_str), "%u-%u-%u",
			 model->pcb_prdate[0],
			 model->pcb_prdate[1],
			 model->pcb_prdate[2]);

		return date_str;
	} else if (!strcmp(key, "PCB_PRLOCATION")) {
		if (bempty_data(model->pcb_prlocation, sizeof(model->pcb_prlocation)))
			return NULL;

		return bcopy_text(model->pcb_prlocation, sizeof(model->pcb_prlocation));
	} else if (!strcmp(key, "PCB_SN")) {
		if (bempty_data(model->pcb_serial, sizeof(model->pcb_serial)))
			return NULL;

		return bcopy_text(model->pcb_serial, sizeof(model->pcb_serial));
	} else if (!strcmp(key, "MAC_ADDR")) {
		static char mac_str[18];
		if (bempty_data(model->mac_addr, sizeof(model->mac_addr)))
			return NULL;

		snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
			 model->mac_addr[0], model->mac_addr[1], model->mac_addr[2],
			 model->mac_addr[3], model->mac_addr[4], model->mac_addr[5]);

		return mac_str;
	} else {
		return NULL;
	}
}

static int firmux_struct_prop_print_all(void *sp)
{
	struct firmux_fields *fields = sp;
	char *val;

	val = _firmux_struct_prop_print(sp, "PRODUCT_ID");
	if (val) {
		data_dump("PRODUCT_ID", val, strlen(val));
	}

	val = _firmux_struct_prop_print(sp, "PRODUCT_NAME");
	if (val) {
		data_dump("PRODUCT_NAME", val, strlen(val));
	}

	val = _firmux_struct_prop_print(sp, "SERIAL_NO");
	if (val) {
		data_dump("SERIAL_NO", val, strlen(val));
	}

	val = _firmux_struct_prop_print(sp, "PCB_NAME");
	if (val) {
		data_dump("PCB_NAME", val, strlen(val));
	}

	val = _firmux_struct_prop_print(sp, "PCB_REVISION");
	if (val) {
		data_dump("PCB_REVISION", val, strlen(val));
	}

	val = _firmux_struct_prop_print(sp, "PCB_PRDATE");
	if (val) {
		data_dump("PCB_PRDATE", val, strlen(val));
	}

	val = _firmux_struct_prop_print(sp, "PCB_PRLOCATION");
	if (val) {
		data_dump("PCB_PRLOCATION", val, strlen(val));
	}

	val = _firmux_struct_prop_print(sp, "PCB_SN");
	if (val) {
		data_dump("PCB_SN", val, strlen(val));
	}

	val = _firmux_struct_prop_print(sp, "MAC_ADDR");
	if (val) {
		data_dump("MAC_ADDR", val, strlen(val));
	}

	return 0;
}

static int firmux_struct_prop_print(void *sp, char *key, char *out)
{
	char *val;

	if (!key)
		return firmux_struct_prop_print_all(sp);

	if (!firmux_struct_prop_is_set(sp, key));
		return 1;

	val = _firmux_struct_prop_print(sp, key);
	if (!val)
		return -1;

	if (out && out[0] == '@')
		afwrite(out + 1, val, strlen(val));
	else if (out)
		data_dump(out, val, strlen(val));
	else
		data_dump(key, val, strlen(val));

	return 0;
}

static int _firmux_struct_prop_store(void *sp, char *key, char *val, size_t len)
{
	struct firmux_fields *model = sp;

	if (!strcmp(key, "PRODUCT_ID")) {
		if (len > sizeof(model->product_id))
			return -1;

		strcpy(model->product_id, val);
	} else if (!strcmp(key, "PRODUCT_NAME")) {
		if (len > sizeof(model->product_name))
			return -1;

		strcpy(model->product_name, val);
	} else if (!strcmp(key, "SERIAL_NO")) {
		if (len > sizeof(model->serial_no))
			return -1;

		strcpy(model->serial_no, val);
	} else if (!strcmp(key, "PCB_NAME")) {
		if (len > sizeof(model->pcb_name))
			return -1;

		strcpy(model->pcb_name, val);
	} else if (!strcmp(key, "PCB_REVISION")) {
		if (len > sizeof(model->pcb_revision))
			return -1;

		strcpy(model->pcb_revision, val);
	} else if (!strcmp(key, "PCB_PRDATE")) {
		unsigned char date[3];
		if (sscanf(val, "%hhu-%hhu-%hhu", &date[0], &date[1], &date[2]) != 3)
			return -1;

		memcpy(model->pcb_prdate, date, sizeof(model->pcb_prdate));
	} else if (!strcmp(key, "PCB_PRLOCATION")) {
		if (len > sizeof(model->pcb_prlocation))
			return -1;

		strcpy(model->pcb_prlocation, val);
	} else if (!strcmp(key, "PCB_SN")) {
		if (len > sizeof(model->pcb_serial))
			return -1;

		strcpy(model->pcb_serial, val);
	} else if (!strcmp(key, "MAC_ADDR")) {
		unsigned char mac[6];
		if (sscanf(val, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
			   &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 6)
			return -1;

		memcpy(model->mac_addr, mac, sizeof(model->mac_addr));
	} else {
		return -1;
	}

	dirty = 1;

	return 0;
}

static int firmux_struct_prop_store(void *sp, char *key, char *in)
{
	char *val;
	size_t len;
	int ret = -1;

	if (!key || !in)
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

	ret = _firmux_struct_prop_store(sp, key, val, len);

	if (in[0] == '@')
		free(val);

	return ret;
}

static void firmux_struct_prop_list(void)
{
	printf("PRODUCT_ID\n");
	printf("PRODUCT_NAME\n");
	printf("SERIAL_NO\n");
	printf("PCB_NAME\n");
	printf("PCB_REVISION\n");
	printf("PCB_PRDATE\n");
	printf("PCB_PRLOCATION\n");
	printf("PCB_SN\n");
	printf("MAC_ADDR\n");
}

static int firmux_struct_flush(void *sp)
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

static void firmux_struct_free(void *sp)
{
}

static void *firmux_struct_init(struct storage_device *dev, int force)
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
		lerror("Storage CRC validation failed (expected 0x%08x, got 0x%08x)", ntohl(fh->crc), crc);
		return NULL;
	}

	return dev->base + sizeof(*fh);
}

static struct storage_protocol firmux_struct_model = {
	.name = "firmux-struct",
	.init = firmux_struct_init,
	.free = firmux_struct_free,
	.list = firmux_struct_prop_list,
	.check = firmux_struct_prop_check,
	.print = firmux_struct_prop_print,
	.store = firmux_struct_prop_store,
	.flush = firmux_struct_flush,
};

static void __attribute__((constructor)) firmux_struct_register(void)
{
	eeprom_register(&firmux_struct_model);
}
