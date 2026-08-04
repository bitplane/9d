#!/bin/sh
set -eu

root=$(CDPATH= cd "$(dirname "$0")" && pwd)
cd "$root"
make deps
exec make "$@"
