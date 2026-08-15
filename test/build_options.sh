#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

cleanup() {
	make clean >/dev/null
}
trap cleanup EXIT

make clean >/dev/null
make NETWORK=1 >/dev/null
./build/simple9p -h 2>&1 \
	| grep -q 'Otherwise listen on a libixp network address'
./build/simple9p -h 2>&1 \
	| grep -q 'default: tcp!localhost!564'
nm build/simple9p | grep -q '[[:space:]]ixp_announce$'

make clean >/dev/null
make NETWORK=0 CFLAGS=-Os >/dev/null
if ./build/simple9p -h 2>&1 \
		| grep -q 'Otherwise listen on a libixp network address'; then
	echo 'Network help present in NETWORK=0 build' >&2
	exit 1
fi
if nm build/simple9p | grep -q '[[:space:]]ixp_announce$'; then
	echo 'Network transport linked in NETWORK=0 build' >&2
	exit 1
fi
for object in rpc socket client; do
	if test -e "build/libixp/$object.o"; then
		echo "Network-only libixp object built with NETWORK=0: $object.o" >&2
		exit 1
	fi
done

make clean >/dev/null
make NETWORK=0 PLATFORM=posix THREAD_LIBS= STATIC=0 >/dev/null

make clean >/dev/null
make NETWORK=0 LINK.c="sh $(pwd)/test/link.sh" STATIC=0 >/dev/null
