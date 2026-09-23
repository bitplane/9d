#!/bin/sh
set -eu
umask 022
root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd)
target=${1:?usage: package.sh <i386-aros|aarch64-aros|x86_64-aros>}
case "$target" in i386-aros|aarch64-aros|x86_64-aros) ;; *) echo "Unknown AROS target: $target" >&2; exit 1 ;; esac
version=$(sed -n '1p' "$root/.release-version")
case "$version" in ''|*[!0-9.]*) echo "Expected release metadata from make dist" >&2; exit 1 ;; esac
# The source archive records a fixed timestamp, shared by every architecture.
epoch=$(sed -n '3p' "$root/.release-version")
case "$epoch" in ''|*[!0-9]*) echo "Missing source timestamp; recreate the source archive" >&2; exit 1 ;; esac
mkdir -p "$root/build" "$root/dist"
work=$(mktemp -d "$root/build/package-aros.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
package=9d
mkdir -p "$work/$package/C" "$work/$package/Help/9d"
cp "$root/build/9d" "$work/$package/C/9d"
chmod 755 "$work/$package/C/9d"
cp "$root/README.md" "$work/$package/Help/9d/README.md"
cp "$root/libixp/LICENSE" "$work/$package/Help/9d/libixp-license.md"
sed -n '/^## License/,$p' "$root/README.md" > "$work/$package/Help/9d/LICENSE.md"
test -s "$work/$package/Help/9d/LICENSE.md"
chmod 644 "$work/$package/Help/9d/"*
archive="$root/dist/9d-$version-$target.tar.bz2"
tar -C "$work" --sort=name --mtime="@$epoch" --owner=0 --group=0 \
    --numeric-owner -cjf "$archive" "$package"
(cd "$root/dist" && sha256sum "$(basename "$archive")") > "$archive.sha256"
echo "$archive"
