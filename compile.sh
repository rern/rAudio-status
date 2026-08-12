#!/bin/bash

[[ ! -e /usr/bin/pkg-config ]] && pacman -Sy --noconfirm pkgconfig

SECONDS=0

arch=aarch64
file_bin=status
if [[ -e /boot/kernel.img ]]; then
	arch=armv6h
	file_bin=_status
	opt='
-Wno-psabi
-idirafter /usr/include
'
elif [[ -e /boot/kernel7.img ]]; then
	arch=armv7h
	opt='
-Wno-psabi
'
fi
opt="
$( [[ ! $1 ]] && echo -O2 )
$opt
_status.cpp
-o $file_bin
$( pkg-config --cflags --libs alsa dbus-1 libcurl libmpdclient libupnpp taglib )
"
echo 
echo g++ $opt
echo ...

g++ $opt

[[ $? != 0 ]] && exit

#mv /srv/http/bash/$file_bin{,.bak}
cp -f $file_bin /srv/http/bash
mv $file_bin status.$arch

echo Compile duration: $SECONDS seconds
