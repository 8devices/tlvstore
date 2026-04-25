#ifndef __FIRMUX_TLV_H
#define __FIRMUX_TLV_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum tlv_code {
	EEPROM_ATTR_NONE,

	/* Product related attributes */
	EEPROM_ATTR_PRODUCT_ID,                 /* char*, ASCII */
	EEPROM_ATTR_PRODUCT_NAME,               /* char*, ASCII */
	EEPROM_ATTR_SERIAL_NO,                  /* char*, ASCII */

	/* PCB related attributes */
	EEPROM_ATTR_PCB_NAME = 16,              /* char*, "FooBar" */
	EEPROM_ATTR_PCB_REVISION,               /* char*, "0002" */
	EEPROM_ATTR_PCB_PROD_DATE,              /* time_t, seconds */
	EEPROM_ATTR_PCB_PROD_LOCATION,          /* char*, "Kaunas" */
	EEPROM_ATTR_PCB_SN,                     /* char*, "" */

	/* MAC infoformation */
	EEPROM_ATTR_MAC = 128,
	EEPROM_ATTR_MAC_FIRST = EEPROM_ATTR_MAC,
	EEPROM_ATTR_MAC_1 = EEPROM_ATTR_MAC,
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
	EEPROM_ATTR_MAC_16,
	EEPROM_ATTR_MAC_LAST = EEPROM_ATTR_MAC_16,

	/* Calibration data */
	EEPROM_ATTR_XTAL_CAL_DATA = 240,
	EEPROM_ATTR_RADIO_CAL_DATA,
	EEPROM_ATTR_RADIO_BOARD_DATA,

	EEPROM_ATTR_EMPTY = 0xFF,
};

enum tlv_spec {
	INPUT_SPEC_NONE,
	INPUT_SPEC_TXT,
	INPUT_SPEC_BIN,
};

struct __attribute__ ((__packed__)) tlv_header {
	char magic[7];
	uint8_t version;
	uint32_t crc;
	uint32_t len;
};

struct tlv_property {
	const char *tlvp_name;
	enum tlv_code tlvp_id;
	enum tlv_spec tlvp_spec;
	ssize_t (*tlvp_parse)(void **data_out, void *data_in, size_t size_in);
	ssize_t (*tlvp_format)(void **data_out, void *data_in, size_t size_in);
};

struct tlv_group {
	const char *tlvg_pattern;
	enum tlv_code tlvg_id_first;
	enum tlv_code tlvg_id_last;
	enum tlv_spec tlvg_spec;
	ssize_t (*tlvg_parse)(void **data_out, void *data_in, size_t size_in, const char *param);
	ssize_t (*tlvg_format)(void **data_out, void *data_in, size_t size_in, char **param);
};

/* Robosoft data-model module: a self-contained set of property and group
 * tables. The built-in tables are one such module; additional modules
 * (extensions) plug in at link time via the section mechanism below. Both
 * tables are NULL-terminated and either may be NULL when unused. */
struct robosoft_tlv_module {
	const char *name;
	struct tlv_property *properties;
	struct tlv_group *groups;
};

/* Register a module by appending its address to the dedicated linker
 * section. The compiler/linker collect all registrations at link time;
 * GNU ld auto-creates __start_/__stop_ symbols for sections whose names
 * are valid C identifiers. */
#define ROBOSOFT_TLV_MODULE_REGISTER(mod)				\
	static const struct robosoft_tlv_module *const			\
	__robosoft_tlv_mod_##mod					\
	__attribute__((section("robosoft_tlv_modules"), used)) = &(mod)

extern const struct robosoft_tlv_module *const __start_robosoft_tlv_modules[];
extern const struct robosoft_tlv_module *const __stop_robosoft_tlv_modules[];

#define ROBOSOFT_TLV_FOREACH_MODULE(iter)				\
	for ((iter) = __start_robosoft_tlv_modules;			\
	     (iter) < __stop_robosoft_tlv_modules; (iter)++)

#endif /* __FIRMUX_TLV_H */
