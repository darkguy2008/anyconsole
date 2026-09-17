#!/bin/sh
set -eu
cd "$(dirname "$0")"
exec qemu-system-x86_64 \
  -machine q35,accel=kvm -cpu host -smp 4 -m 4096 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=efivars.fd \
  -device ich9-ahci,id=ahci -drive file=disk.img,format=raw,if=none,id=d0 -device ide-hd,drive=d0,bus=ahci.0 \
  -nic user,model=e1000e \
  -vga std -vnc :0 \
  -monitor unix:monitor.sock,server,nowait \
  -serial file:boot.log
