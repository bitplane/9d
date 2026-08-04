#!/bin/sh
set -eu

root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
. "$root/deps.mk"
version=${VERSION#v}

old_ifs=$IFS
case "$version" in
    ''|*[!0-9.]*)
        echo "Set VERSION to a semantic version such as 0.6.0." >&2
        exit 1
        ;;
esac
IFS=.
set -- $version
IFS=$old_ifs
if [ "$#" -ne 3 ]; then
    echo "Set VERSION to a semantic version such as 0.6.0." >&2
    exit 1
fi
for component do
    case "$component" in
        ''|*[!0-9]*)
            echo "Set VERSION to a semantic version such as 0.6.0." >&2
            exit 1
            ;;
    esac
done

if [ "${DIST_ALLOW_DIRTY:-0}" != 1 ] &&
   [ -n "$(git -C "$root" status --porcelain --untracked-files=all)" ]; then
    echo "The source tree must be clean before creating a release archive." >&2
    exit 1
fi
"$root/scripts/deps.sh"

actual=$(git -C "$root/libixp" rev-parse HEAD)
if [ "$actual" != "$LIBIXP_COMMIT" ]; then
    echo "libixp is $actual, expected $LIBIXP_COMMIT" >&2
    exit 1
fi

package="simple9p-$version"
stage_root="$root/build/dist-stage"
stage="$stage_root/$package"
output="$root/dist"
archive="$output/$package.tar.xz"
epoch=$(git -C "$root" log -1 --format=%ct)

rm -rf "$stage_root"
mkdir -p "$stage" "$output"
git -C "$root" ls-files -z --cached --others --exclude-standard |
    tar -C "$root" --null -T - -cf - |
    tar -C "$stage" -xf -
mkdir -p "$stage/libixp"
git -C "$root/libixp" archive HEAD | tar -C "$stage/libixp" -xf -
printf '%s\n' "$LIBIXP_COMMIT" > "$stage/libixp/.simple9p-commit"

tar -C "$stage_root" --sort=name --mtime="@$epoch" \
    --owner=0 --group=0 --numeric-owner -cJf "$archive" "$package"
if command -v sha256sum >/dev/null 2>&1; then
    (cd "$output" && sha256sum "$(basename "$archive")") \
        > "$archive.sha256"
else
    (cd "$output" && shasum -a 256 "$(basename "$archive")") \
        > "$archive.sha256"
fi
rm -rf "$stage_root"
echo "$archive"
