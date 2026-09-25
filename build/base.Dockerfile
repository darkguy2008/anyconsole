FROM debian:trixie
ARG APT_PROXY
ARG DOCKER_REPO=https://download.docker.com/linux/debian
RUN echo "Acquire::http::Proxy \"$APT_PROXY\"; Acquire::https::Proxy \"DIRECT\";" > /etc/apt/apt.conf.d/01proxy \
 && apt-get update \
 && apt-get install -y --no-install-recommends curl ca-certificates \
 && curl -fsSL "$DOCKER_REPO/gpg" -o /etc/apt/keyrings/docker.asc \
 && echo "deb [signed-by=/etc/apt/keyrings/docker.asc] $DOCKER_REPO trixie stable" > /etc/apt/sources.list.d/docker.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends busybox gcc libc6-dev libsdl2-dev libevdev-dev pkgconf libdrm-dev libpng-dev libpixman-1-dev libcairo2-dev libjson-c-dev libwayland-dev wayland-protocols librsvg2-bin fonts-inter fontconfig-config btop ncurses-base rsync grub-common fdisk e2fsprogs dosfstools pax-utils dropbear-bin dhcpcd-base libwlroots-0.18-dev libxkbcommon-dev libegl-dev libgles-dev libvulkan-dev mesa-vulkan-drivers wayland-utils grim udev kmod pipewire pipewire-bin pipewire-pulse wireplumber cifs-utils jq docker-ce docker-ce-cli containerd.io docker-buildx-plugin libgl1-mesa-dri mesa-libgallium libegl-mesa0 bluez dbus-daemon libsystemd-dev libudev-dev \
 && rm -f /etc/apt/apt.conf.d/01proxy
