#!/bin/sh
set -eu
cd "$(dirname "$0")"
INDEX=vm/rel/index

serve() {
  printf 'use chroot = no\nuid = root\ngid = root\n[rel]\npath = %s/vm/rel\nread only = yes\n' \
    "$PWD" > /etc/rsyncd.conf
  rc-service rsyncd started || rc-service rsyncd start
}

case "${1:-}" in
  force) mode=${2:-} ;;
  --now) mode=all ;;
  *) mode= ;;
esac

if [ "${1:-}" = force ]; then
  version=$(cat VERSION)
else
  version=$(( $(cat VERSION) + 1 ))
  echo "$version" > VERSION
  date -u > app/changelog
  printf '#!/bin/sh\necho "feature %s online"\n' "$version" > "app/feature-$version"
  chmod +x "app/feature-$version"
  sh vm/rootfs.sh >/dev/null 2>&1
fi

{
  echo "REL=$version"
  case "$mode" in
    install) printf 'SYNC_DELAY=0\nAUTO_INSTALL=1\n' ;;
    all) printf 'SYNC_DELAY=0\nAUTO_INSTALL=1\nAUTO_REBOOT=1\n' ;;
  esac
} > "$INDEX"
serve
echo "published v$version mode=${mode:-none}"
