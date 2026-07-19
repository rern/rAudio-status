#!/bin/bash

[[ ! -e /usr/bin/pkg-config ]] && pacman -Sy --noconfirm pkgconfig

SECONDS=0

if [[ -e /boot/kernel.img ]]; then
	arch=armv6h
	file_bin=_status
	opt='
-Wno-psabi
-idirafter /usr/include
'
else
	[[ -e /boot/kernel7.img ]] && arch=armv7h || arch=aarch64
	file_bin=status
fi
     #strip
opt="
-O2
$opt
_status.cpp
-o $file_bin
$( pkg-config --cflags --libs alsa dbus-1 libcurl libmpdclient libupnpp taglib )
"
echo 
echo g++ $opt
echo ...

g++ $opt

mv /srv/http/bash/$file_bin{,.bak}
cp -f $file_bin /srv/http/bash
mv $file_bin status.$arch

echo Compile duration: $SECONDS seconds
