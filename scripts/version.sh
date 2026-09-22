#!/bin/sh
set -eu

root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)

# Release archives preserve this information without needing Git installed.
if [ -f "$root/.release-version" ]; then
    {
        IFS= read -r version
        IFS= read -r date
    } < "$root/.release-version"
elif [ -e "$root/.git" ]; then
    version=$(git -C "$root" describe --tags --match 'v[0-9]*' --dirty 2>/dev/null) ||
        version="0.0.0+g$(git -C "$root" rev-parse --short HEAD)"
    version=${version#v}
    date=$(TZ=UTC git -C "$root" log -1 --format=%cd --date=format-local:%d.%m.%Y)
else
    echo "Missing version metadata: build from a Git checkout or a release archive." >&2
    exit 1
fi

# Keep generated C string literals safe even for locally chosen tag names.
case "$version" in
    ''|*[!0-9A-Za-z.+-]*) echo "Invalid version: $version" >&2; exit 1 ;;
esac
case "$date" in
    ''|*[!0-9.]*) echo "Invalid source date: $date" >&2; exit 1 ;;
esac
printf '#define NINED_VERSION "%s"\n#define NINED_DATE "%s"\n' "$version" "$date"
