#! /usr/bin/sudo /bin/bash

COMMON_PACKAGES="cmake gcc"
DNF_PACKAGES="$COMMON_PACKAGES gcc-c++"
APT_PACKAGES="$COMMON_PACKAGES g++"

which apt > /dev/null 2>&1
if [ $? == 0 ]; then
  apt update -y
  apt install -y $APT_PACKAGES
else
  dnf update -y 
  dnf install -y $DNF_PACKAGES
fi
