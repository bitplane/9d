CC ?= gcc
CFLAGS += -g -O0 -D_XOPEN_SOURCE=600 -Ilibixp/include
LDFLAGS += -static
LIBS = build/libixp.a -lpthread

NETWORK ?= 1
ifeq ($(NETWORK),0)
CFLAGS += -DSIMPLE9P_NO_NETWORK
else ifneq ($(NETWORK),1)
$(error NETWORK must be 0 or 1)
endif

PLATFORM ?= posix
SRCS = simple9p.c path.c namespace.c platform_$(PLATFORM).c \
       fs_ops.c fs_io.c fs_stat.c fs_dir.c
OBJS = $(patsubst %.c,build/%.o,$(SRCS))
TARGET = build/simple9p

all: build libixp $(TARGET)

$(TARGET): $(OBJS) libixp
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

build/%.o: %.c server.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

libixp: | build
	cd libixp/lib/libixp && \
	for f in convert.c error.c map.c message.c request.c rpc.c server.c socket.c transport.c util.c timer.c client.c thread.c; do \
		$(CC) $(CFLAGS) -I../../include -c $$f -o $$(pwd)/../../../build/$${f%.c}.o || exit 1; \
	done
	ar rcs build/libixp.a build/convert.o build/error.o build/map.o build/message.o \
		build/request.o build/rpc.o build/server.o build/socket.o build/transport.o \
		build/util.o build/timer.o build/client.o build/thread.o

clean:
	rm -rf build

test/9pfuse/build/9pfuse:
	@if [ ! -d test/9pfuse ]; then \
		git clone -b qemount https://github.com/bitplane/9pfuse.git test/9pfuse; \
	fi
	cd test/9pfuse && meson setup build && meson compile -C build

test: test/9pfuse/build/9pfuse
	$(MAKE) test-build-options
	$(MAKE) test-namespace
	$(MAKE) $(TARGET)
	cd test && ./run.sh

test-build-options:
	./test/build_options.sh

test-namespace: build/namespace_test
	./build/namespace_test

build/namespace_test: test/namespace_test.c namespace.c namespace.h path.c server.h | build
	$(CC) $(CFLAGS) -o $@ test/namespace_test.c namespace.c path.c

build/simple9p-synthetic: simple9p.c path.c namespace.c test/platform_synthetic.c \
                         fs_ops.c fs_io.c fs_stat.c fs_dir.c server.h namespace.h \
                         libixp | build
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ simple9p.c path.c namespace.c \
		test/platform_synthetic.c fs_ops.c fs_io.c fs_stat.c fs_dir.c $(LIBS)

.PHONY: all clean libixp test test-build-options test-namespace
