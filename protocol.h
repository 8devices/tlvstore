#ifndef __STORAGE_PROTOCOL_H
#define __STORAGE_PROTOCOL_H

#include "device.h"

struct storage_protocol {
	const char *name;
	int def;
	void *priv;

	void *(*init)(struct storage_device *dev, int force);
	void (*free)(void *sp);
	void (*list)(void);
	int (*check)(char *key, char *val);
	int (*print)(void *sp, char *key, char *out);
	int (*store)(void *sp, char *key, char *in);
	int (*flush)(void *sp);
};

int eeprom_register(struct storage_protocol *proto);
void eeprom_unregister(void);
struct storage_protocol *eeprom_init(struct storage_device *dev, int force);
void eeprom_free(struct storage_protocol *proto);
int eeprom_flush(struct storage_protocol *proto);
void eeprom_list(struct storage_protocol *proto);
int eeprom_check(struct storage_protocol *proto, char *key, char *val);
int eeprom_import(struct storage_protocol *proto, char *key, char *in);
int eeprom_export(struct storage_protocol *proto, char *key, char *out);

#endif /* __STORAGE_PROTOCOL_H */
