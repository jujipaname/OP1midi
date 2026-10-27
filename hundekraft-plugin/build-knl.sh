#!/bin/sh
# Pack du plugin op1midi pour Hundekraft 3.0.3 (out: hundekraft-plugin/op1midi.knl)
# usage: hundekraft-plugin/build-knl.sh [chemin_bundle_source]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUNDLE=${1:-/tmp/opencode/hk/hundekraft-3.0.3-source}
KENNEL="$BUNDLE/ext/kennel"
DST="$KENNEL/synth_sdk/plugins/op1midi"

[ -d "$KENNEL" ] || { echo "bundle introuvable: $KENNEL"; exit 1; }
command -v docker >/dev/null || { echo "docker requis"; exit 1; }

rm -rf "$DST"
mkdir -p "$DST" "$KENNEL/build"
cp "$HERE/op1midi/plugin.json" "$HERE/op1midi/mm2.h" "$HERE/op1midi/"*.c "$DST/"

docker run --rm -u "$(id -u):$(id -g)" -v "$KENNEL":/work -w /work bfin-elf-toolchain-py \
    python3 tools/kennel/pack.py synth_sdk/plugins/op1midi -o build/op1midi.knl

cp "$KENNEL/build/op1midi.knl" "$HERE/op1midi.knl"
ls -la "$HERE/op1midi.knl"
