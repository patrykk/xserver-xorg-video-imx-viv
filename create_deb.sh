#!/bin/bash
#
#    Copyright (C) 2013 by Freescale Semiconductor, Inc.
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the license, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not write to the Free Software
#    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#


#export XACCVER=3.0.35-4.1.0
#export DEBFILE=xorg1.10.4-acc-viv4.6.9p12-linux$XACCVER.deb
echo "Creating xserver acceleration release package ..."
rm -r release
mkdir release || exit 1
mkdir release/DEBIAN || exit 1
mkdir -p release/usr/lib/xorg/modules/drivers/    || exit 1
mkdir -p release/usr/lib/xorg/modules/extensions/ || exit 1
cp EXA/src/vivante_drv.so   release/usr/lib/xorg/modules/drivers/    || exit 1
cp DRI_1.10.4/src/libdri.so release/usr/lib/xorg/modules/extensions/ || exit 1
echo "Package: XAcc"        > release/DEBIAN/control
echo "Version: $XACCVER"   >> release/DEBIAN/control
echo "Architecture: armel" >> release/DEBIAN/control
echo "Description: Xserver acceleration with Vivante gpu drivers" >> release/DEBIAN/control
dpkg -b release $DEBFILE || exit 1
echo "Package $DEBFILE is generated"
rm -r release
exit 0

