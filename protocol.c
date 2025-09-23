
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "log.h"
#include "protocol.h"

struct proto_entry {
	struct storage_protocol *proto;
	struct proto_entry *next;
};

static struct proto_entry *proto_list;
static struct storage_protocol *proto_default;

int eeprom_register(struct storage_protocol *proto)
{
	struct proto_entry *entry;

	if (proto->def) {
		if (proto_default) {
			lerror("Default protocol already registered");
			return -1;
		}

		linfo("Registering default protocol: %s", proto->name);
		proto_default = proto;
		return 0;
	}

	ldebug("Registering protocol: %s", proto->name);

	entry = malloc(sizeof(struct proto_entry));
	if (!entry) {
		perror("malloc() failed");
		return -1;
	}

	entry->proto = proto;
	entry->next = proto_list;
	proto_list = entry;

	return 0;
}

void eeprom_unregister(void)
{
	struct proto_entry *tmp, *ptr = proto_list;

	while (ptr) {
		tmp = ptr->next;
		free(ptr);
		ptr = tmp;
	}

	proto_list = NULL;
}

struct storage_protocol *eeprom_init(struct storage_device *dev, int force)
{
	struct proto_entry *entry;
	struct storage_protocol *proto;
	struct storage_protocol *found = NULL;
	void *priv = NULL;

	found = proto_default;
	priv = found->init(dev, force);
	if (!priv && !force) {
		for (entry = proto_list; entry != NULL; entry = entry->next) {
			priv = entry->proto->init(dev, 0);
			if (!priv)
				continue;
			found = entry->proto;
			break;
		}
	}

	if (!priv) {
		lerror("No matching protocol found for storage device");
		return NULL;
	}

	ldebug("Initialising storage protocol: %s", found->name);

	proto = malloc(sizeof(*proto));
	if (!proto) {
		found->free(priv);
		perror("malloc() failed");
		return NULL;
	}

	memcpy(proto, found, sizeof(*proto));
	proto->priv = priv;

	return proto;
}

void eeprom_free(struct storage_protocol *proto)
{
	ldebug("Releasing protocol storage: %s", proto->name);
	if (proto->flush)
		proto->flush(proto->priv);
	proto->free(proto->priv);
	free(proto);
}

int eeprom_flush(struct storage_protocol *proto)
{
	if (!proto->flush)
		return 1;

	ldebug("Flushing protocol storage: %s", proto->name);
	return proto->flush(proto->priv);
}

void eeprom_list(struct storage_protocol *proto)
{
	if (!proto->list)
		return;

	proto->list();
}

int eeprom_check(struct storage_protocol *proto, char *key, char *in)
{
	if (!key)
		return -1;

	if (!proto->check)
		return 1;

	return proto->check(key, in);
}

int eeprom_import(struct storage_protocol *proto, char *key, char *in)
{
	if (!key || !in)
		return -1;

	if (!proto->store)
		return 1;

	return proto->store(proto->priv, key, in);
}

int eeprom_export(struct storage_protocol *proto, char *key, char *out)
{
	if (!proto->print)
		return 1;

	return proto->print(proto->priv, key, out);
}
