#ifndef __TLV_STORE_H
#define __TLV_STORE_H

#include <stdint.h>
#include <sys/types.h>

struct __attribute__ ((__packed__)) tlv_field {
        uint8_t type;
        uint16_t length;
        uint8_t value[0];
};

struct tlv_store {
	size_t size;
	void *base;
	int frag;
	int dirty;
	int immutable;
};

struct tlv_iterator {
	struct tlv_store *tlvs;
	void *curr;
};

struct tlv_store *tlvs_init(void *mem, int len);
void tlvs_free(struct tlv_store *tlvs);
void tlvs_reset(struct tlv_store *tlvs);
void tlvs_immutable(struct tlv_store *tlvs);
void tlvs_optimise(struct tlv_store *tlvs);
int tlvs_add(struct tlv_store *tlvs, uint8_t type, uint16_t length, void *value);
int tlvs_set(struct tlv_store *tlvs, uint8_t type, uint16_t length, void *value);
int tlvs_del(struct tlv_store *tlvs, uint8_t type);
size_t tlvs_len(struct tlv_store *tlvs);
ssize_t tlvs_get(struct tlv_store *tlvs, uint8_t type, int len, char *buf);
void tlvs_dump(struct tlv_store *tlvs);

void tlvs_iter_init(struct tlv_iterator *iter, struct tlv_store *tlvs);
struct tlv_field *tlvs_iter_next(struct tlv_iterator *iter);

#endif /* __TLV_STORE_H */
