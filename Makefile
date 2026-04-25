ifneq ($(DEBUG),)
CFLAGS += -DDEBUG -g
endif
ifneq ($(NODEBUG),)
CFLAGS += -DDEBUG_STRIP
endif
ifneq ($(CONFIG_TLVS_FILE),)
CFLAGS += -DTLVS_DEFAULT_FILE=\"$(CONFIG_TLVS_FILE)\"
endif
ifneq ($(CONFIG_TLVS_SIZE),)
CFLAGS += -DTLVS_DEFAULT_SIZE=$(CONFIG_TLVS_SIZE)
endif
ifneq ($(CONFIG_TLVS_OFFSET),)
CFLAGS += -DTLVS_DEFAULT_OFFSET=$(CONFIG_TLVS_OFFSET)
endif
ifneq ($(CONFIG_TLVS_COMPRESSION),)
CFLAGS += -DTLVS_DEFAULT_COMPRESSION=$(CONFIG_TLVS_COMPRESSION)
endif
ifeq ($(CONFIG_TLVS_COMPRESSION_NONE),)
CFLAGS += -DHAVE_LZMA_H
LDLIBS += -llzma
endif

# XXX: default supported datamodels configuration
CONFIG_DM_TOBUFI_LEGACY = y
CONFIG_DM_ROBOSOFT = y

SRCS-$(CONFIG_DM_TOBUFI_LEGACY) += datamodel-toblse-tlv.o
SRCS-$(CONFIG_DM_ROBOSOFT) += datamodel-robosoft-tlv.o

# Auto-include any data-model extension .mk plugins. Each plugin is a
# self-contained pair of files dropped into ext/:
#   ext/<model>-<extension>.mk - declares CONFIG_DM_*_<EXTENSION> and appends to SRCS-y
#   ext/datamodel-<model>-<extension>.c - the extension source
# Adding a new extension requires no edits to this Makefile.
-include $(wildcard ext/*.mk)

# Storage backend selection (only one is compiled):
# Priority: MTD > MMAP > BWB (buffered write-back fallback)
ifdef CONFIG_IO_MTD
  SRCS-y += char-mtd.o
else ifdef CONFIG_IO_MMAP
  SRCS-y += char-mmap.o
else
  SRCS-y += char-bwb.o
endif

SRCS = $(SRCS-y) protocol.o tlv.o utils.o crc.o main.o
OBJS = $(SRCS:%.c=%.o)

.PHONY: all
all: tlvs

.PHONY: clean
clean:
	rm -f ext/*.o *.o tlvs

.PHONY: install
install: tlvs
	install -Dm755 tlvs $(PREFIX)/usr/bin/tlvs

tlvs: $(SRCS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS-$<) -c -o $@ $<
