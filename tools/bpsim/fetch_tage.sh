#!/usr/bin/env bash
# Fetch Andre Seznec's CBP-2016 TAGE-SC-L (8KB and 64KB) into .cache/third_party/tage (not committed:
# the archive carries no license text). Verifies the archive's sha256.
set -euo pipefail
cd "$(dirname "$0")/../.."
URL=http://www.jilp.org/cbp2016/code/AndreSeznecLimited.tar.gz
SHA=5ed93324d8b06f1fc53f47a75f9e445b07c10573a49a08d1e4621dacd5ace181
D=.cache/third_party/tage
mkdir -p "$D"
[ -f "$D/a.tar.gz" ] || curl -sSfL -m 120 -o "$D/a.tar.gz" "$URL"
echo "$SHA  $D/a.tar.gz" | sha256sum -c -
tar xzf "$D/a.tar.gz" -C "$D" --no-same-owner
