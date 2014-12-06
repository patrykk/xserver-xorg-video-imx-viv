/****************************************************************************
*
*    Copyright (C) 2005 - 2014 by Vivante Corp.
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


#ifndef _VIV_EXT
#define _VIV_EXT

#define X_VIVEXTQueryVersion                   0
#define X_VIVEXTPixmapPhysaddr                 1
#define X_VIVEXTDrawableFlush                  2
#define X_VIVEXTDrawableInfo                   3
#define X_VIVEXTFULLScreenInfo                 4
#define X_VIVEXTDrawableGetFlag                10
#define X_VIVEXTDrawableSetFlag                11
#define X_VIVEXTPixmapSync                     12
#define X_VIVEXTRefreshVideoModes              15
#define X_VIVEXTDisplayFlip                    16
#define X_VIVEXTGetExaSettings                 17


#define VIVEXTNumberEvents   		0

#define VIVEXTClientNotLocal		0
#define VIVEXTOperationNotSupported	1
#define VIVEXTNumberErrors		    (VIVEXTOperationNotSupported + 1)



#define VIVEXTNAME "vivext"

/*
#define XORG_VERSION_CURRENT 10.4
*/

#define VIVEXT_MAJOR_VERSION   1
#define VIVEXT_MINOR_VERSION   0
#define VIVEXT_PATCH_VERSION   0

typedef struct _VIVEXTQueryVersion {
	CARD8	reqType;
	CARD8	vivEXTReqType;
	CARD16	length B16;
} xVIVEXTQueryVersionReq;
#define sz_xVIVEXTQueryVersionReq	4


typedef struct {
	BYTE	type;/* X_Reply */
	BYTE	pad1;
	CARD16	sequenceNumber B16;
	CARD32	length B32;
	CARD16	majorVersion B16;		/* major version of vivEXT protocol */
	CARD16	minorVersion B16;		/* minor version of vivEXT protocol */
	CARD32	patchVersion B32;		/* patch version of vivEXT protocol */
	CARD32	pad3 B32;
	CARD32	pad4 B32;
	CARD32	pad5 B32;
	CARD32	pad6 B32;
} xVIVEXTQueryVersionReply;
#define sz_xVIVEXTQueryVersionReply	32


typedef struct _VIVEXTDrawableFlush {
	CARD8	reqType;		/* always vivEXTReqCode */
	CARD8	vivEXTReqType;		/* always X_vivEXTDrawableFlush */
	CARD16	length B16;
	CARD32	screen B32;
	CARD32	drawable B32;
} xVIVEXTDrawableFlushReq;
#define sz_xVIVEXTDrawableFlushReq	12



typedef struct _VIVEXTDrawableInfo {
	CARD8	reqType;
	CARD8	vivEXTReqType;
	CARD16	length B16;
	CARD32	screen B32;
	CARD32	drawable B32;
} xVIVEXTDrawableInfoReq;
#define sz_xVIVEXTDrawableInfoReq	12

typedef struct {
	BYTE	type;			/* X_Reply */
	BYTE	pad1;
	CARD16	sequenceNumber B16;
	CARD32	length B32;
	INT16	drawableX B16;
	INT16	drawableY B16;
	INT16	drawableWidth B16;
	INT16	drawableHeight B16;
	CARD32	numClipRects B32;
	INT16       relX B16;
	INT16       relY B16;
	CARD32      alignedWidth B32;
	CARD32      alignedHeight B32;
	CARD32      stride B32;
#if GPU_VERSION_GREATER_THAN(5, 0, 9, 17083)
	CARD32      nodeName B32;
#else
	CARD32      backNode B32;
#endif
	CARD32      phyAddress B32;
} xVIVEXTDrawableInfoReply;

#define sz_xVIVEXTDrawableInfoReply	44


typedef struct _VIVEXTFULLScreenInfo {
	CARD8	reqType;
	CARD8	vivEXTReqType;
	CARD16	length B16;
	CARD32	screen B32;
	CARD32	drawable B32;
} xVIVEXTFULLScreenInfoReq;
#define sz_xVIVEXTFULLScreenInfoReq	12

typedef struct {
	BYTE	type;			/* X_Reply */
	BYTE	pad1;
	CARD16	sequenceNumber B16;
	CARD32	length B32;
	CARD32	fullscreenCovered B32;	/* if fullscreen is covered by windows, set to 1 otherwise 0 */
	CARD32	pad3 B32;
	CARD32	pad4 B32;
	CARD32	pad5 B32;
	CARD32	pad6 B32;
	CARD32	pad7 B32;		/* bytes 29-32 */
} xVIVEXTFULLScreenInfoReply;
#define	sz_xVIVEXTFULLScreenInfoReply 32

/************************************************************************/

typedef struct {
	CARD8	reqType;	/* always XTestReqCode */
	CARD8	xtReqType;	/* always X_IMX_EXT_GetPixmapPhysaddr */
	CARD16	length B16;
	Pixmap	pixmap B32;
} xVIVEXTPixmapPhysaddrReq;
#define sz_xVIVEXTPixmapPhysaddrReq 8

typedef enum
{
	VIV_PixmapUndefined,	/* pixmap is not defined */
	VIV_PixmapFramebuffer,	/* pixmap is in framebuffer */
	VIV_PixmapOther		/* pixmap is not in framebuffer */
} VIVEXT_PixmapState;

typedef struct {
	CARD8	type;			/* must be X_Reply */
	CARD8	pixmapState;		/* has value of IMX_EXT_PixmapState */
	CARD16	sequenceNumber B16;	/* of last request received by server */
	CARD32	length B32;		/* 4 byte quantities beyond size of GenericReply */
	CARD32	PixmapPhysaddr B32;	/* pixmap phys addr; otherwise NULL */
	CARD32	pixmapStride B32;	/* bytes between lines in pixmap */
	CARD32	pad0 B32;		/* bytes 17-20 */
	CARD32	pad1 B32;		/* bytes 21-24 */
	CARD32	pad2 B32;		/* bytes 25-28 */
	CARD32	pad3 B32;		/* bytes 29-32 */
} xVIVEXTPixmapPhysaddrReply;
#define	sz_xVIVEXTPixmapPhysaddrReply 32


#define VIVPIXMAP_FLAG_SHARED_CLIENTWRITE_SERVERREAD 1
typedef struct _VIVEXTDrawableSetFlag {
	CARD8	reqType;		/* always vivEXTReqCode */
	CARD8	vivEXTReqType;		/* always X_VIVEXTDrawableSetFlag */
	CARD16	length B16;
	CARD32	screen B32;
	CARD32	drawable B32;
	CARD32  flag B32;
} xVIVEXTDrawableSetFlagReq;
#define sz_xVIVEXTDrawableSetFlagReq	16

typedef struct {
	CARD8	reqType;	/* always vivEXTReqCode */
	CARD8	xtReqType;	/* always X_VIVEXTPixmapSync */
	CARD16	length B16;
	CARD32	screen B32;
	Pixmap	pixmap B32;
} xVIVEXTPixmapSyncReq;
#define sz_xVIVEXTPixmapSyncReq 12

typedef struct _VIVEXTRefreshVideoModes {
    CARD8   reqType;                /* always vivEXTReqCode */
    CARD8   vivEXTReqType;          /* always X_VIVEXTRefreshVideoModes */
    CARD16  length B16;
    CARD32  screen B32;
    CARD32  fb B32;
} xVIVEXTRefreshVideoModesReq;
#define sz_xVIVEXTRefreshVideoModesReq   12

typedef struct {
    BYTE    type;			/* X_Reply */
    BYTE    pad1;
    CARD16  sequenceNumber B16;
    CARD32  length B32;
    CARD32  preferModeLen B32;
    CARD32  pad3 B32;
    CARD32  pad4 B32;
    CARD32  pad5 B32;
    CARD32  pad6 B32;
    CARD32  pad7 B32;
} xVIVEXTRefreshVideoModesReply;
#define sz_xVIVEXTRefreshVideoModesReply 32


/* Fix tearing: update back surface */
typedef struct _VIVEXTDisplayFlip {
       CARD8   reqType;                /* always vivEXTReqCode */
       CARD8   vivEXTReqType;          /* always X_VIVEXTDisplayFlip */
       CARD16  length B16;
       CARD32  screen B32;
       CARD32  restore B32;
} xVIVEXTDisplayFlipReq;
#define sz_xVIVEXTDisplayFlipReq   12

typedef struct _VIVEXTGetExaSettings {
    CARD8   reqType;                /* always vivEXTReqCode */
    CARD8   vivEXTReqType;          /* always X_VIVEXTRefreshVideoModes */
    CARD16  length B16;
    CARD32  screen B32;
} xVIVEXTGetExaSettingsReq;
#define sz_xVIVEXTGetExaSettingsReq   8

typedef struct {
    BYTE    type;			/* X_Reply */
    BYTE    pad1;
    CARD16  sequenceNumber B16;
    CARD32  length B32;
    CARD32  flags B32;
    CARD32  pad3 B32;
    CARD32  pad4 B32;
    CARD32  pad5 B32;
    CARD32  pad6 B32;
    CARD32  pad7 B32;
} xVIVEXTGetExaSettingsReply;
#define sz_xVIVEXTGetExaSettingsReply 32

void VIVExtensionInit(void);


#endif


