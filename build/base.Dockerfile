FROM debian:trixie
ARG APT_PROXY
ARG DOCKER_REPO=https://download.docker.com/linux/debian
RUN echo "Acquire::http::Proxy \"$APT_PROXY\"; Acquire::https::Proxy \"DIRECT\";" > /etc/apt/apt.conf.d/01proxy \
 && apt-get update \
 && apt-get install -y --no-install-recommends curl ca-certificates \
 && curl -fsSL "$DOCKER_REPO/gpg" -o /etc/apt/keyrings/docker.asc \
 && echo "deb [signed-by=/etc/apt/keyrings/docker.asc] $DOCKER_REPO trixie stable" > /etc/apt/sources.list.d/docker.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends busybox gcc libc6-dev libsdl2-dev libevdev-dev pkgconf libdrm-dev libpng-dev libpixman-1-dev libcairo2-dev libjson-c-dev libwayland-dev wayland-protocols librsvg2-bin fonts-inter fontconfig-config btop ncurses-base rsync grub-common fdisk e2fsprogs dosfstools pax-utils dropbear-bin dhcpcd-base meson ninja-build hwdata libinput-dev libseat-dev libdisplay-info-dev libliftoff-dev libgbm-dev libxkbcommon-dev libegl-dev libgles-dev libvulkan-dev mesa-vulkan-drivers wayland-utils grim udev kmod pipewire pipewire-bin pipewire-pulse libspa-0.2-bluetooth wireplumber cifs-utils jq docker-ce docker-ce-cli containerd.io docker-buildx-plugin libgl1-mesa-dri mesa-libgallium libegl-mesa0 dbus-daemon libsystemd-dev libudev-dev libopus-dev zlib1g-dev libglib2.0-dev libdbus-1-dev make patch xz-utils \
 && rm -f /etc/apt/apt.conf.d/01proxy
ARG WLROOTS_VERSION=0.19.3
ADD --checksum=sha256:5d02693175e5afd9af5f10e3e4976d6e9249dc39a90eb17d23fa5f54b125ccc5 https://gitlab.freedesktop.org/wlroots/wlroots/-/releases/$WLROOTS_VERSION/downloads/wlroots-$WLROOTS_VERSION.tar.gz /wlroots.tar.gz
RUN tar xzf /wlroots.tar.gz \
 && meson setup /wlroots-build /wlroots-$WLROOTS_VERSION --prefix=/usr --libdir=lib/x86_64-linux-gnu --buildtype=release \
    -Dxwayland=disabled -Dbackends=drm,libinput -Drenderers=gles2 -Dexamples=false \
 && meson install -C /wlroots-build --strip \
 && rm -rf /wlroots.tar.gz /wlroots-$WLROOTS_VERSION /wlroots-build
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
