#!/bin/bash

arch=$( uname -m )
[[ $arch == armv6h ]] && opt='-Wno-psabi   -idirafter /usr/include'

# -Wno-psabi              - no warnings
# -idirafter /usr/include - sysroot path
# -O2                     - strip
cat << EOF
g++ -O2 $opt _status.cpp -o status.$arch \
    $( pkg-config --cflags --libs alsa dbus-1 libcurl libmpdclient libupnpp taglib )
EOF

g++ -O2 $opt _status.cpp -o status.$arch \
    $( pkg-config --cflags --libs alsa dbus-1 libcurl libmpdclient libupnpp taglib )

mv /srv/http/bash/status{,.bak}
cp status{,.$arch}
cp status /srv/http/bash
