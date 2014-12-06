/****************************************************************************
*
*    Copyright (C) 2005 - 2013 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/


#ifndef VIVANTE_COMMON_H
#define    VIVANTE_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif
    /*Normal Includes*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


    /* X Window */
#include "xorg-server.h"
#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86xv.h"
#include "xf86RandR12.h"
#include "xorg-server.h"


#include "mipointer.h"
//#include "mibstore.h"
#include "micmap.h"
#include "mipointrst.h"
#include "inputstr.h"
#include "colormapst.h"
#include "xf86cmap.h"
#include "shadow.h"
#include "dgaproc.h"

    /* for visuals */
#include "fb.h"

#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#include "xf86RAC.h"
#endif

    /*FrameBuffer*/
#include "fbdevhw.h"
    /*For EXA*/
#include "exa.h"

    /*For Cursor*/
#include "xf86.h"
#include "xf86Cursor.h"
#include "xf86Crtc.h"
#include "cursorstr.h"

    /* System API compatability */
#include "compat-api.h"

    /*Debug*/
#include "vivante_debug.h"

#define V_MIN(a,b) ((a)>(b)?(b):(a))
#define V_MAX(a,b) ((a)>(b)?(a):(b))

//#define ALL_NONCACHE_BIGSURFACE 1

// i.mx6q: ipu requires address to be 8-byte aligned; stride 4-byte
//             gpu: address to be 64-byte aligned; stride 16-pixel aligned; height should be 8-pixel aligned
// considering rotation, width & height both aligned to 16 pixels
#define ADDRESS_ALIGNMENT 64
#define WIDTH_ALIGNMENT 16
#define HEIGHT_ALIGNMENT 16
#define BITSTOBYTES(x) (((x)+7)/8)

#define    IMX_EXA_NONCACHESURF_WIDTH 1024
#define    IMX_EXA_NONCACHESURF_HEIGHT 1024
#define    IMX_EXA_NONCACHESURF_SIZE ( IMX_EXA_NONCACHESURF_WIDTH * IMX_EXA_NONCACHESURF_HEIGHT )

#define    SURF_SIZE_FOR_SW(sw,sh) do {    \
                        if ( gcmALIGN(sw, WIDTH_ALIGNMENT) < IMX_EXA_NONCACHESURF_WIDTH    \
                        || gcmALIGN(sh, HEIGHT_ALIGNMENT) < IMX_EXA_NONCACHESURF_HEIGHT)    \
                        TRACE_EXIT(FALSE);    \
                } while ( 0 )

#define    SURF_SIZE_FOR_SW_COND(sw,sh) (    \
                        ( gcmALIGN(sw, WIDTH_ALIGNMENT) < IMX_EXA_NONCACHESURF_WIDTH    \
                        || gcmALIGN(sh, HEIGHT_ALIGNMENT) < IMX_EXA_NONCACHESURF_HEIGHT )    \
                        )

/* Align an offset to an arbitrary alignment */
#define IMX_ALIGN(offset, align) 	\
	(((offset) + (align) - 1) - (((offset) + (align) - 1) % (align)))

#ifdef __cplusplus
}
#endif

#endif    /* VIVANTE_COMMON_H */

