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
