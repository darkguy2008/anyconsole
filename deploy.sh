#!/bin/sh
set -eu
cd "$(dirname "$0")"
INDEX=vm/rel/index
. vm/guest/otad.conf

if [ "${ANYCONSOLE_ONHOST:-}" != 1 ]; then
  [ "${1:-}" = force ] || echo $(( $(cat VERSION) + 1 )) > VERSION
  rsync -a --delete --exclude .git --exclude-from=.gitignore ./ "$DEPLOY_HOST:$DEPLOY_PATH/"
  exec ssh "$DEPLOY_HOST" "cd $DEPLOY_PATH && ANYCONSOLE_ONHOST=1 ./deploy.sh $*"
fi

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

version=$(cat VERSION)
if [ "${1:-}" != force ]; then
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
