#!/bin/sh
set -eu

root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
. "$root/deps.mk"
destination="$root/libixp"
temporary="$root/.libixp.tmp.$$"

cleanup() {
    if [ -d "$temporary" ]; then
        rm -rf "$temporary"
    fi
}
trap cleanup EXIT HUP INT TERM

if [ -f "$destination/.simple9p-commit" ]; then
    actual=$(sed -n '1p' "$destination/.simple9p-commit")
    if [ "$actual" != "$LIBIXP_COMMIT" ]; then
        echo "Vendored libixp is $actual, expected $LIBIXP_COMMIT" >&2
        exit 1
    fi
    exit 0
fi

if [ -d "$destination/.git" ]; then
    actual=$(git -C "$destination" rev-parse HEAD)
    if [ "$actual" != "$LIBIXP_COMMIT" ]; then
        echo "libixp is $actual, expected $LIBIXP_COMMIT" >&2
        echo "Run 'make distclean' before fetching the pinned dependency." >&2
        exit 1
    fi
    if [ -n "$(git -C "$destination" status --porcelain)" ]; then
        echo "libixp has local changes; refusing to use it as a release input." >&2
        exit 1
    fi
    exit 0
fi

if [ -e "$destination" ]; then
    echo "$destination exists but is not a recognised libixp checkout." >&2
    exit 1
fi

git clone --depth 1 --branch "$LIBIXP_REF" "$LIBIXP_URL" "$temporary"
actual=$(git -C "$temporary" rev-parse HEAD)
if [ "$actual" != "$LIBIXP_COMMIT" ]; then
    echo "libixp tag resolved to $actual, expected $LIBIXP_COMMIT" >&2
    exit 1
fi
mv "$temporary" "$destination"
