#ifndef __FIRMUX_TLV_H
#define __FIRMUX_TLV_H

enum tlv_code {
	EEPROM_ATTR_NONE,

	/* Product related attributes */
	EEPROM_ATTR_PRODUCT_ID,                 /* char*, ASCII */
	EEPROM_ATTR_PRODUCT_NAME,               /* char*, ASCII */
	EEPROM_ATTR_SERIAL_NO,                  /* char*, ASCII */

	/* PCB related attributes */
	EEPROM_ATTR_PCB_NAME = 16,              /* char*, "FooBar" */
	EEPROM_ATTR_PCB_REVISION,               /* char*, "0002" */
	EEPROM_ATTR_PCB_PRDATE,                 /* time_t, seconds */
	EEPROM_ATTR_PCB_PRLOCATION,             /* char*, "Kaunas" */
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

#endif /* __FIRMUX_TLV_H */
