#!/bin/sh
set -eu
archive=${1:?usage: publish.sh <archive> <release-tag>}
tag=${2:?usage: publish.sh <archive> <release-tag>}
version=${tag#v}
case "$tag" in v[0-9]*) ;; *) echo "Expected a release tag beginning with v" >&2; exit 1 ;; esac
case "$version" in ''|*[!0-9.]*) echo "Invalid release version" >&2; exit 1 ;; esac
name=$(basename "$archive")
case "$name" in
    "9d-$version-i386-aros.tar.bz2") arch=i386 ;;
    "9d-$version-aarch64-aros.tar.bz2") arch=aarch64 ;;
    "9d-$version-x86_64-aros.tar.bz2") arch=x86_64 ;;
    *) echo "Archive name does not match the release or an AROS target" >&2; exit 1 ;;
esac
pkg=${PKG:-pkg}
: "${PKG_SIGNKEY:?Set PKG_SIGNKEY to your signing key file}"
repo=${GITHUB_REPOSITORY:-bitplane/9d}
channel_url=${PKG_CHANNEL_URL:-https://aros-pkg.azurewebsites.net/bitplane}
archive=$(CDPATH='' cd "$(dirname "$archive")" && pwd)/$name
work=$(mktemp -d "$(dirname "$archive")/.publish-aros.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
upstream="https://github.com/$repo/releases/download/$tag/$name"
package=9d
kind=application
short='9P file server for stdio and serial'
source="$archive!/$package"
"$pkg" MANIFEST "$source" KIND "$kind" > "$work/manifest"
for field in "Name: $package" "Version: $version" "Architecture: $arch"; do
    grep -Fxq "$field" "$work/manifest" || { echo "Unexpected package metadata: expected $field" >&2; exit 1; }
done
"$pkg" PUBLISH "$source" CHANNEL "$work/channel" KIND "$kind" \
    UPSTREAM "$upstream" SHORT "$short" \
    HOMEPAGE "https://github.com/$repo" REPOSITORY "https://github.com/$repo" \
    LICENSE 'LicenseRef-9d AND MIT' DISTRIBUTION open-source
"$pkg" PUSH CHANNEL "$work/channel" TO "$channel_url"
