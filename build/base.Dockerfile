FROM debian:trixie
ARG APT_PROXY
RUN echo "Acquire::http::Proxy \"$APT_PROXY\";" > /etc/apt/apt.conf.d/01proxy \
 && apt-get update \
 && apt-get install -y --no-install-recommends busybox gcc libc6-dev btop ncurses-base rsync grub-common fdisk e2fsprogs dosfstools pax-utils dropbear-bin dhcpcd-base cage 7zip libasound2t64 libjack-jackd2-0 libfontconfig1 libpulse0 libdbus-1-3 libxi6 libxrandr2 libxinerama1 libxxf86vm1 libxss1 libxext6 libxrender1 libxcursor1 libxfixes3 libv4l-0 libudev1 libxkbcommon0 libwayland-egl1 libwayland-cursor0 libgl1 libgl1-mesa-dri mesa-libgallium libglx-mesa0 libegl-mesa0 libgles2 xkb-data curl ca-certificates \
 && rm -f /etc/apt/apt.conf.d/01proxy
