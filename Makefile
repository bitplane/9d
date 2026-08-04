CC ?= gcc
CPPFLAGS += -D_XOPEN_SOURCE=700 -Ilibixp/include
CFLAGS += -g -O0
LDFLAGS += -static
LIBS = build/libixp.a -lpthread
LIBIXP_CFLAGS = $(filter-out -Werror,$(CFLAGS))

NETWORK ?= 1
ifeq ($(NETWORK),0)
CPPFLAGS += -DSIMPLE9P_NO_NETWORK
else ifneq ($(NETWORK),1)
$(error NETWORK must be 0 or 1)
endif

PLATFORM ?= posix
SRCS = simple9p.c alloc.c path.c namespace.c platform_$(PLATFORM).c \
       fs_ops.c fs_io.c fs_stat.c fs_dir.c
OBJS = $(patsubst %.c,build/%.o,$(SRCS))
TARGET = build/simple9p

all: build libixp $(TARGET)

$(TARGET): $(OBJS) libixp
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

build/%.o: %.c server.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

libixp: | build
	cd libixp/lib/libixp && \
	for f in convert.c error.c map.c message.c request.c rpc.c server.c socket.c transport.c util.c timer.c client.c thread.c; do \
		$(CC) $(CPPFLAGS) $(LIBIXP_CFLAGS) -I../../include -c $$f -o $$(pwd)/../../../build/$${f%.c}.o || exit 1; \
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
	$(MAKE) test-allocations
	$(MAKE) test-protocol
	$(MAKE) $(TARGET)
	cd test && ./run.sh

test-build-options:
	./test/build_options.sh

test-namespace: build/namespace_test
	./build/namespace_test

test-protocol: build/protocol_test build/simple9p
	./build/protocol_test ./build/simple9p

test-allocations: build/allocation_test
	./build/allocation_test

build/namespace_test: test/namespace_test.c namespace.c namespace.h path.c alloc.c server.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ test/namespace_test.c namespace.c path.c alloc.c

build/protocol_test: test/protocol_test.c libixp | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ test/protocol_test.c $(LIBS)

build/allocation_test: test/allocation_test.c alloc.c path.c namespace.c \
		platform_posix.c fs_ops.c fs_stat.c libixp | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSIMPLE9P_TESTING -o $@ \
		test/allocation_test.c alloc.c path.c namespace.c platform_posix.c \
		fs_ops.c fs_stat.c $(LIBS)

build/simple9p-synthetic: simple9p.c alloc.c path.c namespace.c test/platform_synthetic.c \
						 fs_ops.c fs_io.c fs_stat.c fs_dir.c server.h namespace.h \
						 libixp | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ simple9p.c alloc.c path.c namespace.c \
		test/platform_synthetic.c fs_ops.c fs_io.c fs_stat.c fs_dir.c $(LIBS)

.PHONY: all clean libixp test test-build-options test-namespace test-protocol \
	test-allocations
