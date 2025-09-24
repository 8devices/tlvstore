#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "tlv.h"
#include "device.h"
#include "protocol.h"

#ifndef TLVS_DEFAULT_FILE
#define TLVS_DEFAULT_FILE NULL
#endif
#ifndef TLVS_DEFAULT_SIZE
#define TLVS_DEFAULT_SIZE 0
#endif
#ifndef TLVS_DEFAULT_OFFSET
#define TLVS_DEFAULT_OFFSET 0
#endif

#define OP_LIST 1
#define OP_GET 2
#define OP_SET 3

struct params_list {
	struct params_list *next;
	char *key;
	char *val;
};

static struct params_list *pl;
static struct storage_device *dev;
static struct storage_protocol *proto;
static int op;
static int compat;
/* Global log level - default to WARNING */
int g_log_level = LOG_WARNING;

int tlvstore_parse_line(char *arg)
{
	char *key, *val;
	struct params_list *pe;

	key = strdup(arg);
	val = strchr(key, '=');
	if (val) {
		*val = 0;
		val++;
	}

	ldebug("Parsed parameter: '%s' = '%s'", key, val);

	if (eeprom_check(proto, key, op == OP_SET ? val : NULL)) {
		lerror("Invalid EEPROM param '%s'", arg);
		free(key);
		return 1;
	}

	/* Find available param */
	pe = pl;
	while (pe) {
		if (!strcmp(pe->key, key))
			break;
		pe = pe->next;
	}

	if (pe) {
		free(pe->key);
		pe->key = key;
		pe->val = val;
	} else {
		pe = calloc(1, sizeof(*pe));
		pe->key = key;
		pe->val = val;
		pe->next = pl;
		pl = pe;
	}

	return 0;
}

int tlvstore_parse_config(char *arg)
{
	FILE *fp;
	char *fname;
	char line[256];
	int len, fail = 0;

	fname = *arg == '@' ? arg + 1 : arg;
	fp = fopen(fname, "r");
	if (!fp) {
		lerror("Invalid EEPROM config '%s'", arg);
		return 1;
	}

	ldebug("Opened EEPROM config file: %s", fname);

	while (fgets(line, sizeof(line) - 1, fp)) {
		/* Trim the line */
		len = strlen(line);
		while (line[len - 1] <= 32 || line[len - 1] >= 127)
			len--;
		line[len] = 0;
		fail += tlvstore_parse_line(line);
	}

	if (ferror(fp)) {
		lerror("Cannot read EEPROM config file: '%s'", fname);
		fail++;
	}

	fclose(fp);
	return fail;
}

int tlvstore_parse_params(int argc, char *argv[])
{
	int i, fail = 0;
	char *arg;

	ldebug("Parsing %d parameters", argc);
	for (int i = 0; i < argc; i++) {
		arg = argv[i];
		if (arg[0] == '@')
			fail += tlvstore_parse_config(arg);
		else
			fail += tlvstore_parse_line(arg);
	}

	return fail;
}

int tlvstore_export_params(void)
{
	struct params_list *pe = pl;
	int fail = 0;
	int ret;

	if (!pl) {
		ldebug("Exporting all TLV properties");
		fail = eeprom_export(proto, NULL, NULL);
	} else {
		ldebug("Starting parameters export");
	}

	while (pl) {
		ret = eeprom_export(proto, pl->key, pl->val);
		if (ret < 0 || (!compat && ret)) {
			if (pl->val && pl->val[0] == '@')
				lwarning("Failed to export '%s' to '%s'",
				       pl->key, pl->val);
			else if (pl->val)
				lwarning("Failed to export '%s' as '%s'",
				       pl->key, pl->val);
			else
				lwarning("Failed to export '%s'", pl->key);
			fail++;
		}
		pl = pl->next;
	}

	if (fail)
		ldebug("Failed TLV export, %i failures", fail);

	return fail;
}

int tlvstore_import_params(void)
{
	struct params_list *pe = pl;
	int fail = 0;

	ldebug("Starting parameters import");
	while (pl) {
		if (eeprom_import(proto, pl->key, pl->val) < 0) {
			lwarning("Failed to import '%s' value '%s'", pl->key, pl->val);
			fail++;
		}
		pl = pl->next;
	}

	if (fail)
		lerror("Failed TLV import, %i failures", fail);

	return fail;
}

void tlvstore_list_properties(void)
{
	ldebug("Listing TLV properties");
	eeprom_list(proto);
}

static void tlvstore_usage(void)
{
	fprintf(stderr, "Usage: tlvstore [options] <key>[=@value>] ...\n"
			"  -F, --store-file <file-name>     Storage file path\n"
			"  -S, --store-size <file-size>     Preferred storage file size\n"
			"  -O, --store-offset <file-offset> Storage file data offset\n"
			"  -v, --verbose                    Increase verbosity\n"
			"  -f, --force                      Force initialise storage\n"
			"  -c, --compat                     Compatibility retrieve avilable params\n"
			"  -g, --get                        Get specified keys or all keys when no specified\n"
			"  -s, --set                        Set specified keys\n"
			"  -l, --list                       List available keys\n"
	       );
}

static struct option tlvstore_options[] =
{
	{ "store-size",   1, 0, 'S' },
	{ "store-file",   1, 0, 'F' },
	{ "store-offset", 1, 0, 'O' },
	{ "verbose",      0, 0, 'v' },
	{ "force",        0, 0, 'f' },
	{ "compat",       0, 0, 'c' },
	{ "get",          0, 0, 'g' },
	{ "set",          0, 0, 's' },
	{ "list",         0, 0, 'l' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char *argv[])
{
	int opt, index;
	int ret = 1;
	char *store_file = NULL;
	int store_size = 0;
	int store_offset = 0;
	int force = 0;

	while ((opt = getopt_long(argc, argv, "F:S:O:hvfcgsl", tlvstore_options, &index)) != -1) {
		switch (opt) {
		case 'F':
			store_file = strdup(optarg);
			break;
		case 'S':
			store_size = atoi(optarg);
			break;
		case 'O':
			store_offset = atoi(optarg);
			break;
		case 'v':
			if (g_log_level)
				g_log_level--;
			break;
		case 'f':
			force = 1;
			break;
		case 'c':
			compat = 1;
			break;
		case 'g':
			op = OP_GET;
			break;
		case 's':
			op = OP_SET;
			break;
		case 'l':
			op = OP_LIST;
			break;
		case 'h':
		default:
			tlvstore_usage();
			exit(EXIT_FAILURE);
		}
	}

	/* Initialise build-time defaults only when default
	 * storage file is used. Even in that case allow to
	 * override custom storage size and offset when set. */
	if (!store_file) {
		store_file = TLVS_DEFAULT_FILE;
		if (!store_size)
			store_size = TLVS_DEFAULT_SIZE;
		if (!store_offset)
			store_offset = TLVS_DEFAULT_OFFSET;
	}

	if (access(store_file, F_OK) && !store_size) {
		fprintf(stderr, "Storage file does not exist, specify storage size to initialise\n");
		tlvstore_usage();
		exit(EXIT_FAILURE);
	}

	dev = storage_open(store_file, store_size, store_offset);
	if (!dev) {
		fprintf(stderr, "Failed to initialize '%s' storage file\n", store_file);
		exit(EXIT_FAILURE);
	}

	linfo("Opened storage file '%s' (%zu bytes)", store_file, dev->size);

	proto = eeprom_init(dev, force);
	if (!proto) {
		fprintf(stderr, "Unknown storage protocol for '%s'\n", store_file);
		storage_close(dev);
		exit(EXIT_FAILURE);
	}

	linfo("Initialized storage protocol '%s'", proto->name);

	if (tlvstore_parse_params(argc - optind, &argv[optind])) {
		tlvstore_usage();
		eeprom_free(proto);
		storage_close(dev);
		exit(EXIT_FAILURE);
	}

	if (op == OP_LIST) {
		tlvstore_list_properties();
		ret = 0;
	} else if (op == OP_GET) {
		if (!tlvstore_export_params())
			ret = 0;
	} else if (op == OP_SET) {
		if (!tlvstore_import_params())
			ret = 0;
	} else {
		lwarning("No operation specified");
	}

	eeprom_free(proto);

	storage_close(dev);

	eeprom_unregister();

	return ret;
}
