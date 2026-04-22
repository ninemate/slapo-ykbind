OPENLDAP_SRC ?= $(CURDIR)/openldap-src
OPENLDAP_BUILD ?= $(OPENLDAP_SRC)
PREFIX ?= /usr/local
MODULE_DIR ?= $(PREFIX)/lib/openldap

CPPFLAGS += -I$(OPENLDAP_BUILD)/include
CPPFLAGS += -I$(OPENLDAP_SRC)/include
CPPFLAGS += -I$(OPENLDAP_SRC)/servers/slapd
CPPFLAGS += -I$(OPENLDAP_SRC)/libraries/liblber
CPPFLAGS += -I$(OPENLDAP_SRC)/libraries/libldap
CPPFLAGS += -DSLAPD_OVER_YKBIND=SLAPD_MOD_DYNAMIC

CFLAGS ?= -O2 -g -fPIC -Wall -Wextra -Wformat -Werror=format-security
LDFLAGS += -shared
LDLIBS += -lcrypto

TARGET = ykbind.so
SOURCES = slapo-ykbind.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

install: $(TARGET)
	install -d $(DESTDIR)$(MODULE_DIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(MODULE_DIR)/$(TARGET)

clean:
	rm -f $(TARGET)
