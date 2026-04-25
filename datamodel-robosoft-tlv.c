/* Robonode robosoft TLV datamodel */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "crc.h"

#include "log.h"
#include "utils.h"
#include "tlv.h"
#include "device.h"
#include "protocol.h"
#include "datamodel-robosoft-tlv.h"

#define EEPROM_MAGIC "RSDMTLV"
#define EEPROM_VERSION 1

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

static ssize_t tlvp_parse_device_mac(void **data_out, void *data_in, size_t size_in, const char *param)
{
	char *extra_buf;
	int extra_len, len;

	len = aparse_mac_address(data_out, data_in, size_in);
	if (len < 0)
		return len;

	extra_len = len + (param ? (strlen(param) + 1) : 0);
	if (!data_out)
		return extra_len;

	if (param && strlen(param)) {
		extra_buf = realloc(*data_out, extra_len);
		if (!extra_buf) {
			perror("realloc() failed");
			free(*data_out);
			return -1;
		}
		strcpy((char *)extra_buf + len, param);
		*data_out = extra_buf;
		len = extra_len;
	}

	return len;
}

static ssize_t tlvp_format_device_mac(void **data_out, void *data_in, size_t size_in, char **param)
{
	static char extra[16];
	int extra_len = size_in - 6;

	if (param) {
		if (extra_len < 0)
			extra_len = 0;
		else if (extra_len >= sizeof(extra))
			extra_len = sizeof(extra) - 1;
		if (extra_len)
			strncpy(extra, (char *)data_in + 6, extra_len);
		extra[extra_len] = '\0';
		*param = extra;
	}

	return aformat_mac_address(data_out, data_in, size_in);
}

static ssize_t tlvp_input_bin(void **data_out, void *data_in, size_t size_in)
{
	return acopy_data(data_out, data_in, size_in);
}

static ssize_t tlvp_output_bin(void **data_out, void *data_in, size_t size_in)
{
	return acopy_data(data_out, data_in, size_in);
}

/* Compression callbacks for binary built-in properties. When the build
 * was configured without liblzma we fall back to a plain copy so the
 * properties remain usable, just at full size. */
static ssize_t tlvp_compress_bin(void **data_out, void *data_in, size_t size_in)
{
#ifdef HAVE_LZMA_H
	return acompress_data(data_out, data_in, size_in);
#else
	return acopy_data(data_out, data_in, size_in);
#endif
}

static ssize_t tlvp_decompress_bin(void **data_out, void *data_in, size_t size_in)
{
#ifdef HAVE_LZMA_H
	return adecompress_data(data_out, data_in, size_in);
#else
	return acopy_data(data_out, data_in, size_in);
#endif
}

static struct tlv_property robosoft_builtin_props[] = {
	{ "PRODUCT_ID", EEPROM_ATTR_PRODUCT_ID, INPUT_SPEC_TXT, acopy_data, acopy_data },
	{ "PRODUCT_NAME", EEPROM_ATTR_PRODUCT_NAME, INPUT_SPEC_TXT, acopy_data, acopy_data },
	{ "SERIAL_NO", EEPROM_ATTR_SERIAL_NO, INPUT_SPEC_TXT, acopy_data, acopy_data },
	{ "PCB_NAME", EEPROM_ATTR_PCB_NAME, INPUT_SPEC_TXT, acopy_data, acopy_data },
	{ "PCB_REVISION", EEPROM_ATTR_PCB_REVISION, INPUT_SPEC_TXT, acopy_data, acopy_data },
	{ "PCB_PROD_DATE", EEPROM_ATTR_PCB_PROD_DATE, INPUT_SPEC_TXT, aparse_byte_triplet, aformat_byte_triplet },
	{ "PCB_PROD_LOCATION", EEPROM_ATTR_PCB_PROD_LOCATION, INPUT_SPEC_TXT, acopy_data, acopy_data },
	{ "PCB_SN", EEPROM_ATTR_PCB_SN, INPUT_SPEC_TXT, acopy_data, acopy_data },
	{ "XTAL_CALDATA", EEPROM_ATTR_XTAL_CAL_DATA, INPUT_SPEC_BIN, tlvp_input_bin, tlvp_output_bin },
	{ "RADIO_CALDATA", EEPROM_ATTR_RADIO_CAL_DATA, INPUT_SPEC_BIN, tlvp_compress_bin, tlvp_decompress_bin },
	{ "RADIO_BRDDATA", EEPROM_ATTR_RADIO_BOARD_DATA, INPUT_SPEC_BIN, tlvp_compress_bin, tlvp_decompress_bin },
	{ NULL, EEPROM_ATTR_NONE, INPUT_SPEC_NONE, NULL, NULL, }
};

static struct tlv_group robosoft_builtin_groups[] = {
	{ "MAC_ADDR", EEPROM_ATTR_MAC_FIRST, EEPROM_ATTR_MAC_LAST, INPUT_SPEC_TXT, tlvp_parse_device_mac, tlvp_format_device_mac },
	{ NULL, EEPROM_ATTR_NONE, EEPROM_ATTR_NONE, INPUT_SPEC_NONE, NULL, NULL }
};

static const struct robosoft_tlv_module robosoft_builtin_module = {
	.name = "builtin",
	.properties = robosoft_builtin_props,
	.groups = robosoft_builtin_groups,
};
ROBOSOFT_TLV_MODULE_REGISTER(robosoft_builtin_module);

/* Flat property and group tables, populated at init from every registered
 * module. Always NULL-terminated so the lookup helpers can iterate them
 * the same way they iterated the original static arrays. */
static struct tlv_property *tlv_properties;
static struct tlv_group *tlv_groups;
static size_t tlv_properties_n;
static size_t tlv_groups_n;
static int robosoft_modules_ready;

static const struct robosoft_tlv_module *robosoft_prop_owner(const struct tlv_property *p)
{
	const struct robosoft_tlv_module *const *iter;
	struct tlv_property *tlvp;

	ROBOSOFT_TLV_FOREACH_MODULE(iter) {
		for (tlvp = (*iter)->properties; tlvp && tlvp->tlvp_name; tlvp++)
			if (tlvp->tlvp_id == p->tlvp_id &&
			    !strcmp(tlvp->tlvp_name, p->tlvp_name))
				return *iter;
	}
	return NULL;
}

static const struct robosoft_tlv_module *robosoft_group_owner(const struct tlv_group *g)
{
	const struct robosoft_tlv_module *const *iter;
	struct tlv_group *tlvg;

	ROBOSOFT_TLV_FOREACH_MODULE(iter) {
		for (tlvg = (*iter)->groups; tlvg && tlvg->tlvg_pattern; tlvg++)
			if (tlvg->tlvg_id_first == g->tlvg_id_first &&
			    tlvg->tlvg_id_last == g->tlvg_id_last &&
			    !strcmp(tlvg->tlvg_pattern, g->tlvg_pattern))
				return *iter;
	}
	return NULL;
}

static void robosoft_modules_release(void)
{
	free(tlv_properties);
	tlv_properties = NULL;
	tlv_properties_n = 0;
	free(tlv_groups);
	tlv_groups = NULL;
	tlv_groups_n = 0;
	robosoft_modules_ready = 0;
}

static int robosoft_modules_validate(void)
{
	size_t i, j;
	int errors = 0;

	for (i = 0; i < tlv_properties_n; i++) {
		for (j = i + 1; j < tlv_properties_n; j++) {
			if (tlv_properties[i].tlvp_id != tlv_properties[j].tlvp_id)
				continue;
			lerror("Property id 0x%02x clash: '%s' (%s) vs '%s' (%s)",
			       tlv_properties[i].tlvp_id,
			       tlv_properties[i].tlvp_name,
			       robosoft_prop_owner(&tlv_properties[i])->name,
			       tlv_properties[j].tlvp_name,
			       robosoft_prop_owner(&tlv_properties[j])->name);
			errors++;
		}
	}

	for (i = 0; i < tlv_groups_n; i++) {
		for (j = i + 1; j < tlv_groups_n; j++) {
			enum tlv_code a_first = tlv_groups[i].tlvg_id_first;
			enum tlv_code a_last  = tlv_groups[i].tlvg_id_last;
			enum tlv_code b_first = tlv_groups[j].tlvg_id_first;
			enum tlv_code b_last  = tlv_groups[j].tlvg_id_last;
			if (a_first > b_last || b_first > a_last)
				continue;
			lerror("Group id range overlap: '%s' (%s) [0x%02x..0x%02x] vs '%s' (%s) [0x%02x..0x%02x]",
			       tlv_groups[i].tlvg_pattern,
			       robosoft_group_owner(&tlv_groups[i])->name,
			       a_first, a_last,
			       tlv_groups[j].tlvg_pattern,
			       robosoft_group_owner(&tlv_groups[j])->name,
			       b_first, b_last);
			errors++;
		}
	}

	for (i = 0; i < tlv_properties_n; i++) {
		enum tlv_code id = tlv_properties[i].tlvp_id;
		for (j = 0; j < tlv_groups_n; j++) {
			if (id < tlv_groups[j].tlvg_id_first ||
			    id > tlv_groups[j].tlvg_id_last)
				continue;
			lerror("Property id 0x%02x ('%s' in %s) falls inside group range '%s' (%s) [0x%02x..0x%02x]",
			       id,
			       tlv_properties[i].tlvp_name,
			       robosoft_prop_owner(&tlv_properties[i])->name,
			       tlv_groups[j].tlvg_pattern,
			       robosoft_group_owner(&tlv_groups[j])->name,
			       tlv_groups[j].tlvg_id_first,
			       tlv_groups[j].tlvg_id_last);
			errors++;
		}
	}

	return errors;
}

static int robosoft_modules_init(void)
{
	const struct robosoft_tlv_module *const *iter;
	struct tlv_property *tlvp;
	struct tlv_group *tlvg;
	size_t prop_n = 0, group_n = 0;
	size_t pi = 0, gi = 0;

	if (robosoft_modules_ready)
		return 0;

	ROBOSOFT_TLV_FOREACH_MODULE(iter) {
		for (tlvp = (*iter)->properties; tlvp && tlvp->tlvp_name; tlvp++)
			prop_n++;
		for (tlvg = (*iter)->groups; tlvg && tlvg->tlvg_pattern; tlvg++)
			group_n++;
	}

	/* +1 leaves a zero-init NULL terminator at the end. */
	tlv_properties = calloc(prop_n + 1, sizeof(*tlv_properties));
	tlv_groups = calloc(group_n + 1, sizeof(*tlv_groups));
	if (!tlv_properties || !tlv_groups) {
		perror("calloc() failed");
		robosoft_modules_release();
		return -1;
	}

	ROBOSOFT_TLV_FOREACH_MODULE(iter) {
		for (tlvp = (*iter)->properties; tlvp && tlvp->tlvp_name; tlvp++)
			tlv_properties[pi++] = *tlvp;
		for (tlvg = (*iter)->groups; tlvg && tlvg->tlvg_pattern; tlvg++)
			tlv_groups[gi++] = *tlvg;
	}
	tlv_properties_n = prop_n;
	tlv_groups_n = group_n;

	if (robosoft_modules_validate()) {
		robosoft_modules_release();
		return -1;
	}

	robosoft_modules_ready = 1;
	return 0;
}

static struct tlv_property *robosoft_tlv_prop_find(char *key)
{
	struct tlv_property *tlvp;

	tlvp = &tlv_properties[0];
	while (tlvp->tlvp_name) {
		if (!strcmp(tlvp->tlvp_name, key))
			break;
		tlvp++;
	}

	if (!tlvp->tlvp_name)
		return NULL;

	return tlvp;
}

static struct tlv_group *robosoft_tlv_param_find(char *key, char **param)
{
	struct tlv_group *tlvg;

	*param = NULL;

	tlvg = &tlv_groups[0];
	while (tlvg->tlvg_pattern) {
		if (!strncmp(tlvg->tlvg_pattern, key, strlen(tlvg->tlvg_pattern))) {
			*param = key + strlen(tlvg->tlvg_pattern);
			break;
		}
		tlvg++;
	}

	if (!*param || **param == '\0')
		return NULL;

	/* Skip the separator character */
	(*param)++;

	if (!tlvg->tlvg_pattern)
		return NULL;

	return tlvg;
}

static enum tlv_code robosoft_tlv_param_slot(struct tlv_store *tlvs, struct tlv_group *tlvg, char *param, int exact)
{
	enum tlv_code code, slot = EEPROM_ATTR_NONE;
	char *extra;
	void *data = NULL;
	int data_len = 0;
	ssize_t size;

	for (code = tlvg->tlvg_id_first; code <= tlvg->tlvg_id_last; code++) {
		size = tlvs_get(tlvs, code, 0, NULL);
		if (size < 0) {
			if (!exact && (slot == EEPROM_ATTR_NONE))
				slot = code;
			continue;
		}
		if (data_len < size) {
			data_len = size;
			data = realloc(data, size);
			if (!data) {
				perror("realloc() failed");
				break;
			}
		}

		if (tlvs_get(tlvs, code, size, data) < 0) {
			if (!exact && (slot == EEPROM_ATTR_NONE))
				slot = code;
			continue;
		}

		if (tlvg->tlvg_format(NULL, data, size, &extra) < 0)
			continue;

		if (extra && !strcmp(extra, param)) {
			slot = code;
			break;
		}
	}

	if (data)
		free(data);

	return slot;
}

static int robosoft_tlv_prop_check(char *key, char *in)
{
	struct tlv_property *tlvp;
	struct tlv_group *tlvg;
	char *param;
	char *val = NULL;
	size_t len = 0;
	int ret = -1;

	if (in && in[0] == '@') {
		val = afread(in + 1, &len);
	} else if (in) {
		val = in;
		len = strlen(in);
	}

	tlvg = robosoft_tlv_param_find(key, &param);
	if (tlvg) {
		if (!val)
			return 0;

		ret = tlvg->tlvg_parse(NULL, val, len, param) == -1;
		goto out;
	}

	tlvp = robosoft_tlv_prop_find(key);
	if (tlvp) {
		if (!val)
			return 0;

		ret = tlvp->tlvp_parse(NULL, val, len) == -1;
		goto out;
	}

out:
	if (in && in[0] == '@')
		free(val);

	return ret;
}

static struct tlv_property *robosoft_tlv_prop_format(struct tlv_field *tlv, char **key)
{
	struct tlv_property *tlvp;

	if (key)
		*key = NULL;

	tlvp = &tlv_properties[0];
	while (tlvp->tlvp_name) {
		if (tlvp->tlvp_id == tlv->type)
			break;
		tlvp++;
	}

	if (!tlvp->tlvp_name)
		return NULL;

	if (key) {
		*key = strndup(tlvp->tlvp_name, strlen(tlvp->tlvp_name));
		if (!*key)
			return NULL;
	}

	return tlvp;
}

static struct tlv_group *robosoft_tlv_param_format(struct tlv_field *tlv, char **key)
{
	struct tlv_group *tlvg;
	char *param;

	if (key)
		*key = NULL;

	tlvg = &tlv_groups[0];
	while (tlvg->tlvg_pattern) {
		if (tlvg->tlvg_id_first <= tlv->type && tlvg->tlvg_id_last >= tlv->type)
			break;
		tlvg++;
	}

	if (!tlvg->tlvg_pattern)
		return NULL;

	if (key) {
		if (tlvg->tlvg_format(NULL, tlv->value, ntohs(tlv->length), &param) < 0)
			return NULL;
		*key = malloc(strlen(tlvg->tlvg_pattern) + strlen(param) + 2);
		if (!*key)
			return NULL;
		sprintf(*key, "%s_%s", tlvg->tlvg_pattern, param);
	}

	return tlvg;
}

static int robosoft_tlv_prop_store(void *sp, char *key, char *in)
{
	struct tlv_store *tlvs = (struct tlv_store *)sp;
	struct tlv_property *tlvp;
	struct tlv_group *tlvg;
	enum tlv_code code;
	char *param;
	char *val;
	void *data = NULL;
	ssize_t size, len;
	int ret = -1;

	if ((tlvg = robosoft_tlv_param_find(key, &param))) {
		code = robosoft_tlv_param_slot(tlvs, tlvg, param, 0);
		if (code == EEPROM_ATTR_NONE) {
			ldebug("Failed TLV param '%s' slot lookup", param);
			return -1;
		}
	} else if ((tlvp = robosoft_tlv_prop_find(key))) {
		code = tlvp->tlvp_id;
	} else {
		ldebug("Invalid TLV property '%s'", key);
		return -1;
	}

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

	if (tlvg) {
		size = tlvg->tlvg_parse(&data, val, len, param);
		if (size < 0) {
			lerror("Failed TLV param '%s' parse, size %zu", param, len);
			goto fail;
		}
	} else if (tlvp) {
		size = tlvp->tlvp_parse(&data, val, len);
		if (size < 0) {
			lerror("Failed TLV property parse, size %zu", len);
			goto fail;
		}
	}

	ret = tlvs_set(tlvs, code, size, data);

fail:
	if (data)
		free(data);
	if (in[0] == '@')
		free(val);
	return ret;
}

static int robosoft_tlv_print_all(struct tlv_store *tlvs)
{
	struct tlv_iterator iter;
	struct tlv_field *tlv;
	struct tlv_property *tlvp;
	struct tlv_group *tlvg;
	enum tlv_spec spec;
	char *key;
	char *val;
	ssize_t len;
	int fail = 0;

	tlvs_iter_init(&iter, tlvs);

	while ((tlv = tlvs_iter_next(&iter)) != NULL) {
		val = NULL;
		key = NULL;

		if ((tlvg = robosoft_tlv_param_format(tlv, &key))) {
			len = tlvg->tlvg_format((void **)&val, tlv->value, ntohs(tlv->length), NULL);
			if (len < 0) {
				lerror("Failed to format TLV param %s", key);
				fail++;
				goto next;
			}
			spec = tlvg->tlvg_spec;
		} else if ((tlvp = robosoft_tlv_prop_format(tlv, &key))) {
			len = tlvp->tlvp_format((void **)&val, tlv->value, ntohs(tlv->length));
			if (len < 0) {
				lerror("Failed to format TLV param %s", key);
				fail++;
				goto next;
			}
			spec = tlvp->tlvp_spec;
		} else {
			lerror("Invalid TLV property type '%i'", tlv->type);
			fail++;
			goto next;
		}

		data_dump(key, val, len, spec);

next:
		free(key);
		if (val)
			free(val);
	}

	return fail;
}

static int robosoft_tlv_prop_print(void *sp, char *key, char *out)
{
	struct tlv_store *tlvs = (struct tlv_store *)sp;
	struct tlv_property *tlvp;
	struct tlv_group *tlvg;
	enum tlv_code code;
	enum tlv_spec spec;
	char *param;
	char *val;
	void *data;
	ssize_t size, len;

	if (!key)
		return robosoft_tlv_print_all(tlvs);

	if ((tlvg = robosoft_tlv_param_find(key, &param))) {
		code = robosoft_tlv_param_slot(tlvs, tlvg, param, 1);
		if (code == EEPROM_ATTR_NONE) {
			ldebug("Failed TLV param '%s' slot lookup", param);
			return -1;
		}
	} else if ((tlvp = robosoft_tlv_prop_find(key))) {
		code = tlvp->tlvp_id;
	} else {
		ldebug("Invalid TLV property '%s'", key);
		return -1;
	}

	size = tlvs_get(tlvs, code, 0, NULL);
	if (size < 0) {
		lerror("Failed TLV property '%s' get", key);
		return 1;
	}

	data = malloc(size);
	if (!data) {
		perror("malloc() failed");
		return -1;
	}

	if (tlvs_get(tlvs, code, size, data) < 0) {
		free(data);
		return -1;
	}

	if (tlvg) {
		len = tlvg->tlvg_format((void **)&val, data, size, NULL);
		spec = tlvg->tlvg_spec;
	} else if (tlvp) {
		len = tlvp->tlvp_format((void **)&val, data, size);
		spec = tlvp->tlvp_spec;
	}
	free(data);
	if (len < 0) {
		lerror("Failed TLV property format, size %zu", size);
		return -1;
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

static void robosoft_tlv_prop_list(void)
{
	struct tlv_property *tlvp;
	struct tlv_group *tlvg;

	tlvp = &tlv_properties[0];
	while (tlvp->tlvp_name) {
		printf("%s\n", tlvp->tlvp_name);
		tlvp++;
	}

	tlvg = &tlv_groups[0];
	while (tlvg->tlvg_pattern) {
		printf("%s*\n", tlvg->tlvg_pattern);
		tlvg++;
	}
}

static int robosoft_tlv_flush(void *sp)
{
	struct tlv_store *tlvs = sp;
	struct tlv_header *tlvh = tlvs->base - sizeof(*tlvh);
	int len;

	if (tlvs->dirty) {
		len = tlvs_len(sp);
		tlvs->dirty = 0;
		tlvh->len = htonl(len);
		tlvh->crc = htonl(crc_32(tlvs->base, len));
	}

	return 0;
}

static void robosoft_tlv_free(void *sp)
{
	tlvs_free((struct tlv_store *)sp);
	robosoft_modules_release();
}

static void *robosoft_tlv_init(struct storage_device *dev, int force)
{
	struct tlv_header *tlvh;
	struct tlv_store *tlvs;
	int empty, len = 0;
	unsigned int crc;

	if (robosoft_modules_init() < 0) {
		lerror("Failed to initialise robosoft TLV modules");
		return NULL;
	}

	if (dev->size <= sizeof(*tlvh)) {
		lerror("Storage is too small %zu/%zu", dev->size, sizeof(*tlvh));
		return NULL;
	}

	tlvh = dev->base;
	if (!strncmp(tlvh->magic, EEPROM_MAGIC, sizeof(tlvh->magic)) &&
	    tlvh->version == EEPROM_VERSION)
		goto done;

	empty = 1;
	while (len < sizeof(*tlvh)) {
		if (((unsigned char *)dev->base)[len] != 0xFF) {
			empty = 0;
			break;
		}
		len++;
	}

	if (force) {
		ldebug("Reinitialising non-empty storage");
		/* XXX: TLV management module relies solely on buffer
		 * data to find the spare slot to set data, therefore
		 * memory has to be whiped out before passing it for
		 * TLV processing. */
		memset(dev->base, 0xFF, dev->size);
	}

	if (empty || force) {
		memset(tlvh, 0, sizeof(*tlvh));
		memcpy(tlvh->magic, EEPROM_MAGIC, sizeof(tlvh->magic));
		tlvh->version = EEPROM_VERSION;
	} else {
		ldebug("Unknown storage signature");
		return NULL;
	}

done:
	crc = crc_32(dev->base + sizeof(*tlvh), ntohl(tlvh->len));
	if (crc != ntohl(tlvh->crc)) {
		lerror("Storage CRC validation failed (expected 0x%08x, got 0x%08x)", ntohl(tlvh->crc), crc);
		return NULL;
	}

	tlvs = tlvs_init(dev->base + sizeof(*tlvh), dev->size - sizeof(*tlvh));
	if (!tlvs)
		lerror("Failed to initialize TLV store");

	return tlvs;
}

static struct storage_protocol robosoft_tlv_model = {
	.name = "robosoft-tlv",
	.def = 1,
	.init = robosoft_tlv_init,
	.free = robosoft_tlv_free,
	.list = robosoft_tlv_prop_list,
	.check = robosoft_tlv_prop_check,
	.print = robosoft_tlv_prop_print,
	.store = robosoft_tlv_prop_store,
	.flush = robosoft_tlv_flush,
};

static void __attribute__((constructor)) robosoft_tlv_register(void)
{
	eeprom_register(&robosoft_tlv_model);
}
