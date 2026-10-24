# libaeon -- builds the shared library, a static library, and the test tool.
CC      ?= cc
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -Werror=implicit-function-declaration -fPIC -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -Wno-format-truncation -Wno-misleading-indentation
LDLIBS   = -ldl -lpthread   # V183: libcurl is dlopen'd at init, never linked
SRC      = aeon.c third_party/cJSON/cJSON.c
all: libaeon.so libaeon.a aeon-sdk-test
libaeon.so: $(SRC) aeon.h
	$(CC) $(CFLAGS) -shared -o $@ $(SRC) $(LDLIBS)
libaeon.a: $(SRC) aeon.h
	$(CC) $(CFLAGS) -c aeon.c -o aeon.o && $(CC) $(CFLAGS) -c third_party/cJSON/cJSON.c -o cJSON.o && ar rcs $@ aeon.o cJSON.o
aeon-sdk-test: aeon-sdk-test.c libaeon.a
	$(CC) $(CFLAGS) -o $@ aeon-sdk-test.c libaeon.a $(LDLIBS)
clean:
	rm -f libaeon.so libaeon.a aeon.o cJSON.o aeon-sdk-test
.PHONY: all clean
