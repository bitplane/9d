#!/bin/sh
set -eu

root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
destination="$root/libixp"

if [ ! -e "$destination" ]; then
    exit 0
fi
if [ -d "$destination/.git" ]; then
    if [ -n "$(git -C "$destination" status --porcelain)" ]; then
        echo "libixp has local changes; refusing to remove it." >&2
        exit 1
    fi
elif [ ! -f "$destination/.9d-commit" ]; then
    echo "$destination is not a recognised managed dependency." >&2
    exit 1
fi
rm -rf "$destination"
