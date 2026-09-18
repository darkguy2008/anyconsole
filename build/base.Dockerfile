FROM debian:trixie
ARG APT_PROXY
RUN echo "Acquire::http::Proxy \"$APT_PROXY\";" > /etc/apt/apt.conf.d/01proxy \
 && apt-get update \
 && apt-get install -y --no-install-recommends busybox btop ncurses-base rsync grub-common pax-utils dropbear-bin dhcpcd-base cage retroarch libgl1 libgl1-mesa-dri mesa-libgallium libglx-mesa0 libegl-mesa0 libgles2 xkb-data curl ca-certificates unzip \
 && rm -f /etc/apt/apt.conf.d/01proxy
