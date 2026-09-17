#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
. vm/guest/otad.conf
VERSION=$(cat VERSION)
KERNEL_VERSION=$(basename /lib/modules/*-lts)
STAGE=vm/stage
INITRAMFS_CACHE=vm/cache/initramfs-$KERNEL_VERSION
rm -rf $STAGE
mkdir -p $STAGE/boot vm/cache
[ -f "$INITRAMFS_CACHE" ] ||
  mkinitfs -F "base ata scsi usb nvme ext4 kms" -o "$INITRAMFS_CACHE" "$KERNEL_VERSION"
cp "$INITRAMFS_CACHE" $STAGE/boot/initramfs
cp /boot/vmlinuz-lts $STAGE/boot/kernel
cp --parents -t $STAGE "/lib/modules/$KERNEL_VERSION/modules.builtin"
for module in $MODULES; do modprobe --show-depends "$module"; done |
  awk '$1 == "insmod" { print $2 }' | sort -u | xargs cp --parents -t $STAGE
depmod -b "$PWD/$STAGE" "$KERNEL_VERSION"
docker run --rm -e VERSION="$VERSION" -v "$PWD:/src" debian:trixie sh /src/vm/mkroot.in
rm -rf $STAGE "vm/rel/$VERSION"
mkdir -p "vm/rel/$VERSION"
tar -xzf vm/rootfs.tar.gz -C "vm/rel/$VERSION"
