FROM registry.fedoraproject.org/fedora-minimal:44
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked dnf install -y --setopt=install_weak_deps=False python3 python3-pip libevdev systemd-libs linuxconsoletools
RUN pip install --no-cache-dir "hid-tools[uhid]==0.12"
COPY fakepad.py /
ENTRYPOINT ["python3", "/fakepad.py"]
