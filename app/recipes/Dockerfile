ARG LIBRETRO=https://buildbot.libretro.com

FROM debian:trixie-slim AS unzip
ARG LIBRETRO
RUN apt-get update && apt-get install -y --no-install-recommends 7zip && rm -rf /var/lib/apt/lists/*
ENV CORES=$LIBRETRO/nightly/linux/x86_64/latest SYSTEM=$LIBRETRO/assets/system
WORKDIR /cores

FROM unzip AS retroarch-download
ARG LIBRETRO
ADD $LIBRETRO/stable/1.22.2/linux/x86_64/RetroArch.7z .
ADD "https://raw.githubusercontent.com/libretro/retroarch-joypad-autoconfig/master/linuxraw/Sony%20Interactive%20Entertainment%20DualSense%20Wireless%20Controller.cfg" /retroarch/autoconfig/linuxraw/dualsense.cfg
RUN 7z e RetroArch.7z RetroArch-Linux-x86_64/RetroArch-Linux-x86_64.AppImage >/dev/null \
 && chmod +x RetroArch-Linux-x86_64.AppImage \
 && ./RetroArch-Linux-x86_64.AppImage --appimage-extract >/dev/null \
 && mv squashfs-root/usr/bin/retroarch squashfs-root/usr/lib /retroarch/

FROM unzip AS fceumm
ADD $CORES/fceumm_libretro.so.zip .
RUN 7z e fceumm_libretro.so.zip >/dev/null

FROM unzip AS dosbox_pure
ADD $CORES/dosbox_pure_libretro.so.zip .
RUN 7z e dosbox_pure_libretro.so.zip >/dev/null

FROM unzip AS snes9x
ADD $CORES/snes9x_libretro.so.zip .
RUN 7z e snes9x_libretro.so.zip >/dev/null

FROM unzip AS parallel_n64
ADD $CORES/parallel_n64_libretro.so.zip .
RUN 7z e parallel_n64_libretro.so.zip >/dev/null

FROM unzip AS mgba
ADD $CORES/mgba_libretro.so.zip .
RUN 7z e mgba_libretro.so.zip >/dev/null

FROM unzip AS gambatte
ADD $CORES/gambatte_libretro.so.zip .
RUN 7z e gambatte_libretro.so.zip >/dev/null

FROM unzip AS mednafen_wswan
ADD $CORES/mednafen_wswan_libretro.so.zip .
RUN 7z e mednafen_wswan_libretro.so.zip >/dev/null

FROM unzip AS bluemsx
ADD $CORES/bluemsx_libretro.so.zip $SYSTEM/blueMSX.zip .
RUN 7z e bluemsx_libretro.so.zip >/dev/null && 7z x -o/system blueMSX.zip >/dev/null

FROM unzip AS freeintv
ADD $CORES/freeintv_libretro.so.zip .
RUN 7z e freeintv_libretro.so.zip >/dev/null

FROM unzip AS stella2014
ADD $CORES/stella2014_libretro.so.zip .
RUN 7z e stella2014_libretro.so.zip >/dev/null

FROM unzip AS swanstation
ADD $CORES/swanstation_libretro.so.zip .
RUN 7z e swanstation_libretro.so.zip >/dev/null

FROM unzip AS pcsx2
ADD $CORES/pcsx2_libretro.so.zip .
RUN 7z e pcsx2_libretro.so.zip >/dev/null

FROM unzip AS ppsspp
ADD $CORES/ppsspp_libretro.so.zip $SYSTEM/PPSSPP.zip .
RUN 7z e ppsspp_libretro.so.zip >/dev/null && 7z x -o/system PPSSPP.zip >/dev/null

FROM unzip AS azahar
ADD $CORES/azahar_libretro.so.zip .
RUN 7z e azahar_libretro.so.zip >/dev/null

FROM debian:trixie-slim AS gpu
RUN apt-get update && apt-get install -y --no-install-recommends libgl1 libgl1-mesa-dri mesa-libgallium libglx-mesa0 libegl1 libegl-mesa0 libgles2 libvulkan1 mesa-vulkan-drivers libpulse0 && rm -rf /var/lib/apt/lists/*
ENV HOME=/tmp

FROM gpu AS retroarch
RUN apt-get update && apt-get install -y --no-install-recommends libasound2t64 libjack-jackd2-0 libfontconfig1 libdbus-1-3 libxi6 libxrandr2 libxinerama1 libxxf86vm1 libxss1 libxext6 libxrender1 libxcursor1 libxfixes3 libv4l-0 libudev1 libxkbcommon0 libwayland-egl1 libwayland-cursor0 && rm -rf /var/lib/apt/lists/*
COPY --from=retroarch-download /retroarch /retroarch
RUN for bundled in /retroarch/lib/*; do ! [ -e "/usr/lib/x86_64-linux-gnu/${bundled##*/}" ] || rm "$bundled"; done
COPY retroarch.cfg vulkan.cfg /retroarch/
LABEL anyconsole.vulkan="--appendconfig=/retroarch/vulkan.cfg" anyconsole.rewind="true"
ENV LD_LIBRARY_PATH=/retroarch/lib
ENTRYPOINT ["/retroarch/retroarch", "--verbose", "--config", "/retroarch/retroarch.cfg", "--libretro", "/retroarch/core.so"]

# nes Nintendo Entertainment System
FROM retroarch AS nes
LABEL anyconsole.launch="nes fds unf unif"
COPY --from=fceumm /cores/fceumm_libretro.so /retroarch/core.so

# msdos MS-DOS
FROM retroarch AS msdos
LABEL anyconsole.launch="dosz zip exe com bat conf iso cue"
COPY --from=dosbox_pure /cores/dosbox_pure_libretro.so /retroarch/core.so
COPY msdos.opt /retroarch/core.opt

# snes Super Nintendo
FROM retroarch AS snes
LABEL anyconsole.launch="sfc smc fig swc bs"
COPY --from=snes9x /cores/snes9x_libretro.so /retroarch/core.so

# n64 Nintendo 64
FROM retroarch AS n64
LABEL anyconsole.launch="z64 n64 v64"
COPY --from=parallel_n64 /cores/parallel_n64_libretro.so /retroarch/core.so

# gba Game Boy Advance
FROM retroarch AS gba
LABEL anyconsole.launch="gba" anyconsole.bios="/system"
COPY --from=mgba /cores/mgba_libretro.so /retroarch/core.so

# gb Game Boy
FROM retroarch AS gb
LABEL anyconsole.launch="gb gbc dmg"
COPY --from=gambatte /cores/gambatte_libretro.so /retroarch/core.so

# wsc WonderSwan Color
FROM retroarch AS wsc
LABEL anyconsole.launch="wsc ws pc2"
COPY --from=mednafen_wswan /cores/mednafen_wswan_libretro.so /retroarch/core.so

# msx2 MSX2
FROM retroarch AS msx2
LABEL anyconsole.launch="m3u dsk mx2 rom cas"
COPY --from=bluemsx /cores/bluemsx_libretro.so /retroarch/core.so
COPY --from=bluemsx /system /system

# intellivision Intellivision
FROM retroarch AS intellivision
LABEL anyconsole.launch="int bin rom" anyconsole.bios="/system"
COPY --from=freeintv /cores/freeintv_libretro.so /retroarch/core.so

# atari2600 Atari 2600
FROM retroarch AS atari2600
LABEL anyconsole.launch="a26 bin"
COPY --from=stella2014 /cores/stella2014_libretro.so /retroarch/core.so

# ps1 PlayStation
FROM retroarch AS ps1
LABEL anyconsole.launch="m3u chd cue pbp" anyconsole.bios="/system"
COPY --from=swanstation /cores/swanstation_libretro.so /retroarch/core.so

# ps2 PlayStation 2
FROM retroarch AS ps2
LABEL anyconsole.launch="m3u chd iso cso" anyconsole.bios="/system/pcsx2/bios"
COPY --from=pcsx2 /cores/pcsx2_libretro.so /retroarch/core.so

# psp PlayStation Portable
FROM retroarch AS psp
LABEL anyconsole.launch="iso cso pbp"
COPY --from=ppsspp /cores/ppsspp_libretro.so /retroarch/core.so
COPY --from=ppsspp /system /system

# n3ds Nintendo 3DS
FROM retroarch AS n3ds
LABEL anyconsole.launch="3ds cci cxi 3dsx zcci" anyconsole.bios="/saves/Azahar/sysdata"
COPY --from=azahar /cores/azahar_libretro.so /retroarch/core.so

FROM gpu AS x11
RUN apt-get update && apt-get install -y --no-install-recommends xwayland xwayland-run xauth libopengl0 libasound2-plugins && rm -rf /var/lib/apt/lists/*
COPY asound.conf /etc/asound.conf
ENV SDL_JOYSTICK_HIDAPI=0 SDL_JOYSTICK_DISABLE_UDEV=1 SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
VOLUME /engine

FROM debian:trixie-slim AS fetch
RUN apt-get update && apt-get install -y --no-install-recommends curl ca-certificates jq libxml2-utils && rm -rf /var/lib/apt/lists/*
COPY appimage /usr/local/bin/

FROM fetch AS redream-release
ADD https://redream.io/download /redream.html
RUN echo "https://redream.io$(xmllint --html --xpath 'string((//a[starts-with(@href, "/download/redream.x86_64-linux-v") and contains(@href, "-g")])[1]/@href)' /redream.html 2>/dev/null)" > /url

FROM fetch AS redream-download
COPY --from=redream-release /url /url
RUN mkdir /redream && curl -fsSL "$(cat /url)" | tar xz -C /redream

# dreamcast Sega Dreamcast
FROM x11 AS dreamcast
LABEL anyconsole.launch="m3u gdi chd cdi cue" anyconsole.bios="/bios"
COPY --from=redream-download /redream/redream /opt/redream/redream
COPY redream/ /opt/redream/
ENTRYPOINT ["/opt/redream/launch"]

FROM fetch AS cemu-release
ADD https://api.github.com/repos/cemu-project/Cemu/releases/latest /release.json
RUN jq -r '.assets[] | select(.name | endswith("x86_64.AppImage")) | .browser_download_url' /release.json > /url

FROM fetch AS cemu-download
COPY --from=cemu-release /url /url
RUN appimage /cemu

# wiiu Nintendo Wii U
FROM x11 AS wiiu
RUN apt-get update && apt-get install -y --no-install-recommends python3-sdl2 && rm -rf /var/lib/apt/lists/*
LABEL anyconsole.launch="wux wud iso rpx wua" anyconsole.bios="/bios"
COPY --from=cemu-download /cemu /opt/cemu
COPY cemu/ /opt/cemu/
ENV XDG_CONFIG_HOME=/engine XDG_DATA_HOME=/engine XDG_CACHE_HOME=/engine GDK_BACKEND=x11
ENTRYPOINT ["/opt/cemu/launch"]

FROM fetch AS rpcs3-release
ADD https://api.github.com/repos/RPCS3/rpcs3-binaries-linux/releases/latest /release.json
RUN jq -r '.assets[] | select(.name | endswith("_linux64.AppImage")) | .browser_download_url' /release.json > /url

FROM fetch AS rpcs3-download
COPY --from=rpcs3-release /url /url
RUN appimage /rpcs3

# ps3 PlayStation 3
FROM x11 AS ps3
LABEL anyconsole.launch="sfo" anyconsole.bios="/bios" anyconsole.install="/opt/rpcs3/install"
COPY --from=rpcs3-download /rpcs3 /opt/rpcs3
RUN ln -s /opt/rpcs3/usr/share/rpcs3 /usr/share/rpcs3
COPY rpcs3/ /opt/rpcs3/
ENV XDG_CONFIG_HOME=/engine/config XDG_CACHE_HOME=/engine/cache QT_QPA_PLATFORM=xcb
ENTRYPOINT ["/opt/rpcs3/launch"]
