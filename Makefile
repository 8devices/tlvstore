ifneq ($(DEBUG),)
CFLAGS += -DDEBUG -g
endif
ifneq ($(CONFIG_TLVS_FILE),)
CFLAGS += -DTLVS_DEFAULT_FILE=\"$(CONFIG_TLVS_FILE)\"
endif
ifneq ($(CONFIG_TLVS_SIZE),)
CFLAGS += -DTLVS_DEFAULT_SIZE=$(CONFIG_TLVS_SIZE)
endif
ifneq ($(CONFIG_TLVS_COMPRESSION),)
CFLAGS += -DTLVS_DEFAULT_COMPRESSION=$(CONFIG_TLVS_COMPRESSION)
endif
ifeq ($(CONFIG_TLVS_COMPRESSION_NONE),)
CFLAGS += -DHAVE_LZMA_H
LDLIBS += -llzma
endif

.PHONY: all
all: tlvs

.PHONY: clean
clean:
	rm -f *.o tlvs

.PHONY: install
install: tlvs
	install -Dm755 tlvs $(PREFIX)/usr/bin/tlvs

tlvs: datamodel-firmux-struct.o datamodel-firmux-fields.o datamodel-firmux-tlv.o datamodel-legacy-tlv.o protocol.o char.o tlv.o utils.o crc.o main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS-$<) -c -o $@ $<

main.o: main.c
protocol.o: protocol.c
tlv.o: tlv.c
char.o: char.c
utils.o: utils.c
crc.o: crc.c
datamodel-firmux-struct.o: datamodel-firmux-struct.c
datamodel-firmux-fields.o: datamodel-firmux-fields.c
datamodel-firmux-tlv.o: datamodel-firmux-tlv.c
datamodel-legacy-tlv.o: datamodel-legacy-tlv.c
