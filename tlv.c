
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "tlv.h"

#define TLV_EMPTY 0xFF
#define TLV_PAD 0x00

#define TLV_PROPS(tlv) tlv->type, ntohs(tlv->length), ((void *)tlv - (void *)tlvs->base)

#ifdef DEBUG
#define TLV_DEBUG(op, tlv) fprintf(stderr, op " TLV[%x] data # %i @ 0x%02lx\n", TLV_PROPS(tlv))
#else
#define TLV_DEBUG(op, tlv)
#endif

struct tlv_store *tlvs_init(void *mem, int len)
{
	struct tlv_store *tlvs;

	assert(mem != NULL);
	assert(len != 0);

	tlvs = calloc(1, sizeof(*tlvs));
	if (!tlvs) {
		perror("calloc() failed");
		return NULL;
	}

	tlvs->size = len;
	tlvs->base = mem;

	return tlvs;
}

void tlvs_free(struct tlv_store *tlvs)
{
	free(tlvs);
}

void tlvs_reset(struct tlv_store *tlvs)
{
	memset(tlvs->base, TLV_EMPTY, tlvs->size);

	tlvs->dirty = 1;
}

void tlvs_optimise(struct tlv_store *tlvs)
{
	struct tlv_field *tlv;
	void *last, *curr, *save;
	int count;

	if (!tlvs->frag)
		return;

	save = tlvs->base;
	curr = tlvs->base;
	last = tlvs->base + tlvs->size;

	while ((curr + sizeof(struct tlv_field)) < last) {
		tlv = curr;
		if (tlv->type == TLV_EMPTY)
			break;
		/* Padding (holes) handling */
		if (tlv->type == TLV_PAD) {
			curr++;
			continue;
		}
		count = ntohs(tlv->length) + sizeof(struct tlv_field);
		if (save != curr) {
			memcpy(save, curr, count);
		}
		save += count;
		curr += count;
	}

	if (save != curr)
		memset(save, TLV_EMPTY, curr - save);

	tlvs->dirty = 1;
}

static struct tlv_field *tlvs_gap(struct tlv_store *tlvs, uint16_t length)
{
	void *last, *curr;
	struct tlv_field *tlv = NULL;
	struct tlv_field *gap = NULL;
	size_t gap_len = 0, tmp_len;

	curr = tlvs->base;
	last = tlvs->base + tlvs->size;

	while ((curr + sizeof(struct tlv_field)) < last) {
		tlv = curr;
		if (tlv->type == TLV_EMPTY) {
			if (curr + sizeof(struct tlv_field) + length >= last)
				tlv = NULL;
			break;
		}
		if (tlv->type == TLV_PAD) {
			while (curr < last && ((struct tlv_field *)curr)->type == TLV_PAD)
				curr++;
			if (curr == last) {
				tlv = NULL;
				break;
			}
			tmp_len = (curr - (void *)tlv) - sizeof(struct tlv_field);
			if ((tmp_len >= length) &&
			    (!gap || tmp_len < gap_len)) {
				gap = tlv;
				gap_len = tmp_len;
			}
			if (gap_len == length)
				break;
			continue;
		}
		curr += ntohs(tlv->length) + sizeof(struct tlv_field);
	}

	return gap ? gap : tlv;
}

static struct tlv_field *tlvs_find(struct tlv_store *tlvs, uint8_t type)
{
	void *last, *curr;
	struct tlv_field *tlv;

	curr = tlvs->base;
	last = tlvs->base + tlvs->size;

	while ((curr + sizeof(struct tlv_field)) < last) {
		tlv = curr;
		if (tlv->type == type)
			return tlv;
		if (tlv->type == TLV_EMPTY)
			break;
		/* Padding (holes) handling */
		if (tlv->type == TLV_PAD) {
			curr++;
			continue;
		}
		curr += ntohs(tlv->length) + sizeof(struct tlv_field);
	}

	return NULL;
}

static int tlvs_add_tail(struct tlv_store *tlvs, uint8_t type, uint16_t length, void *value)
{
	struct tlv_field *tlv;

	tlv = tlvs_gap(tlvs, length);
	if (!tlv) {
		fprintf(stderr, "TLV gap not found, type 0x%02x, length %d\n", type, length);
		return -ENOSPC;
	}

	tlv->type = type;
	tlv->length = htons(length);
	memcpy(tlv->value, value, length);
	tlvs->dirty = 1;
	TLV_DEBUG("New", tlv);
	return 0;
}

int tlvs_add(struct tlv_store *tlvs, uint8_t type, uint16_t length, void *value)
{
	struct tlv_field *tlv;

	assert(type != TLV_EMPTY && type != TLV_PAD);

	tlv = tlvs_find(tlvs, type);
	if (tlv)
		return -EEXIST;

	return tlvs_add_tail(tlvs, type, length, value);
}

int tlvs_set(struct tlv_store *tlvs, uint8_t type, uint16_t length, void *value)
{
	struct tlv_field *tlv;

	assert(type != TLV_EMPTY && type != TLV_PAD);

	tlv = tlvs_find(tlvs, type);
	if (!tlv)
		return tlvs_add_tail(tlvs, type, length, value);

	if (ntohs(tlv->length) == length) {
		memcpy(tlv->value, value, length);
		tlvs->dirty = 1;
		TLV_DEBUG("Set", tlv);
		return 0;
	}

	tlvs->frag = 1;

	if (ntohs(tlv->length) > length) {
		memset(tlv->value, TLV_PAD, ntohs(tlv->length));
		memcpy(tlv->value, value, length);
		tlv->length = htons(length);
		tlvs->dirty = 1;
		TLV_DEBUG("Set", tlv);
		return 0;
	} else if (ntohs(tlv->length) < length) {
		memset(tlv, TLV_PAD, sizeof(*tlv) + ntohs(tlv->length));
		return tlvs_add_tail(tlvs, type, length, value);
	}
}

int tlvs_del(struct tlv_store *tlvs, uint8_t type)
{
	struct tlv_field *tlv;

	assert(type != TLV_EMPTY && type != TLV_PAD);

	tlv = tlvs_find(tlvs, type);
	if (!tlv)
		return -ENOENT;

	tlvs->frag = 1;
	tlvs->dirty = 1;
	memset(tlv, TLV_PAD, sizeof(*tlv) + ntohs(tlv->length));
	TLV_DEBUG("Delete", tlv);
	return 0;
}

size_t tlvs_len(struct tlv_store *tlvs)
{
	void *item;

	item = tlvs_find(tlvs, TLV_EMPTY);
	if (item) {
		return item - tlvs->base;
	} else {
		return tlvs->size;
	}
}

ssize_t tlvs_get(struct tlv_store *tlvs, uint8_t type, int len, char *buf)
{
	struct tlv_field *tlv;
	int cnt, flen;

	tlv = tlvs_find(tlvs, type);
	if (!tlv)
		return -1;

	flen = ntohs(tlv->length);
	if (!buf)
		return flen;

	cnt = len < flen ? len : flen;
	memcpy(buf, tlv->value, cnt);
	/* ASCII termination when tailroom is available */
	if (len > flen)
		buf[flen] = '\0';

	return cnt;
}

void tlvs_dump(struct tlv_store *tlvs)
{
	struct tlv_iterator iter;
	struct tlv_field *tlv;
	int i;

	tlvs_iter_init(&iter, tlvs);

	while ((tlv = tlvs_iter_next(&iter)) != NULL) {
		printf("TLV[%x] data # %i @ 0x%02lx: ", TLV_PROPS(tlv));
		for (i = 0; i < ntohs(tlv->length); i++) {
			printf("0x%02x ", tlv->value[i]);
		}
		printf("| ");
		for (i = 0; i < ntohs(tlv->length); i++) {
			printf("%c", (tlv->value[i] > 31 &&
				      tlv->value[i] < 127) ?
			       tlv->value[i] : '.');
		}
		printf("\n");
	}
}

void tlvs_iter_init(struct tlv_iterator *iter, struct tlv_store *tlvs)
{
	if (!iter || !tlvs)
		return;

	iter->tlvs = tlvs;
	iter->curr = tlvs->base;
}

struct tlv_field *tlvs_iter_next(struct tlv_iterator *iter)
{
	void *last;
	struct tlv_field *tlv;

	if (!iter)
		return NULL;

	last = iter->tlvs->base + iter->tlvs->size;

	while ((iter->curr + sizeof(struct tlv_field)) < last) {
		tlv = iter->curr;
		/* End of TLV storage */
		if (tlv->type == TLV_EMPTY)
			return NULL;
		/* Padding (holes) handling */
		if (tlv->type == TLV_PAD) {
			iter->curr++;
			continue;
		}

		/* Move cursor to next entry for subsequent calls */
		iter->curr += ntohs(tlv->length) + sizeof(struct tlv_field);

		return tlv;
	}

	return NULL;
}
