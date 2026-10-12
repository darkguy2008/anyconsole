ARG LIBRETRO=https://buildbot.libretro.com
# Windows 98 disk controller, picked with --build-arg WIN98_SCSI=<value>:
#   lsi        -device lsi (53C895A) with LSI's sym_hi driver, fetched from archive.org
#   lsi53c810  -device lsi53c810 with Windows 98's in-box SYMC8XX driver; builds SeaBIOS and QEMU
#              from source with the patches in lsi53c810/ (~14% slower disk I/O, nothing fetched from archive.org)
ARG WIN98_SCSI=lsi

FROM registry.fedoraproject.org/fedora-minimal:44 AS fedora
RUN echo install_weak_deps=False >> /etc/dnf/dnf.conf

FROM fedora AS unzip
ARG LIBRETRO
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y 7zip
ENV CORES=$LIBRETRO/nightly/linux/x86_64/latest SYSTEM=$LIBRETRO/assets/system STABLE=$LIBRETRO/stable/1.22.2/linux/x86_64
WORKDIR /cores

FROM unzip AS retroarch-download
ADD $STABLE/RetroArch.7z .
ADD "https://raw.githubusercontent.com/libretro/retroarch-joypad-autoconfig/master/sdl2/PS5%20Controller.cfg" /retroarch/autoconfig/sdl2/ps5.cfg
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

FROM unzip AS genesis_plus_gx
ADD $CORES/genesis_plus_gx_libretro.so.zip .
RUN 7z e genesis_plus_gx_libretro.so.zip >/dev/null

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

FROM unzip AS dolphin
ADD $CORES/dolphin_libretro.so.zip $SYSTEM/Dolphin.zip .
RUN 7z e dolphin_libretro.so.zip >/dev/null && 7z x -o/system Dolphin.zip >/dev/null

FROM unzip AS ppsspp
ADD $CORES/ppsspp_libretro.so.zip $SYSTEM/PPSSPP.zip .
RUN 7z e ppsspp_libretro.so.zip >/dev/null && 7z x -o/system PPSSPP.zip >/dev/null

FROM unzip AS azahar
ADD $CORES/azahar_libretro.so.zip .
RUN 7z e azahar_libretro.so.zip >/dev/null

FROM fedora AS gpu
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y mesa-dri-drivers mesa-libGL mesa-libEGL libglvnd-gles vulkan-loader mesa-vulkan-drivers pulseaudio-libs
ENV HOME=/tmp SDL_JOYSTICK_HIDAPI=0 SDL_JOYSTICK_DISABLE_UDEV=1 SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1

FROM gpu AS retroarch
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y alsa-lib pipewire-jack-audio-connection-kit-libs fontconfig dbus-libs libXi libXrandr libXinerama libXxf86vm libXScrnSaver libXext libXrender libXcursor libXfixes libv4l systemd-libs libxkbcommon libwayland-egl libwayland-cursor sdl2-compat libusb1 jq
COPY --from=retroarch-download /retroarch /retroarch
RUN for bundled in /retroarch/lib/*; do ! [ -e "/usr/lib64/${bundled##*/}" ] || rm "$bundled"; done
COPY retroarch.cfg vulkan.cfg /retroarch/
COPY retroarch-launch /retroarch/launch
RUN touch /retroarch/core.opt
LABEL anyconsole.vulkan="--appendconfig=/retroarch/vulkan.cfg" anyconsole.rewind="true"
ENV LD_LIBRARY_PATH=/retroarch/lib
ENTRYPOINT ["/retroarch/launch"]

# nes Nintendo Entertainment System
FROM retroarch AS nes
LABEL anyconsole.launch="nes fds unf unif" anyconsole.emulate="{\"pad\":\"digital\"}"
COPY --from=fceumm /cores/fceumm_libretro.so /retroarch/core.so

# msdos MS-DOS
FROM retroarch AS msdos
LABEL anyconsole.launch="dosz zip exe com bat conf iso cue"
COPY --from=dosbox_pure /cores/dosbox_pure_libretro.so /retroarch/core.so
COPY msdos.opt /retroarch/core.opt

# snes Super Nintendo
FROM retroarch AS snes
LABEL anyconsole.launch="sfc smc fig swc bs" anyconsole.emulate="{\"pad\":\"digital\"}"
COPY --from=snes9x /cores/snes9x_libretro.so /retroarch/core.so

# n64 Nintendo 64
FROM retroarch AS n64
LABEL anyconsole.launch="z64 n64 v64"
COPY --from=parallel_n64 /cores/parallel_n64_libretro.so /retroarch/core.so

# genesis Sega Genesis
FROM retroarch AS genesis
LABEL anyconsole.launch="md gen smd bin mdx" anyconsole.emulate="{\"pad\":\"digital\"}"
COPY --from=genesis_plus_gx /cores/genesis_plus_gx_libretro.so /retroarch/core.so

# gba Game Boy Advance
FROM retroarch AS gba
LABEL anyconsole.launch="gba" anyconsole.emulate="{\"pad\":\"digital\"}" anyconsole.bios="/system"
COPY --from=mgba /cores/mgba_libretro.so /retroarch/core.so

# gb Game Boy
FROM retroarch AS gb
LABEL anyconsole.launch="gb gbc dmg" anyconsole.emulate="{\"pad\":\"digital\"}"
COPY --from=gambatte /cores/gambatte_libretro.so /retroarch/core.so

# wsc WonderSwan Color
FROM retroarch AS wsc
LABEL anyconsole.launch="wsc ws pc2" anyconsole.emulate="{\"pad\":\"digital\"}"
COPY --from=mednafen_wswan /cores/mednafen_wswan_libretro.so /retroarch/core.so

# msx2 MSX2
FROM retroarch AS msx2
LABEL anyconsole.launch="m3u dsk mx2 rom cas" anyconsole.emulate="{\"pad\":\"digital\"}"
COPY --from=bluemsx /cores/bluemsx_libretro.so /retroarch/core.so
COPY --from=bluemsx /system /system

# intellivision Intellivision
FROM retroarch AS intellivision
LABEL anyconsole.launch="int bin rom" anyconsole.emulate="{\"pad\":\"digital\"}" anyconsole.bios="/system"
COPY --from=freeintv /cores/freeintv_libretro.so /retroarch/core.so

# atari2600 Atari 2600
FROM retroarch AS atari2600
LABEL anyconsole.launch="a26 bin" anyconsole.emulate="{\"pad\":\"digital\"}"
COPY --from=stella2014 /cores/stella2014_libretro.so /retroarch/core.so

# ps1 PlayStation
FROM retroarch AS ps1
LABEL anyconsole.launch="m3u chd cue pbp" anyconsole.bios="/system"
COPY --from=swanstation /cores/swanstation_libretro.so /retroarch/core.so

# gamecube Nintendo GameCube
FROM retroarch AS gamecube
LABEL anyconsole.launch="m3u rvz gcm iso ciso gcz wia tgc dol elf"
COPY --from=dolphin /cores/dolphin_libretro.so /retroarch/core.so
COPY --from=dolphin /system /system

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
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y xorg-x11-server-Xwayland xwayland-run xorg-x11-xauth libglvnd-opengl alsa-plugins-pulseaudio python3-pysdl2 libgpg-error
COPY asound.conf /etc/asound.conf
COPY joystick-guid /usr/local/bin/
VOLUME /engine

FROM x11 AS x11-rewind
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y inotify-tools
COPY rewind /usr/local/bin/

FROM fedora AS fetch
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y tar gzip libxml2 7zip
COPY appimage github-release /usr/local/bin/

FROM fetch AS redream-release
ADD https://redream.io/download /redream.html
RUN echo "https://redream.io$(xmllint --html --xpath 'string((//a[starts-with(@href, "/download/redream.x86_64-linux-v") and contains(@href, "-g")])[1]/@href)' /redream.html 2>/dev/null)" > /url

FROM fetch AS redream-download
COPY --from=redream-release /url /url
RUN mkdir /redream && curl -fsSL "$(cat /url)" | tar xz -C /redream

FROM fedora AS redream-commands
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y gcc python3-capstone python3-pyelftools
COPY --from=redream-download /redream/redream /redream
COPY redream/addresses redream/commands.c /
RUN /addresses /redream > /addresses.flags \
 && gcc -O2 -Wall -Wextra -Werror -shared -fPIC $(cat /addresses.flags) -o /commands.so /commands.c

# dreamcast Sega Dreamcast
FROM x11-rewind AS dreamcast
LABEL anyconsole.launch="m3u gdi chd cdi cue" anyconsole.bios="/bios" anyconsole.rewind="true"
COPY --from=redream-download /redream/redream /opt/redream/redream
COPY --from=redream-commands /commands.so /opt/redream/
COPY redream/ /opt/redream/
ENV REDREAM_COMMANDS=/tmp/redream-commands
ENTRYPOINT ["/opt/redream/launch"]

FROM fetch AS cemu-release
ADD https://github.com/cemu-project/Cemu/releases.atom /releases.atom
RUN github-release cemu-project/Cemu x86_64.AppImage > /url

FROM fetch AS cemu-download
COPY --from=cemu-release /url /url
RUN appimage /cemu

# wiiu Nintendo Wii U
FROM x11 AS wiiu
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y xdotool
LABEL anyconsole.launch="wux wud iso rpx wua" anyconsole.bios="/bios" anyconsole.screen="/opt/cemu/screen"
COPY --from=cemu-download /cemu /opt/cemu
COPY cemu/ /opt/cemu/
ENV XDG_CONFIG_HOME=/engine XDG_DATA_HOME=/engine XDG_CACHE_HOME=/engine GDK_BACKEND=x11
ENTRYPOINT ["/opt/cemu/launch"]

FROM fetch AS rpcs3-release
ADD https://github.com/RPCS3/rpcs3-binaries-linux/releases.atom /releases.atom
RUN github-release RPCS3/rpcs3-binaries-linux _linux64.AppImage > /url

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

FROM fetch AS pcsx2-release
ADD https://github.com/PCSX2/pcsx2/releases.atom /releases.atom
RUN github-release PCSX2/pcsx2 -linux-appimage-x64-Qt.AppImage > /url

FROM fetch AS pcsx2-download
COPY --from=pcsx2-release /url /url
RUN appimage /pcsx2

# ps2 PlayStation 2
FROM x11-rewind AS ps2
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y fontconfig freetype harfbuzz libcom_err libSM libICE matchbox-window-manager
LABEL anyconsole.launch="chd iso cso" anyconsole.bios="/bios" anyconsole.vulkan="--vulkan" anyconsole.rewind="true"
COPY --from=pcsx2-download /pcsx2 /opt/pcsx2
COPY pcsx2/ /opt/pcsx2/
ENV XDG_CONFIG_HOME=/engine QT_QPA_PLATFORM=xcb
ENTRYPOINT ["/opt/pcsx2/launch"]

FROM fetch AS softgpu-release
ADD https://github.com/JHRobotics/softgpu/releases.atom /releases.atom
RUN github-release JHRobotics/softgpu .zip > /url

FROM fetch AS patcher9x-release
ADD https://github.com/JHRobotics/patcher9x/releases.atom /releases.atom
RUN github-release JHRobotics/patcher9x -linux-amd64.tar.gz > /url

FROM fetch AS win98-download
COPY --from=softgpu-release /url /softgpu.url
COPY --from=patcher9x-release /url /patcher9x.url
ADD https://files.emulace.cz/ac97_362.zip /ac97.zip
RUN curl -fsSL -o /softgpu.zip "$(cat /softgpu.url)" \
 && curl -fsSL "$(cat /patcher9x.url)" | tar xz -C /usr/local/bin \
 && 7z e -o/base/drivers /softgpu.zip 'driver/sse3-w98/*' -x!'driver/sse3-w98/mesa25' >/dev/null \
 && 7z e -o/base/build/DX9 /softgpu.zip 'redist/dx9_2004/*' >/dev/null \
 && 7z e -o/base/drivers /ac97.zip 'WDM/*' >/dev/null

FROM fetch AS win98-scsi-lsi
ADD --checksum=sha256:76e3ed7029a6f5ec8c8bdc160a072b77135d4c7a1128589f4f94508c477e10eb https://archive.org/download/SYMC8XX-bios-driver/win896.zip /lsi.zip
RUN 7z e -o/out/opt/win98/base/drivers /lsi.zip 'sym_hi.*' >/dev/null \
 && echo 'SCSI="-device lsi"' > /out/opt/win98/scsi

FROM fedora AS win98-scsi-lsi53c810
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y gcc make patch diffutils tar gzip python3 python-unversioned-command ninja-build pkgconf flex bison bzip2 xz glib2-devel glib2-static pcre2-static pixman-devel zlib-ng-compat-devel sdl2-compat-devel libepoxy-devel mesa-libgbm-devel pulseaudio-libs-devel libfdt-devel
ARG SEABIOS_VERSION=1.17.0
ADD --checksum=sha256:5159a35fac5c1579dd5d702001033c826ce45d30d3bd657ec1b2695455997712 https://www.seabios.org/downloads/seabios-$SEABIOS_VERSION.tar.gz /seabios.tar.gz
ARG QEMU_VERSION=10.2.2
ADD --checksum=sha256:784b296ff29c1417aa72323abcb2d2ea9ab9771724f577dcd785c3b04f21e176 https://download.qemu.org/qemu-$QEMU_VERSION.tar.xz /qemu.tar.xz
COPY lsi53c810/qemu-lsi-io-reentrancy.patch /
RUN tar xJf /qemu.tar.xz && cd /qemu-$QEMU_VERSION \
 && patch -p1 < /qemu-lsi-io-reentrancy.patch \
 && ./configure --target-list=i386-softmmu --firmwarepath=/usr/share/qemu:/usr/share/seabios --without-default-features --enable-werror --disable-debug-info --enable-kvm --enable-sdl --enable-opengl --enable-pa \
 && make -C build qemu-system-i386 \
 && install -D build/qemu-system-i386 /out/usr/local/bin/qemu-system-i386
COPY lsi53c810/seabios-*.patch /seabios-patches/
RUN tar xzf /seabios.tar.gz && cd /seabios-$SEABIOS_VERSION \
 && for fix in /seabios-patches/*.patch; do patch -p1 < "$fix" || exit 1; done \
 && printf 'CONFIG_QEMU=y\nCONFIG_ROM_SIZE=256\n' > .config && make olddefconfig && make \
 && install -D out/bios.bin /out/opt/win98/bios.bin \
 && echo 'SCSI="-device lsi53c810 -bios /opt/win98/bios.bin"' > /out/opt/win98/scsi

FROM win98-scsi-$WIN98_SCSI AS win98-scsi

FROM fedora AS win98-programs
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y mingw32-gcc
COPY win98/launcher.c win98/autologon.c /
ARG WIN32_FLAGS="-Os -s -Wall -Wextra -Werror -nostdlib -mwindows -Wl,-e,_start -Wl,--subsystem,windows:4.0 -Wl,--no-insert-timestamp"
RUN mkdir /anycon \
 && i686-w64-mingw32-gcc $WIN32_FLAGS -o /anycon/LAUNCHER.EXE /launcher.c -lkernel32 -luser32 \
 && i686-w64-mingw32-gcc $WIN32_FLAGS -o /anycon/AUTOLOGON.EXE /autologon.c -lkernel32

# win98 Windows 98
FROM gpu AS win98
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y libatomic qemu-system-x86-core qemu-ui-sdl qemu-ui-opengl qemu-audio-pa qemu-img mtools dosfstools util-linux syslinux 7zip jq python3-evdev
LABEL anyconsole.launch="img iso" anyconsole.bios="/bios" anyconsole.install="/opt/win98/install" anyconsole.devices="/dev/kvm" anyconsole.touchpad="raw" anyconsole.shared="/shared"
COPY --from=win98-download /usr/local/bin/patcher9x /usr/local/bin/
COPY --from=win98-download /base /opt/win98/base
COPY --from=win98-programs /anycon /opt/win98/base/anycon
COPY --from=win98-scsi /out/ /
COPY win98/ /opt/win98/
VOLUME /engine
ENTRYPOINT ["/opt/win98/launch"]
