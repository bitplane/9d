#!/bin/sh
set -eu
tag=${1:?usage: upload.sh <tag> <asset>...}
shift
[ "$#" -gt 0 ]
repo=${GITHUB_REPOSITORY:-bitplane/9d}
mkdir -p "${TMPDIR:-$HOME/tmp}"
work=$(mktemp -d "${TMPDIR:-$HOME/tmp}/9d-upload.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
gh release view "$tag" --repo "$repo" --json assets --jq '.assets[].name' > "$work/assets"
for asset do
    name=$(basename "$asset")
    if grep -Fxq "$name" "$work/assets"; then
        gh release download "$tag" --repo "$repo" --pattern "$name" --dir "$work"
        if ! cmp -s "$asset" "$work/$name"; then
            echo "Refusing to replace published asset $name; use a new release version." >&2
            exit 1
        fi
        rm "$work/$name"
        echo "Already uploaded: $name"
    else
        gh release upload "$tag" --repo "$repo" "$asset"
        printf '%s\n' "$name" >> "$work/assets"
    fi
done
