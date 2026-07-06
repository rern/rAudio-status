# sysroot upgrade
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

pacman -Syy archlinuxarm-keyring firmware-raspberrypi \
    linux-firmware linux-rpi raspberrypi-bootloader raspberrypi-utils \
    --overwrite '*'
reboot

pacman -S filesystem gcc glibc --overwrite '*'
pacman -Sdd cryptsetup gpgme pacman openssl openssl-1.1 --overwrite '*'
pacman -S coreutils curl kmod krb5 libarchive libssh2 libubsan mkinitcpio --overwrite '*'

# compiled 'status'
g++ -O2 $ -idirafter /usr/include _status.cpp -o /srv/http/bash/status \
    $( pkg-config --cflags --libs alsa dbus-1 libcurl libmpdclient libupnpp taglib )

# copy status to /srv/http/bash

# copy /lib/... to /opt/armv6-new/lib/
libs="\
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
