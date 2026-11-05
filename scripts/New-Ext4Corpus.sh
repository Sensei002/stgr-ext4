#!/usr/bin/env bash
#
# SPDX-License-Identifier: MIT
#
# Generates the EXT4 image corpus consumed by the engine host tests. Linux
# only: it needs mke2fs and e2fsck from e2fsprogs.
#
# Output in <outdir>:
#   <name>.img        the image
#   expectations.txt  "<name> <status> <blockSize>" for every image
#   manifest.sha256   sha256sum of every image
#   toolchain.txt     e2fsprogs version and the exact mke2fs command lines
#   baseline.txt      e2fsck -fn result for the images expected to be accepted
#
# The images are evidence, not fixtures: they are not committed. The SHA-256
# manifest lets the Windows host tests prove they parsed the exact bytes that
# Linux produced.
#
# The accepted images disable metadata_csum on purpose. CRC-32C is not
# implemented, so a checksummed volume is refused rather than trusted; the
# corpus pins that refusal instead of hiding it.

set -euo pipefail

outdir="${1:-artifacts/corpus}"
image_mb=16

if ! command -v mke2fs >/dev/null 2>&1; then
    echo 'mke2fs not found; install e2fsprogs' >&2
    exit 1
fi
if ! command -v truncate >/dev/null 2>&1; then
    echo 'truncate not found; coreutils is required' >&2
    exit 1
fi

mkdir -p "$outdir"
rm -f "$outdir"/*.img
: > "$outdir/expectations.txt"
: > "$outdir/toolchain.txt"
: > "$outdir/baseline.txt"

mke2fs -V >> "$outdir/toolchain.txt" 2>&1

create() {
    local name="$1"
    local expect="$2"
    local blocksize="$3"
    local img="$outdir/$name.img"

    shift 3
    truncate -s "${image_mb}M" "$img"
    printf 'mke2fs -q -F -t ext4 %s %s\n' "$*" "$name.img" >> "$outdir/toolchain.txt"
    mke2fs -q -F -t ext4 "$@" "$img"
    printf '%s %s %s\n' "$name" "$expect" "$blocksize" >> "$outdir/expectations.txt"
}

#
# Accepted: every field the engine reads is checksum-free and supported.
#
create accept-1k              StgrStatusSuccess 1024 -b 1024 -O ^metadata_csum
create accept-2k              StgrStatusSuccess 2048 -b 2048 -O ^metadata_csum
create accept-4k              StgrStatusSuccess 4096 -b 4096 -O ^metadata_csum
create accept-4k-32byte-desc  StgrStatusSuccess 4096 -b 4096 -O ^metadata_csum,^64bit

#
# Refused: metadata_csum is the default, and the engine refuses to trust a
# volume whose checksums it cannot verify.
#
create refuse-metadata-csum   StgrStatusChecksumNotVerified 0 -b 4096

#
# Refused: incompatible or read-only-compatible features outside the supported
# matrix. metadata_csum is disabled so the feature refusal is the only reason.
#
create refuse-bigalloc        StgrStatusUnsupportedFeature 0 -b 4096 -O bigalloc,^metadata_csum -C 65536
create refuse-inline-data     StgrStatusUnsupportedFeature 0 -b 4096 -O inline_data,^metadata_csum
create refuse-encrypt         StgrStatusUnsupportedFeature 0 -b 4096 -O encrypt,^metadata_csum

#
# Refused: valid geometry with a corrupted superblock signature.
#
create refuse-magic           StgrStatusBadMagic 0 -b 4096 -O ^metadata_csum
# Superblock magic lives at byte 1024 + 0x38.
printf '\x00\x00' | dd of="$outdir/refuse-magic.img" bs=1 seek=1080 conv=notrunc status=none

#
# Baseline: the images that must parse are also clean according to e2fsck.
#
for name in accept-1k accept-2k accept-4k accept-4k-32byte-desc; do
    img="$outdir/$name.img"
    if e2fsck -fn "$img" >/dev/null 2>&1; then
        printf '%s: e2fsck -fn clean\n' "$name" >> "$outdir/baseline.txt"
    else
        printf '%s: e2fsck -fn reported problems\n' "$name" >> "$outdir/baseline.txt"
    fi
done

(
    cd "$outdir"
    sha256sum -- *.img > manifest.sha256
)

images=$(find "$outdir" -maxdepth 1 -name '*.img' | wc -l)
printf 'corpus: %s images in %s\n' "$images" "$outdir"
cat "$outdir/expectations.txt"
