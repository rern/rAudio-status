### sysroot upgrade
sed -i '/REPOSITORIES/,$ d' /etc/pacman.conf
cat << EOF >> /etc/pacman.conf
[+R]
SigLevel = Optional TrustAll
Server = file:///home/x/GitHub/rern.github.io/armv6h/+R

[alarm]
SigLevel = Optional TrustAll
Server = file:///home/x/GitHub/rern.github.io/armv6h/alarm

[core]
SigLevel = Optional TrustAll
Server = file:///home/x/GitHub/rern.github.io/armv6h/core

[extra]
SigLevel = Optional TrustAll
Server = file:///home/x/GitHub/rern.github.io/armv6h/extra
EOF

pacman -Syy coreutils curl cryptsetup gcc glibc gpgme kmod krb5 \
    libarchive libssh2 libubsan mkinitcpio pacman openssl openssl-1.1 \
    --overwrite '*'

# compiled 'status'
g++ -O2 $opt _status.cpp -o status \
    $( pkg-config --cflags --libs alsa dbus-1 libcurl libmpdclient libupnpp taglib )

# copy status to /srv/http/bash
scp status root@192.168.1.90:/srv/http/bash

### on rpi - install new libraries
curl -sL https://github.com/rern/rAudio-status/raw/main/lib.tar.xz | bsdtar xpf - -C /

list_libs="\
ld-linux-armhf.so.3
libc.so.6
libdl.so.2
libgcc_s.so.1
libm.so.6
libnss_dns.so.2
libnss_files.so.2
libpthread.so.0
libresolv.so.2
librt.so.1
libstdc++.so.6
libutil.so.1
"

# script to run status binary
cat << EOF > /srv/http/bash/run-status.sh
#!/bin/bash

exec unshare --mount --propagation private bash -c '
  mkdir -p /tmp/mergedlib
  mount -t overlay overlay -o lowerdir=/opt/armv6-new/lib:/usr/lib,ro /tmp/mergedlib
  mount --bind /tmp/mergedlib /usr/lib
  exec /srv/http/bash/status
'
EOF
chmod +x /srv/http/bash/run-status.sh
