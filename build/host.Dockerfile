ARG ALPINE=3.24
FROM alpine:$ALPINE AS kernel
ARG KERNEL=linux-lts
RUN apk add --no-cache alpine-sdk patch \
 && . /etc/os-release \
 && git clone -q --depth 1 --filter=blob:none --sparse -b "${VERSION_ID%.*}-stable" https://gitlab.alpinelinux.org/alpine/aports.git /aports \
 && git -C /aports sparse-checkout set "main/$KERNEL" \
 && SUDO= abuild-keygen -a -i -n
COPY *.patch /patches/
WORKDIR /aports/main/$KERNEL
RUN abuild -F fetch unpack prepare \
 && pending= \
 && for fix in /patches/*.patch; do \
      patch -d src/linux-*/ -p1 -R -f -s --dry-run < "$fix" >/dev/null || { cp "$fix" . && echo "source=\"\$source ${fix##*/}\"" >> APKBUILD && pending=1 || exit 1; }; \
    done \
 && mkdir /out \
 && if [ -z "$pending" ]; then apk fetch -q -o /out "$KERNEL"; \
    else apk update -q && abuild -F checksum && abuild -F -r && cp ~/packages/main/*/$KERNEL-[0-9]*.apk /out/; fi

FROM alpine:$ALPINE
COPY --from=kernel /root/.abuild/*.rsa.pub /etc/apk/keys/
COPY --from=kernel /out/ /packages/
RUN echo disable_trigger=1 > /etc/update-grub.conf \
 && apk add --no-cache /packages/*.apk linux-firmware mkinitfs kmod grub grub-efi util-linux dosfstools e2fsprogs e2fsprogs-extra coreutils findutils ovmf docker-cli docker-cli-buildx
