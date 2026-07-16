#!/bin/bash

[[ ! -e /usr/bin/pkg-config ]] && pacman -Sy --noconfirm pkgconfig

SECONDS=0

if [[ -e /boot/kernel8.img ]]; then
	arch=aarch64
elif [[ -e /boot/kernel7.img ]]; then
	arch=armv7h
else
	if grep -q system-container /proc/self/cgroup; then
		echo 'Run sysroot with boot -b option'
		exit
#-------------------------------------------------------------------------------
	fi
	arch=armv6h     #fix sysroot path
	opt='-Wno-psabi -idirafter /usr/include'
         #suppress warnings
#                   - fix sysroot path
fi

cat << EOF
g++ -O2 $opt _status.cpp -o status \
    $( pkg-config --cflags --libs alsa dbus-1 libcurl libmpdclient libupnpp taglib )
...
EOF

    #strip
g++ -O2 $opt _status.cpp -o status \
    $( pkg-config --cflags --libs alsa dbus-1 libcurl libmpdclient libupnpp taglib )

if [[ $arch == armv6h ]]; then
	mv /srv/http/bash/_status{,.bak}
	cp -f status /srv/http/bash/_status
	cp -f status{,.$arch}
else
	mv /srv/http/bash/status{,.bak}
	cp -f status /srv/http/bash
	cp -f status{,.$arch}
fi

echo Compile duration: $SECONDS seconds
