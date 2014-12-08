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
#
# options
# [XSERVER_GREATER_THAN_13=1] [BUSID_HAS_NUMBER=1] [clean] [install]
# for install, use "sudo ./fastbuild.sh install". Default path: /usr. 
#   To change to other place, use "prefix=<absolute dir>"
#
# for kernel 3.5.7, use "BUSID_HAS_NUMBER=1"
# for yocto, use "YOCTO=1"

# --------------------------------------------------------------------------------
# Cross build:
# --------------------------------------------------------------------------------
# if xserver version is 1.14, export this variable:
# bash>export XSERVER_GREATER_THAN_13=1
# build and install:
# bash>export PATH=<toolchain/bin>:$PATH
# bash>export CROSS_COMPILE=<toolchain prefix>
# bash>./fastbuild.sh sysroot=<system root> [BUILD_HARD_VFP=1] [YOCTO=1] [BUSID_HAS_NUMBER=1]
# bash>./fastbuild.sh [prefix=<absolute path to install>] install

# --------------------------------------------------------------------------------
# Native build:
# --------------------------------------------------------------------------------
# if xserver version is 1.14, export this variable:
# bash>export XSERVER_GREATER_THAN_13=1
# build and install:
# bash>./fastbuild.sh [BUILD_HARD_VFP=1] [YOCTO=1] [BUSID_HAS_NUMBER=1]
# bash>sudo ./fastbuild.sh install

make -C EXA/src/ -f makefile.linux $* || exit 1
#if [ "$XSERVER_GREATER_THAN_13" != "1" ]; then
#  make -C DRI_1.10.4/src/ -f makefile.linux $* || exit 1
#fi
make -C FslExt/src/ -f makefile.linux $* || exit 1
make -C util/autohdmi/ -f makefile.linux $* || exit 1
make -C util/pandisplay/ -f makefile.linux $* || exit 1

exit 0

