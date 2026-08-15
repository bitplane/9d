#!/bin/sh
set -eu

exec "${CC:-cc}" "$@"
