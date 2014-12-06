/*
 * Copyright (C) 2013-2014 Freescale Semiconductor, Inc.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without 
 * restriction, including without limitation the rights to use, copy, 
 * modify, merge, publish, distribute, sublicense, and/or sell copies 
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS 
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN 
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN 
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
 * SOFTWARE.
 */

#include "fsl_pixmap_ext.h"

#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#include "HAL/gc_hal.h"

#include <stdio.h>
#define FSLPRINTF printf

/*****************************************************************************
 * X Extensions (VIV & FSL)
 ****************************************************************************/
#define X_VIVEXTDrawableInfo        3
#define X_VIVEXTDrawableSetFlag     11
#define X_VIVEXTPixmapSync          12

#define VIVEXTNAME "vivext"

struct clip_rect {
    unsigned short x1;
    unsigned short y1;
    unsigned short x2;
    unsigned short y2;
};

typedef struct _VIVEXTDrawableInfo {
	CARD8	reqType;
	CARD8	vivEXTReqType;
	CARD16	length B16;
	CARD32	screen B32;
	CARD32	drawable B32;
} xVIVEXTDrawableInfoReq;
#define sz_xVIVEXTDrawableInfoReq	12

typedef struct {
	BYTE	type;/* X_Reply */
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
	CARD32      backNode B32;
	CARD32      phyAddress B32;
} xVIVEXTDrawableInfoReply;
#define sz_xVIVEXTDrawableInfoReply	44


#define VIVPIXMAP_FLAG_SHARED_CLIENTWRITE_SERVERREAD 1
typedef struct _VIVEXTDrawableSetFlag {
       CARD8   reqType;                /* always vivEXTReqCode */
       CARD8   vivEXTReqType;          /* always X_VIVEXTDrawableSetFlag */
       CARD16  length B16;
       CARD32  screen B32;
       CARD32  drawable B32;
       CARD32  flag B32;
} xVIVEXTDrawableSetFlagReq;
#define sz_xVIVEXTDrawableSetFlagReq   16

typedef struct {
	CARD8	reqType;	/* always vivEXTReqCode */
	CARD8	vivEXTReqType;	/* always X_VIVEXTPixmapSync */
	CARD16	length B16;
	CARD32	screen B32;
	Pixmap	pixmap B32;
} xVIVEXTPixmapSyncReq;
#define sz_xVIVEXTPixmapSyncReq 12

static XExtensionInfo _VIVEXT_info_data;
static XExtensionInfo *VIVEXT_info = &_VIVEXT_info_data;
static /*const */char *VIVEXT_extension_name = VIVEXTNAME;

#define VIVEXTCheckExtension(dpy,i,val) \
  XextCheckExtension (dpy, i, VIVEXT_extension_name, val)

/*****************************************************************************
 *                                                                           *
 *                           private utility routines                          *
 *                                                                           *
 *****************************************************************************/

static int close_display(Display *dpy, XExtCodes *extCodes);
static /* const */ XExtensionHooks VIVEXT_extension_hooks = {
    NULL,                                /* create_gc */
    NULL,                                /* copy_gc */
    NULL,                                /* flush_gc */
    NULL,                                /* free_gc */
    NULL,                                /* create_font */
    NULL,                                /* free_font */
    close_display,                        /* close_display */
    NULL,                                /* wire_to_event */
    NULL,                                /* event_to_wire */
    NULL,                                /* error */
    NULL,                                /* error_string */
};

static XEXT_GENERATE_FIND_DISPLAY (find_display, VIVEXT_info,
                                   VIVEXT_extension_name,
                                   &VIVEXT_extension_hooks,
                                   0, NULL)

static XEXT_GENERATE_CLOSE_DISPLAY (close_display, VIVEXT_info)


Bool VIVEXTDrawableSetFlag(Display *dpy, unsigned int screen, unsigned int drawable, unsigned int flag)
{
    XExtDisplayInfo *info = find_display (dpy);
    xVIVEXTDrawableSetFlagReq *req;

    VIVEXTCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(VIVEXTDrawableSetFlag, req);
    req->reqType = info->codes->major_opcode;
    req->vivEXTReqType = X_VIVEXTDrawableSetFlag;
    req->screen = screen;
    req->drawable = drawable;
    req->flag = flag;

    UnlockDisplay(dpy);
    SyncHandle();
    return True;
}

Bool VIVEXTDrawableInfo(Display* dpy, int screen, Drawable drawable,
    int* X, int* Y, int* W, int* H,
    int* numClipRects, struct clip_rect ** pClipRects,
    int* relX,
    int* relY,
    unsigned int * alignedWidth,
    unsigned int * alignedHeight,
    unsigned int * stride,
    unsigned int * backNode,
    unsigned int * phyAddress
)
{

    XExtDisplayInfo *info = find_display (dpy);
    xVIVEXTDrawableInfoReply rep;
    xVIVEXTDrawableInfoReq *req;
    int extranums = 0;

    VIVEXTCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(VIVEXTDrawableInfo, req);
    req->reqType = info->codes->major_opcode;
    req->vivEXTReqType = X_VIVEXTDrawableInfo;
    req->screen = screen;
    req->drawable = drawable;

    extranums = ( sizeof(xVIVEXTDrawableInfoReply) - 32 ) / 4;

    if (!_XReply(dpy, (xReply *)&rep, extranums , xFalse))
    {
        UnlockDisplay(dpy);
        SyncHandle();
        return False;
    }

    *X = (int)rep.drawableX;
    *Y = (int)rep.drawableY;
    *W = (int)rep.drawableWidth;
    *H = (int)rep.drawableHeight;
    *numClipRects = rep.numClipRects;
    *alignedWidth = (unsigned int)rep.alignedWidth;
    *alignedHeight = (unsigned int)rep.alignedHeight;
    *stride = (unsigned int)rep.stride;
    *backNode = (unsigned int)rep.backNode;
    *phyAddress = (unsigned int)rep.phyAddress;

    *relX = rep.relX;
    *relY = rep.relY;

    if (*numClipRects) {
       int len = sizeof(struct clip_rect) * (*numClipRects);

       *pClipRects = (struct clip_rect *)Xcalloc(len, 1);
       if (*pClipRects)
          _XRead(dpy, (char*)*pClipRects, len);
    } else {
        *pClipRects = NULL;
    }

    UnlockDisplay(dpy);
    SyncHandle();

    return True;
}

Bool VIVEXTPixmapSync(Display *dpy, unsigned int screen, Pixmap pixmap)
{
    XExtDisplayInfo *info = find_display (dpy);
    xVIVEXTPixmapSyncReq *req;

    VIVEXTCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(VIVEXTPixmapSync, req);
    req->reqType = info->codes->major_opcode;
    req->vivEXTReqType = X_VIVEXTPixmapSync;
    req->screen = screen;
    req->pixmap = pixmap;

    UnlockDisplay(dpy);
    SyncHandle();
    return True;
}

static gceSTATUS _LockVideoNode(
        gcoHAL Hal,
        gctUINT32 Node,
        gctUINT32 *Address,
        gctPOINTER *Memory)
{
    gceSTATUS status;
    gcsHAL_INTERFACE iface;

    gcmASSERT(Address != gcvNULL);
    gcmASSERT(Memory != gcvNULL);
    gcmASSERT(Node != 0);

    memset(&iface, 0, sizeof(gcsHAL_INTERFACE));

    iface.command = gcvHAL_LOCK_VIDEO_MEMORY;
    iface.u.LockVideoMemory.node = Node;
    iface.u.LockVideoMemory.cacheable = gcvFALSE;
    /* Call kernel API. */
    gcmONERROR(gcoHAL_Call(Hal, &iface));

    /* Get allocated node in video memory. */
    *Address = iface.u.LockVideoMemory.address;
    *Memory = gcmUINT64_TO_PTR(iface.u.LockVideoMemory.memory);
OnError:
    return status;
}

static gceSTATUS _UnlockVideoNode(
        gcoHAL Hal,
        gctUINT32 Node)
{
    gcsHAL_INTERFACE iface;
    gceSTATUS status;

    gcmASSERT(Node != 0);

    memset(&iface, 0, sizeof(gcsHAL_INTERFACE));
    iface.command = gcvHAL_UNLOCK_VIDEO_MEMORY;
    iface.u.UnlockVideoMemory.node = Node;
    iface.u.UnlockVideoMemory.type = gcvSURF_BITMAP;
    iface.u.UnlockVideoMemory.asynchroneous = gcvTRUE;
    /* Call kernel API. */
    gcmONERROR(gcoOS_DeviceControl(
               gcvNULL,
               IOCTL_GCHAL_INTERFACE,
               &iface, gcmSIZEOF(iface),
               &iface, gcmSIZEOF(iface)));
    gcmONERROR(iface.status);

OnError:
    return status;
}


/*
 * Get logical address of a Pixmap (mapping into caller process)
 *
 * CAUTION
 * must not call this function when this pixmap is in use
 * cannot be used on shared pixmap
 */
void * FslLockPixmap(Display *dpy, Pixmap pixmap, int *stride)
{
    int x;
    int y;
    int w;
    int h;
    struct clip_rect *clips;
    int clipCount;
    int relX;
    int relY;
    int alignedWidth;
    int alignedHeight;
    unsigned int backNode;
    unsigned int phyAddr[3] = {0, 0, 0};
    unsigned int logAddr[3] = {0, 0, 0};
    Bool rc;

    // client wants to access Pixmap; make it noncacheable
    rc = VIVEXTDrawableSetFlag(dpy, DefaultScreen(dpy), pixmap, VIVPIXMAP_FLAG_SHARED_CLIENTWRITE_SERVERREAD);
    if(!rc)
    {
        FSLPRINTF("Error: failed to share Pixmap 0x%08x\n", (int)pixmap);
        return NULL;
    }

    // query physical address
    clips = NULL;
    rc = VIVEXTDrawableInfo(dpy, DefaultScreen(dpy), pixmap,
                &x, &y, &w, &h,
                &clipCount, &clips,
                &relX,
                &relY,
                (unsigned int *)&alignedWidth,
                (unsigned int *)&alignedHeight,
                (unsigned int *)stride,
                &backNode,
                &phyAddr[0]);
    if(clips != NULL)
        Xfree(clips);

    if(rc && backNode != 0)
    {
        if (_LockVideoNode(0, backNode, &phyAddr[0], (gctPOINTER *)&logAddr[0]) == gcvSTATUS_MEMORY_LOCKED)
        {
            _UnlockVideoNode(0, backNode);
            _LockVideoNode(0, backNode, &phyAddr[0], (gctPOINTER *)&logAddr[0]);
        }
    }
    else
    {
        FSLPRINTF("Error: failed to get back node for Pixmap 0x%08x\n", (int)pixmap);
        return NULL;
    }

    return (void *)logAddr[0];
}

/*
 * Unmap logical address of Pixmap from caller process
 */
void FslUnlockPixmap(Display *dpy, Pixmap pixmap)
{
    int x;
    int y;
    int w;
    int h;
    struct clip_rect *clips;
    int clipCount;
    int relX;
    int relY;
    int alignedWidth;
    int alignedHeight;
    int stride;
    unsigned int backNode;
    unsigned int phyAddr[3] = {0, 0, 0};
    unsigned int logAddr[3] = {0, 0, 0};
    Bool rc;

    // query back node
    clips = NULL;
    rc = VIVEXTDrawableInfo(dpy, DefaultScreen(dpy), pixmap,
                &x, &y, &w, &h,
                &clipCount, &clips,
                &relX,
                &relY,
                (unsigned int *)&alignedWidth,
                (unsigned int *)&alignedHeight,
                (unsigned int *)&stride,
                &backNode,
                &phyAddr[0]);
    if(clips != NULL)
        Xfree(clips);

    if(rc && backNode != 0)
    {
        _UnlockVideoNode(0, backNode);
    }
    else
    {
        FSLPRINTF("Warning: failed to get back node for Pixmap 0x%08x\n", (int)pixmap);
    }
}

/*
 * Request xserver to finish access to Pixmap
 */
void FslSyncPixmap(Display *dpy, Pixmap pixmap)
{
    VIVEXTPixmapSync(dpy, DefaultScreen(dpy), pixmap);
}

