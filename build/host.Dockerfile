FROM alpine:3.24
RUN echo disable_trigger=1 > /etc/update-grub.conf \
 && apk add --no-cache linux-lts linux-firmware mkinitfs kmod grub grub-efi util-linux dosfstools e2fsprogs e2fsprogs-extra coreutils findutils ovmf docker-cli docker-cli-buildx
