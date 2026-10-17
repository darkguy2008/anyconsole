ARG LIBRETRO=https://buildbot.libretro.com
ARG RETROARCH=1.22.2 RETROARCH_BUILD=$LIBRETRO/nightly/linux/x86_64/2026-10-14_RetroArch.7z
# Windows 98 disk controller, picked with --build-arg WIN98_SCSI=<value>:
#   lsi        -device lsi (53C895A) with LSI's sym_hi driver, fetched from archive.org
#   lsi53c810  -device lsi53c810 with Windows 98's in-box SYMC8XX driver; builds SeaBIOS and QEMU
#              from source with the patches in lsi53c810/ (~14% slower disk I/O, nothing fetched from archive.org)
ARG WIN98_SCSI=lsi

FROM registry.fedoraproject.org/fedora-minimal:44@sha256:f362951032563f154aa976533e3a77c0a51314777de34971f41d99e6956bbc85 AS fedora
RUN echo install_weak_deps=False >> /etc/dnf/dnf.conf

FROM fedora AS unzip
ARG LIBRETRO RETROARCH
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y 7zip
ENV SYSTEM=$LIBRETRO/assets/system STABLE=$LIBRETRO/stable/$RETROARCH/linux/x86_64
WORKDIR /cores

FROM unzip AS retroarch-download
ARG RETROARCH_BUILD
ADD --checksum=sha256:9322a725a4e182c4fae32249051aec0df918abec4315f017369bea952e022a61 $RETROARCH_BUILD RetroArch.7z
ADD --checksum=sha256:e1013acb1e92a9f63fdb368e0d326a269b0ca4eb22b7e0e8263cbb4eae21f655 "https://raw.githubusercontent.com/libretro/retroarch-joypad-autoconfig/f3b62b847ed21939b668b897b18bbc1defbaae05/sdl2/PS5%20Controller.cfg" /retroarch/autoconfig/sdl2/ps5.cfg
RUN 7z e RetroArch.7z RetroArch-Linux-x86_64/RetroArch-Linux-x86_64.AppImage >/dev/null \
 && chmod +x RetroArch-Linux-x86_64.AppImage \
 && ./RetroArch-Linux-x86_64.AppImage --appimage-extract >/dev/null \
 && mv squashfs-root/usr/bin/retroarch squashfs-root/usr/lib /retroarch/

FROM unzip AS cores
ADD --checksum=sha256:4b7ed8dc97d4bf035fce182c64b5658c7662e2e9e5d42129538afbd4b6096307 $STABLE/RetroArch_cores.7z .
RUN 7z e RetroArch_cores.7z 'RetroArch-Linux-x86_64/RetroArch-Linux-x86_64.AppImage.home/.config/retroarch/cores/*' >/dev/null

FROM unzip AS bluemsx
ADD --checksum=sha256:6db124b8cd102dd68d36c45659e3390fd620732fe2e0179249be404e8f84042f $SYSTEM/blueMSX.zip .
RUN 7z x -o/system blueMSX.zip >/dev/null

FROM unzip AS dolphin
ADD --checksum=sha256:782f390a0818fd9d70e13ad82ee11d9ea16e3979e1de2c4d2b9dfffc155d8c17 $SYSTEM/Dolphin.zip .
RUN 7z x -o/system Dolphin.zip >/dev/null

FROM unzip AS ppsspp
ADD --checksum=sha256:e93a625c61062ce0032dce034129c14230f5982b492b9f146099340c3b81f4f8 $SYSTEM/PPSSPP.zip .
RUN 7z x -o/system PPSSPP.zip >/dev/null

FROM unzip AS azahar
ADD --checksum=sha256:2223444b74de2c922bae1efcfed60002f696b3c2aaf4fc73fc03e66a395dc26e https://github.com/azahar-emu/azahar/releases/download/2126.2/azahar-libretro-linux-x86_64-2126.2.zip .
RUN 7z e azahar-libretro-linux-x86_64-2126.2.zip >/dev/null

FROM fedora AS retroarch-frame-size
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y gcc
ADD --checksum=sha256:bd3398d29c3763d18617087020ff56b5450a48a63123403a4b674f8e79947acb https://raw.githubusercontent.com/libretro/RetroArch/69a4f0ea1e8aaf442ae4858f2e7f2b31a1776576/libretro-common/include/libretro.h /
COPY retroarch-frame-size.c /
RUN gcc -O2 -Wall -Wextra -Werror -shared -fPIC -o /frame-size.so /retroarch-frame-size.c

FROM fedora AS gpu
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y mesa-dri-drivers mesa-libGL mesa-libEGL libglvnd-gles vulkan-loader mesa-vulkan-drivers pulseaudio-libs
ENV HOME=/tmp SDL_JOYSTICK_HIDAPI=0 SDL_JOYSTICK_DISABLE_UDEV=1 SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1

FROM gpu AS retroarch
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y alsa-lib pipewire-jack-audio-connection-kit-libs fontconfig dbus-libs libXi libXrandr libXinerama libXxf86vm libXScrnSaver libXext libXrender libXcursor libXfixes libv4l systemd-libs libxkbcommon libwayland-egl libwayland-cursor sdl2-compat libusb1 jq
COPY --from=retroarch-download /retroarch /retroarch
RUN for bundled in /retroarch/lib/*; do ! [ -e "/usr/lib64/${bundled##*/}" ] || rm "$bundled"; done
COPY retroarch.cfg vulkan.cfg refresh.jq /retroarch/
COPY option.jq system.jq resolution.jq /usr/lib/jq/
COPY retroarch-launch /retroarch/launch
COPY --from=retroarch-frame-size /frame-size.so /retroarch/
RUN touch /retroarch/core.opt /retroarch/vulkan.opt
LABEL anyconsole.vulkan="--appendconfig=/retroarch/vulkan.cfg" anyconsole.rewind="true"
ENV LD_LIBRARY_PATH=/retroarch/lib
ENTRYPOINT ["/retroarch/launch"]

# nes Nintendo Entertainment System
FROM retroarch AS nes
LABEL anyconsole.launch="nes fds unf unif" anyconsole.analog="{\"sticks\":false}" anyconsole.thumbnails="https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%20Entertainment%20System/Named_Boxarts/{name}.png"
COPY --from=cores /cores/fceumm_libretro.so /retroarch/core.so
COPY nes-system.jq /usr/lib/jq/system.jq

# msdos MS-DOS
FROM retroarch AS msdos
LABEL anyconsole.launch="dosz zip exe com bat conf iso cue" anyconsole.thumbnails="https://thumbnails.libretro.com/DOS/Named_Boxarts/{name}.png"
COPY --from=cores /cores/dosbox_pure_libretro.so /retroarch/core.so
COPY msdos.opt /retroarch/core.opt
COPY msdos-refresh.jq /retroarch/refresh.jq

# snes Super Nintendo
FROM retroarch AS snes
LABEL anyconsole.launch="sfc smc fig swc bs" anyconsole.analog="{\"sticks\":false}" anyconsole.thumbnails="https://thumbnails.libretro.com/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/Named_Boxarts/{name}.png"
COPY --from=cores /cores/snes9x_libretro.so /retroarch/core.so
COPY snes-system.jq /usr/lib/jq/system.jq

# n64 Nintendo 64
FROM retroarch AS n64
LABEL anyconsole.launch="z64 n64 v64" anyconsole.analog="{\"sticks\":true}" anyconsole.thumbnails="https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%2064/Named_Boxarts/{name}.png" anyconsole.resolution="{\"height\":480,\"scales\":[1,2,4,8]}"
COPY --from=cores /cores/parallel_n64_libretro.so /retroarch/core.so
COPY n64.opt /retroarch/core.opt
COPY n64-vulkan.opt /retroarch/vulkan.opt
COPY n64-resolution.jq /usr/lib/jq/resolution.jq

# genesis Sega Genesis
FROM retroarch AS genesis
LABEL anyconsole.launch="md gen smd bin mdx" anyconsole.analog="{\"sticks\":false}" anyconsole.thumbnails="https://thumbnails.libretro.com/Sega%20-%20Mega%20Drive%20-%20Genesis/Named_Boxarts/{name}.png"
COPY --from=cores /cores/genesis_plus_gx_libretro.so /retroarch/core.so
COPY genesis-system.jq /usr/lib/jq/system.jq

# gba Game Boy Advance
FROM retroarch AS gba
LABEL anyconsole.launch="gba" anyconsole.analog="{\"sticks\":false}" anyconsole.bios="/system" anyconsole.thumbnails="https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy%20Advance/Named_Boxarts/{name}.png"
COPY --from=cores /cores/mgba_libretro.so /retroarch/core.so

# gb Game Boy
FROM retroarch AS gb
LABEL anyconsole.launch="gb gbc dmg" anyconsole.analog="{\"sticks\":false}" anyconsole.thumbnails="https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy/Named_Boxarts/{name}.png"
COPY --from=cores /cores/gambatte_libretro.so /retroarch/core.so

# wsc WonderSwan Color
FROM retroarch AS wsc
LABEL anyconsole.launch="wsc ws pc2" anyconsole.analog="{\"sticks\":false}" anyconsole.thumbnails="https://thumbnails.libretro.com/Bandai%20-%20WonderSwan%20Color/Named_Boxarts/{name}.png"
COPY --from=cores /cores/mednafen_wswan_libretro.so /retroarch/core.so
COPY wsc-system.jq /usr/lib/jq/system.jq

# msx2 MSX2
FROM retroarch AS msx2
LABEL anyconsole.launch="m3u dsk mx2 rom cas" anyconsole.analog="{\"sticks\":false}" anyconsole.thumbnails="https://thumbnails.libretro.com/Microsoft%20-%20MSX2/Named_Boxarts/{name}.png"
COPY --from=cores /cores/bluemsx_libretro.so /retroarch/core.so
COPY --from=bluemsx /system /system

# intellivision Intellivision
FROM retroarch AS intellivision
LABEL anyconsole.launch="int bin rom" anyconsole.analog="{\"sticks\":true}" anyconsole.bios="/system" anyconsole.thumbnails="https://thumbnails.libretro.com/Mattel%20-%20Intellivision/Named_Boxarts/{name}.png"
COPY --from=cores /cores/freeintv_libretro.so /retroarch/core.so

# atari2600 Atari 2600
FROM retroarch AS atari2600
LABEL anyconsole.launch="a26 bin" anyconsole.analog="{\"sticks\":false,\"dial\":\"stella_paddle_analog_absolute\"}" anyconsole.thumbnails="https://thumbnails.libretro.com/Atari%20-%202600/Named_Boxarts/{name}.png"
COPY --from=cores /cores/stella_libretro.so /retroarch/core.so

# ps1 PlayStation
FROM retroarch AS ps1
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y gawk
LABEL anyconsole.launch="m3u chd cue pbp" anyconsole.analog="{\"sticks\":true,\"toggle\":{\"forced\":[\"swanstation_Controller1_ForceAnalog\",\"swanstation_Controller2_ForceAnalog\",\"swanstation_Controller3_ForceAnalog\",\"swanstation_Controller4_ForceAnalog\"]}}" anyconsole.bios="/system" anyconsole.thumbnails="https://thumbnails.libretro.com/Sony%20-%20PlayStation/Named_Boxarts/{name}.png" anyconsole.resolution="{\"height\":240,\"scales\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]}"
COPY --from=cores /cores/swanstation_libretro.so /retroarch/core.so
COPY ps1.opt /retroarch/core.opt
COPY ps1-system.jq /usr/lib/jq/system.jq
COPY ps1-resolution.jq /usr/lib/jq/resolution.jq
COPY analog-state.awk ps1-analog.awk analog-log /retroarch/
ARG RETRO_DEVICE_ANALOG=5 RETRO_DEVICE_TYPE_SHIFT=8 SWANSTATION_DUALSHOCK_SUBCLASS=0 SWANSTATION_LIBRARY_NAME=SwanStation
RUN mkdir -p "/retroarch/remaps/$SWANSTATION_LIBRARY_NAME" \
 && for port in 1 2 3 4; do echo "input_libretro_device_p$port = \"$(((SWANSTATION_DUALSHOCK_SUBCLASS + 1) << RETRO_DEVICE_TYPE_SHIFT | RETRO_DEVICE_ANALOG))\""; done > "/retroarch/remaps/$SWANSTATION_LIBRARY_NAME/$SWANSTATION_LIBRARY_NAME.rmp"
ENTRYPOINT ["/retroarch/analog-log", "/retroarch/ps1-analog.awk", "/retroarch/launch"]

# gamecube Nintendo GameCube
FROM retroarch AS gamecube
LABEL anyconsole.launch="m3u rvz gcm iso ciso gcz wia tgc dol elf" anyconsole.analog="{\"sticks\":true}" anyconsole.thumbnails="https://thumbnails.libretro.com/Nintendo%20-%20GameCube/Named_Boxarts/{name}.png" anyconsole.resolution="{\"height\":528,\"scales\":[1,2,3,4,5,6]}"
COPY --from=cores /cores/dolphin_libretro.so /retroarch/core.so
COPY --from=dolphin /system /system
COPY gamecube-system.jq /usr/lib/jq/system.jq
COPY gamecube-resolution.jq /usr/lib/jq/resolution.jq

# psp PlayStation Portable
FROM retroarch AS psp
LABEL anyconsole.launch="iso cso pbp" anyconsole.analog="{\"sticks\":true}" anyconsole.thumbnails="https://thumbnails.libretro.com/Sony%20-%20PlayStation%20Portable/Named_Boxarts/{name}.png" anyconsole.resolution="{\"height\":272,\"scales\":[1,2,3,4,5,6,7,8,9,10]}"
COPY --from=cores /cores/ppsspp_libretro.so /retroarch/core.so
COPY --from=ppsspp /system /system
COPY psp-system.jq /usr/lib/jq/system.jq
COPY psp-resolution.jq /usr/lib/jq/resolution.jq

# n3ds Nintendo 3DS
FROM retroarch AS n3ds
LABEL anyconsole.launch="3ds cci cxi 3dsx zcci" anyconsole.analog="{\"sticks\":true}" anyconsole.bios="/saves/Azahar/sysdata" anyconsole.thumbnails="https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%203DS/Named_Boxarts/{name}.png" anyconsole.resolution="{\"height\":240,\"scales\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18]}"
COPY --from=azahar /cores/azahar_libretro.so /retroarch/core.so
COPY n3ds-system.jq /usr/lib/jq/system.jq
COPY n3ds-resolution.jq /usr/lib/jq/resolution.jq

FROM gpu AS x11
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y xorg-x11-server-Xwayland xwayland-run xorg-x11-xauth libglvnd-opengl alsa-plugins-pulseaudio python3-pysdl2 libgpg-error jq
COPY asound.conf /etc/asound.conf
COPY option.jq /usr/lib/jq/
COPY joystick-guid system-settings /usr/local/bin/
VOLUME /engine

FROM x11 AS x11-rewind
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y inotify-tools
COPY rewind /usr/local/bin/

FROM fedora AS fetch
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y tar gzip 7zip
COPY appimage /usr/local/bin/

FROM fetch AS redream-download
ADD --checksum=sha256:685370fd84766d3a59d74ab4bd1b1ce0dd3b3094a9548dcb28f22b1e0e5210cf https://redream.io/download/redream.x86_64-linux-v1.5.0-1240-gc41f7f2.tar.gz /redream.tar.gz
RUN mkdir /redream && tar xzf /redream.tar.gz -C /redream

FROM fedora AS redream-commands
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y gcc python3-capstone python3-pyelftools
COPY --from=redream-download /redream/redream /redream
COPY redream/addresses redream/commands.c /
RUN /addresses /redream > /addresses.flags \
 && gcc -O2 -Wall -Wextra -Werror -shared -fPIC $(cat /addresses.flags) -o /commands.so /commands.c

# dreamcast Sega Dreamcast
FROM x11-rewind AS dreamcast
LABEL anyconsole.launch="m3u gdi chd cdi cue" anyconsole.analog="{\"sticks\":true}" anyconsole.bios="/bios" anyconsole.rewind="true" anyconsole.thumbnails="https://thumbnails.libretro.com/Sega%20-%20Dreamcast/Named_Boxarts/{name}.png" anyconsole.resolution="{\"height\":480,\"scales\":[1,2,3,4,5,6,7,8]}"
COPY --from=redream-download /redream/redream /opt/redream/redream
COPY --from=redream-commands /commands.so /opt/redream/
COPY redream/ /opt/redream/
COPY dreamcast-system.jq /usr/lib/jq/system.jq
ENV REDREAM_COMMANDS=/tmp/redream-commands
ENTRYPOINT ["/opt/redream/launch"]

FROM fetch AS cemu-download
ADD --checksum=sha256:0c20c4aeb800bb13d9bab9474ef45a6f8fcde6402cad9b32ac2a1bbd03186313 https://github.com/cemu-project/Cemu/releases/download/v2.6/Cemu-2.6-x86_64.AppImage /app.AppImage
RUN appimage /cemu

# wiiu Nintendo Wii U
FROM x11 AS wiiu
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y xdotool
LABEL anyconsole.launch="wux wud iso rpx wua" anyconsole.analog="{\"sticks\":true}" anyconsole.bios="/bios" anyconsole.screen="/opt/cemu/screen" anyconsole.thumbnails="https://thumbnails.libretro.com/Nintendo%20-%20Wii%20U/Named_Boxarts/{name}.png"
COPY --from=cemu-download /cemu /opt/cemu
COPY cemu/ /opt/cemu/
COPY wiiu-system.jq /usr/lib/jq/system.jq
ENV XDG_CONFIG_HOME=/engine XDG_DATA_HOME=/engine XDG_CACHE_HOME=/engine GDK_BACKEND=x11
ENTRYPOINT ["/opt/cemu/launch"]

FROM fetch AS rpcs3-download
ADD --checksum=sha256:21aa550a312b449ad1f1a2868ccd2100c3ae52f5a03e6fcb46774df0632195cb https://github.com/RPCS3/rpcs3-binaries-linux/releases/download/build-2ba2f75bcea7ef6bdd687046c7fcfb790faf9853/rpcs3-v0.0.43-20284-2ba2f75b_linux64.AppImage /app.AppImage
RUN appimage /rpcs3

FROM fedora AS rpcs3-config-reload
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y gcc python3-pyelftools patchelf
COPY --from=rpcs3-download /rpcs3 /rpcs3
COPY rpcs3/addresses rpcs3/config-reload.c /
RUN patchelf --add-needed /opt/rpcs3/config-reload.so /rpcs3/usr/bin/rpcs3 \
 && /addresses /rpcs3/usr/bin/rpcs3 > /addresses.flags \
 && gcc -O2 -Wall -Wextra -Werror -shared -fPIC $(cat /addresses.flags) -o /rpcs3/config-reload.so /config-reload.c

# ps3 PlayStation 3
FROM x11 AS ps3
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y matchbox-window-manager yq inotify-tools 7zip
COPY with-window-manager /usr/local/bin/
LABEL anyconsole.launch="sfo iso elf" anyconsole.analog="{\"sticks\":true}" anyconsole.bios="/bios" anyconsole.prepare="/opt/rpcs3/prepare" anyconsole.install="/opt/rpcs3/install" anyconsole.thumbnails="https://raw.githubusercontent.com/aldostools/Resources/main/COV/{name}.JPG" anyconsole.resolution="{\"height\":720,\"exact\":true}"
COPY --from=rpcs3-config-reload /rpcs3 /opt/rpcs3
RUN ln -s /opt/rpcs3/usr/share/rpcs3 /usr/share/rpcs3
COPY rpcs3/ /opt/rpcs3/
ADD https://api.rpcs3.net/config/?api=v1 /opt/rpcs3/config_database.dat
COPY ps3-system.jq /usr/lib/jq/system.jq
ENV XDG_CONFIG_HOME=/engine/config XDG_CACHE_HOME=/engine/cache QT_QPA_PLATFORM=xcb
ENV RPCS3_CONFIG=$XDG_CONFIG_HOME/rpcs3/config.yml
ENTRYPOINT ["/opt/rpcs3/launch"]

FROM fetch AS pcsx2-download
ADD --checksum=sha256:0c46bb6a88aa2782b10853a7b07cf3387ba99cbef2b966372cd2315b8571abea https://github.com/PCSX2/pcsx2/releases/download/v2.8.2/pcsx2-v2.8.2-linux-appimage-x64-Qt.AppImage /app.AppImage
RUN appimage /pcsx2

# ps2 PlayStation 2
FROM x11-rewind AS ps2
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y fontconfig freetype harfbuzz libcom_err libSM libICE matchbox-window-manager gawk
COPY with-window-manager /usr/local/bin/
LABEL anyconsole.launch="chd iso cso" anyconsole.analog="{\"sticks\":true,\"toggle\":{\"button\":\"guide\"}}" anyconsole.bios="/bios" anyconsole.vulkan="--vulkan" anyconsole.rewind="true" anyconsole.thumbnails="https://thumbnails.libretro.com/Sony%20-%20PlayStation%202/Named_Boxarts/{name}.png" anyconsole.resolution="{\"height\":448,\"scales\":[1,2,3,4,5,6,7,8,9,10,11,12]}"
COPY --from=pcsx2-download /pcsx2 /opt/pcsx2
COPY pcsx2/ analog-state.awk analog-log /opt/pcsx2/
COPY ps2-system.jq /usr/lib/jq/system.jq
ENV XDG_CONFIG_HOME=/engine QT_QPA_PLATFORM=xcb
ENTRYPOINT ["/opt/pcsx2/analog-log", "/opt/pcsx2/analog.awk", "/opt/pcsx2/launch"]

FROM fetch AS win98-download
ADD --checksum=sha256:287449f16ad234da1d546deac66b3f91ab3101f5d3a202472117b83de42ccc68 https://github.com/JHRobotics/softgpu/releases/download/v0.8.2025.53/softgpu-0.8.2025.53.zip /softgpu.zip
ADD --checksum=sha256:ca787d7c6421959c9f37afdd998feb9393fe07c1c63c43c4e22db5b170a79908 https://github.com/JHRobotics/patcher9x/releases/download/v0.9.91/patcher9x-0.9.91-linux-amd64.tar.gz /patcher9x.tar.gz
ADD --checksum=sha256:6060cfcc0eee1800d29ccbc45a1a28b94ca2604cfda5ca301dc89cf4d22e6ee5 https://files.emulace.cz/ac97_362.zip /ac97.zip
RUN tar xzf /patcher9x.tar.gz -C /usr/local/bin \
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
LABEL anyconsole.launch="img iso" anyconsole.bios="/bios" anyconsole.prepare="/opt/win98/windows" anyconsole.install="/opt/win98/install" anyconsole.devices="/dev/kvm" anyconsole.touchpad="raw" anyconsole.shared="/shared"
COPY --from=win98-download /usr/local/bin/patcher9x /usr/local/bin/
COPY --from=win98-download /base /opt/win98/base
COPY --from=win98-programs /anycon /opt/win98/base/anycon
COPY --from=win98-scsi /out/ /
COPY win98/ /opt/win98/
VOLUME /engine
ENTRYPOINT ["/opt/win98/launch"]
