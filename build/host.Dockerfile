ARG ALPINE=3.24
FROM alpine:$ALPINE AS kernel
ARG KERNEL=linux-lts
RUN apk add --no-cache alpine-sdk patch patchutils
COPY *.patch /patches/
COPY build-modules /usr/local/bin/
RUN . /etc/os-release \
 && git clone -q --depth 1 --filter=blob:none --sparse -b "${VERSION_ID%.*}-stable" https://gitlab.alpinelinux.org/alpine/aports.git /aports \
 && git -C /aports sparse-checkout set "main/$KERNEL" \
 && cd "/aports/main/$KERNEL" \
 && build-modules /patches /out

FROM alpine:$ALPINE
COPY --from=kernel /out/ /kernel/
RUN echo disable_trigger=1 > /etc/update-grub.conf \
 && apk add --no-cache /kernel/packages/*.apk linux-firmware mkinitfs kmod grub grub-efi util-linux dosfstools e2fsprogs e2fsprogs-extra coreutils findutils ovmf docker-cli docker-cli-buildx \
 && cp -a /kernel/modules/. /
