ARG LIBRETRO=https://buildbot.libretro.com
ARG CORES=$LIBRETRO/nightly/linux/x86_64/latest

FROM debian:trixie-slim AS downloads
ARG LIBRETRO
ARG CORES
RUN apt-get update && apt-get install -y --no-install-recommends 7zip && rm -rf /var/lib/apt/lists/*
WORKDIR /downloads
ADD $LIBRETRO/stable/1.22.2/linux/x86_64/RetroArch.7z $CORES/fceumm_libretro.so.zip $CORES/dosbox_pure_libretro.so.zip ./
ADD "https://raw.githubusercontent.com/libretro/retroarch-joypad-autoconfig/master/linuxraw/Sony%20Interactive%20Entertainment%20DualSense%20Wireless%20Controller.cfg" /retroarch/autoconfig/linuxraw/dualsense.cfg
RUN 7z e RetroArch.7z RetroArch-Linux-x86_64/RetroArch-Linux-x86_64.AppImage >/dev/null \
 && chmod +x RetroArch-Linux-x86_64.AppImage \
 && ./RetroArch-Linux-x86_64.AppImage --appimage-extract >/dev/null \
 && mv squashfs-root/usr/bin/retroarch squashfs-root/usr/lib /retroarch/ \
 && mkdir /cores && 7z e -o/cores '*.so.zip' >/dev/null

FROM debian:trixie-slim AS retroarch
RUN apt-get update && apt-get install -y --no-install-recommends libasound2t64 libjack-jackd2-0 libfontconfig1 libpulse0 libdbus-1-3 libxi6 libxrandr2 libxinerama1 libxxf86vm1 libxss1 libxext6 libxrender1 libxcursor1 libxfixes3 libv4l-0 libudev1 libxkbcommon0 libwayland-egl1 libwayland-cursor0 libgl1 libgl1-mesa-dri mesa-libgallium libglx-mesa0 libegl1 libegl-mesa0 libgles2 libvulkan1 mesa-vulkan-drivers && rm -rf /var/lib/apt/lists/*
COPY --from=downloads /retroarch /retroarch
RUN for bundled in /retroarch/lib/*; do ! [ -e "/usr/lib/x86_64-linux-gnu/${bundled##*/}" ] || rm "$bundled"; done
COPY retroarch.cfg vulkan.cfg /retroarch/
LABEL anyconsole.vulkan="--appendconfig=/retroarch/vulkan.cfg"
ENV LD_LIBRARY_PATH=/retroarch/lib HOME=/tmp
ENTRYPOINT ["/retroarch/retroarch", "--verbose", "--config", "/retroarch/retroarch.cfg", "--libretro", "/retroarch/core.so"]

# nes Nintendo Entertainment System
FROM retroarch AS nes
LABEL anyconsole.launch="nes fds unf unif"
COPY --from=downloads /cores/fceumm_libretro.so /retroarch/core.so

# msdos MS-DOS
FROM retroarch AS msdos
LABEL anyconsole.launch="dosz zip exe com bat conf iso cue"
COPY --from=downloads /cores/dosbox_pure_libretro.so /retroarch/core.so
COPY msdos.opt /retroarch/core.opt
