FROM fedora:44
ARG DOCKER_REPO=https://download.docker.com/linux/fedora
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked \
    curl -fsSL -o /etc/yum.repos.d/docker-ce.repo "$DOCKER_REPO/docker-ce.repo" \
 && dnf install -y --setopt=keepcache=True --setopt=install_weak_deps=False busybox gcc sdl2-compat-devel 'pkgconfig(libevdev)' 'pkgconfig(libdrm)' 'pkgconfig(libpng)' 'pkgconfig(pixman-1)' 'pkgconfig(cairo)' 'pkgconfig(json-c)' 'pkgconfig(wayland-client)' 'pkgconfig(wayland-server)' 'pkgconfig(wayland-protocols)' librsvg2-tools rsms-inter-fonts fontconfig btop ncurses-base rsync grub2-tools-minimal util-linux e2fsprogs dosfstools python3-pyelftools python-unversioned-command dropbear dhcpcd 'pkgconfig(wlroots-0.19)' 'pkgconfig(gbm)' 'pkgconfig(xkbcommon)' 'pkgconfig(egl)' 'pkgconfig(glesv2)' 'pkgconfig(wayland-egl)' 'pkgconfig(freetype2)' 'pkgconfig(harfbuzz)' 'pkgconfig(opusfile)' 'pkgconfig(libpipewire-0.3)' cmake gcc-c++ 'pkgconfig(vulkan)' mesa-vulkan-drivers mesa-dri-drivers mesa-libEGL wayland-utils grim socat systemd-udev kmod pipewire pipewire-utils pipewire-pulseaudio wireplumber cifs-utils jq docker-ce docker-ce-cli containerd.io docker-buildx-plugin dbus-daemon seatd libxml2 glibc-gconv-extra 'pkgconfig(libsystemd)' 'pkgconfig(libudev)' 'pkgconfig(opus)' 'pkgconfig(zlib)' 'pkgconfig(glib-2.0)' 'pkgconfig(dbus-1)' make patch xz \
 && systemd-hwdb update --usr \
 && ld.so --list-diagnostics | sed -n 's/^path\.system_dirs\[0x[0-9a-f]*\]="\(.*\)"$/\1/p' > /etc/ld.so.conf.d/system-dirs.conf
ARG PAX_UTILS_VERSION=1.3.10
ADD --chmod=755 --checksum=sha256:e6aa8eb2f8aa97cbe66491d82e0c9f99c7cc706c6eb0a64b4a7e06f70e71f7bd https://raw.githubusercontent.com/gentoo/pax-utils/v$PAX_UTILS_VERSION/lddtree.py /usr/local/bin/lddtree
ARG BLUEZ_VERSION=5.87
ADD --checksum=sha256:26bdcf2cebd7310c6f598850606b037ef0c515fe6608ebc54d22c50c4c32b35f https://www.kernel.org/pub/linux/bluetooth/bluez-$BLUEZ_VERSION.tar.xz /bluez.tar.xz
COPY *.patch /patches/
RUN tar xJf /bluez.tar.xz \
 && cd /bluez-$BLUEZ_VERSION \
 && for fix in /patches/*.patch; do patch -p1 < "$fix" || exit 1; done \
 && ./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var --libexecdir=/usr/libexec --with-udevdir=/usr/lib/udev --enable-sixaxis \
    --disable-manpages --disable-obex --disable-cups --disable-client --disable-tools --disable-monitor --disable-systemd \
 && make -j"$(nproc)" \
 && make install-strip \
 && cd / && rm -rf /bluez.tar.xz /bluez-$BLUEZ_VERSION /patches
ARG YOGA_VERSION=3.2.1
ADD --checksum=sha256:86b399ac31fd820d8ffa823c3fae31bb690b6fc45301b2a8a966c09b5a088b55 https://github.com/facebook/yoga/archive/refs/tags/v$YOGA_VERSION.tar.gz /yoga.tar.gz
RUN tar xzf /yoga.tar.gz \
 && cmake -S /yoga-$YOGA_VERSION/yoga -B /yoga-build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /yoga-build -j"$(nproc)" \
 && install -D -t /usr/local/lib /yoga-build/libyogacore.a \
 && install -D -m 644 -t /usr/local/include/yoga /yoga-$YOGA_VERSION/yoga/*.h \
 && rm -rf /yoga.tar.gz /yoga-$YOGA_VERSION /yoga-build
ARG QUICKJS_VERSION=2026-06-04
ADD --checksum=sha256:b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a https://bellard.org/quickjs/quickjs-$QUICKJS_VERSION.tar.xz /quickjs.tar.xz
RUN tar xJf /quickjs.tar.xz \
 && make -C /quickjs-$QUICKJS_VERSION -j"$(nproc)" libquickjs.a \
 && install -D -m 644 -t /usr/local/lib /quickjs-$QUICKJS_VERSION/libquickjs.a \
 && install -D -m 644 -t /usr/local/include/quickjs /quickjs-$QUICKJS_VERSION/quickjs.h /quickjs-$QUICKJS_VERSION/quickjs-libc.h \
 && rm -rf /quickjs.tar.xz /quickjs-$QUICKJS_VERSION
