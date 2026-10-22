#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

cleanup() {
	make clean >/dev/null
}
trap cleanup EXIT

make clean >/dev/null
make NETWORK=1 >/dev/null
./build/9d -h 2>&1 \
	| grep -q 'Otherwise listen on a libixp network address'
./build/9d -h 2>&1 \
	| grep -q 'default: tcp!localhost!564'
nm build/9d | grep -q '[[:space:]]ixp_announce$'

make NETWORK=0 CFLAGS=-Os >/dev/null
if ./build/9d -h 2>&1 \
		| grep -q 'Otherwise listen on a libixp network address'; then
	echo 'Network help present in NETWORK=0 build' >&2
	exit 1
fi
if nm build/9d | grep -q '[[:space:]]ixp_announce$'; then
	echo 'Network transport linked in NETWORK=0 build' >&2
	exit 1
fi
for object in rpc socket client; do
	if ar t build/libixp.a | grep -q "^$object.o$"; then
		echo "Network-only object retained with NETWORK=0: $object.o" >&2
		exit 1
	fi
done

# Switch back without cleaning, including platform-specific shared flags.
make NETWORK=1 PLATFORM=riscos STATIC=0 >/dev/null
if nm -u build/fs_io.o | grep -q '[[:space:]]pread$'; then
	echo 'RISC OS build retained POSIX positioned I/O' >&2
	exit 1
fi
make NETWORK=1 PLATFORM=posix STATIC=0 >/dev/null
nm -u build/fs_io.o | grep -q '[[:space:]]pread$'
./build/9d -h 2>&1 | grep -q 'Otherwise listen on a libixp network address'

# A changed fid limit must rebuild both the server and its test client.
make STATIC=0 MAX_FIDS=512 test-protocol >/dev/null
make STATIC=0 MAX_FIDS=1024 test-protocol >/dev/null

make NETWORK=0 PLATFORM=posix THREAD_LIBS= STATIC=0 >/dev/null

make NETWORK=0 LINK.c="sh $(pwd)/test/link.sh" STATIC=0 >/dev/null
