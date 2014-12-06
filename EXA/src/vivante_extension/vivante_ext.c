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


#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xorg-server.h"

#include <errno.h>
#include <string.h>
#include "xf86.h"
#include <X11/X.h>
#include <X11/Xproto.h>
#include "scrnintstr.h"
#include "windowstr.h"
#include "misc.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "extinit.h"
#include "colormapst.h"
#include "cursorstr.h"
#include "scrnintstr.h"
#include "servermd.h"
#include "swaprep.h"
#include "drm.h"
#include "xf86Module.h"
#include "globals.h"
#include "pixmapstr.h"
/*
#include "xf86Extensions.h"
*/
#include "vivante_debug.h"
#include "vivante_ext.h"
#include "vivante_exa.h"
#include "vivante.h"
#include "vivante_priv.h"
#include "vivante_common.h"

static unsigned char VIVEXTReqCode = 0;
static int VIVEXTErrorBase;

static int ProcVIVEXTDrawableFlush(register ClientPtr client)
{
	DrawablePtr 	pDrawable;
	WindowPtr	pWin;
	ScreenPtr 	pScreen;
	PixmapPtr      pWinPixmap;
	Viv2DPixmapPtr ppriv = NULL;
	int rc;
	GenericSurfacePtr surf;

	REQUEST(xVIVEXTDrawableFlushReq);
	REQUEST_SIZE_MATCH(xVIVEXTDrawableFlushReq);
	if (stuff->screen >= screenInfo.numScreens) {
		client->errorValue = stuff->screen;
		return BadValue;
	}

	rc = dixLookupDrawable(&pDrawable, stuff->drawable, client, 0,
			DixReadAccess);

	if (rc != Success)
		return rc;

	if ( pDrawable->type == DRAWABLE_WINDOW)
	{
		pWin = (WindowPtr)pDrawable;

		pScreen = screenInfo.screens[stuff->screen];

		pWinPixmap = pScreen->GetWindowPixmap(pWin);

		ppriv = (Viv2DPixmapPtr)exaGetPixmapDriverPrivate(pWinPixmap);

		if (ppriv) {
			surf = (GenericSurfacePtr)ppriv->mVidMemInfo;
			gcoOS_CacheFlush(gcvNULL, (gctUINT32)surf->mVideoNode.mNode, surf->mVideoNode.mLogicalAddr, surf->mStride * surf->mAlignedHeight);
			ppriv->mCpuBusy = FALSE;

		}
	}

	if (pDrawable->type == DRAWABLE_PIXMAP)
	{

		ppriv = (Viv2DPixmapPtr)exaGetPixmapDriverPrivate((PixmapPtr)pDrawable);

		if ( ppriv )
		{
			surf = (GenericSurfacePtr)ppriv->mVidMemInfo;
			gcoOS_CacheFlush(gcvNULL, (gctUINT32)surf->mVideoNode.mNode, surf->mVideoNode.mLogicalAddr, surf->mStride * surf->mAlignedHeight);
			ppriv->mCpuBusy = FALSE;
		}
	}

	return  Success;
}

// Return value: 0 for ok; otherwise failed
static int ProcVIVEXTDrawableSetFlag(register ClientPtr client)
{
    DrawablePtr 	pDrawable;
    WindowPtr	pWin;
    ScreenPtr 	pScreen;
    PixmapPtr      pWinPixmap;
    Viv2DPixmapPtr ppriv = NULL;
    int rc;
    GenericSurfacePtr surf;

    REQUEST(xVIVEXTDrawableSetFlagReq);
    REQUEST_SIZE_MATCH(xVIVEXTDrawableSetFlagReq);
    if (stuff->screen >= screenInfo.numScreens)
    {
        client->errorValue = stuff->screen;
        return -1;
    }

    rc = dixLookupDrawable(&pDrawable, stuff->drawable, client, 0, DixReadAccess);

    if (rc != Success)
        return -1;

    if ( pDrawable->type == DRAWABLE_WINDOW)
    {
        pWin = (WindowPtr)pDrawable;
        pScreen = screenInfo.screens[stuff->screen];
        pWinPixmap = pScreen->GetWindowPixmap(pWin);
        ppriv = (Viv2DPixmapPtr)exaGetPixmapDriverPrivate(pWinPixmap);
    }
    else if (pDrawable->type == DRAWABLE_PIXMAP)
    {
        pScreen = screenInfo.screens[stuff->screen];
        ppriv = (Viv2DPixmapPtr)exaGetPixmapDriverPrivate((PixmapPtr)pDrawable);
    }

    if (ppriv)
    {
        if(stuff->flag & VIVPIXMAP_FLAG_SHARED_CLIENTWRITE_SERVERREAD)
            ppriv->mFlags |= VIVPIXMAP_FLAG_NONCACHEABLE;
        if(ppriv->mFlags & VIVPIXMAP_FLAG_NONCACHEABLE)
        {
            // turn this  Pixmap to non-cacheable
            VivPtr pViv = VIVPTR_FROM_SCREEN(pScreen);
            if(pViv)
            {
                VIVGPUPtr gpuctx = (VIVGPUPtr) pViv->mGrCtx.mGpu;
                VIV2DGPUSurfaceReAllocNonCached(gpuctx, ppriv);
            }
        }
        return  0;
    }
    else
    {
        return -1;
    }
}

#ifdef COMPOSITE
#else
static Bool VIVGetParentScreenXY(ScreenPtr pScreen, WindowPtr pWin, int *screenX, int *screenY)
{

	Viv2DPixmapPtr ppriv;
	WindowPtr pParentW = NULL;
	WindowPtr preParentW = NULL;
	GenericSurfacePtr surf = NULL;
	GenericSurfacePtr parentsurf = NULL;
	PixmapPtr      pWinPixmap;
	pWinPixmap = pScreen->GetWindowPixmap(pWin);

	ppriv = (Viv2DPixmapPtr)exaGetPixmapDriverPrivate(pWinPixmap);
	if ( ppriv == NULL )
	{
		*screenX = (int )pWin->drawable.x;
		*screenY = (int )pWin->drawable.y;
		return FALSE;
	}
	surf = (GenericSurfacePtr) (ppriv->mVidMemInfo);

	pParentW = pWin->parent;
	preParentW = pWin;
	while ( pParentW ){
		pWinPixmap = pScreen->GetWindowPixmap(pParentW);
		ppriv = (Viv2DPixmapPtr)exaGetPixmapDriverPrivate(pWinPixmap);

		if (ppriv)
			parentsurf = (GenericSurfacePtr) (ppriv->mVidMemInfo);
		else
			parentsurf = NULL;

		if ( parentsurf == surf )
		{
			preParentW = pParentW;
			pParentW = pParentW->parent;
			continue;
		}
		break;
	}

	if ( preParentW )
	{
		*screenX = (int )preParentW->drawable.x;
		*screenY = (int )preParentW->drawable.y;
	} else {
		return FALSE;
	}

	return TRUE;
}
#endif

static Bool
VIVEXTDrawableInfo(ScreenPtr pScreen,
	DrawablePtr pDrawable,
	int *X,
	int *Y,
	int *W,
	int *H,
	int *numClipRects,
	drm_clip_rect_t ** pClipRects,
	int *relX,
	int *relY,
#if GPU_VERSION_GREATER_THAN(5, 0, 9, 17083)
	unsigned int *nodeName,
#else
	unsigned int *backNode,
#endif
	unsigned int *phyAddress,
	unsigned int *alignedWidth,
	unsigned int *alignedHeight,
	unsigned int *stride)
{

	WindowPtr		pWin;
	PixmapPtr      pWinPixmap;
	Viv2DPixmapPtr ppriv;
	GenericSurfacePtr surf = NULL;

#if GPU_VERSION_GREATER_THAN(5, 0, 9, 17083)
	*nodeName = 0;
#else
	*backNode = 0;
#endif

	if (pDrawable->type == DRAWABLE_WINDOW) {

		pWin = (WindowPtr)pDrawable;
		*X = (int)(pWin->drawable.x);
		*Y = (int)(pWin->drawable.y);
		*W = (int)(pWin->drawable.width);
		*H = (int)(pWin->drawable.height);
		*numClipRects = RegionNumRects(&pWin->clipList);
		*pClipRects = (drm_clip_rect_t *)RegionRects(&pWin->clipList);

		pWinPixmap = pScreen->GetWindowPixmap(pWin);
		*alignedWidth = gcmALIGN(pWinPixmap->drawable.width, WIDTH_ALIGNMENT);
		*alignedHeight = gcmALIGN(pWinPixmap->drawable.height, HEIGHT_ALIGNMENT);

		#ifdef COMPOSITE
		*relX = *X - pWinPixmap->screen_x;
		*relY = *Y - pWinPixmap->screen_y;
		#else
		*relX = *X;
		*relY = *Y;
		#endif

		ppriv = (Viv2DPixmapPtr)exaGetPixmapDriverPrivate(pWinPixmap);
		*phyAddress = 0;
		*stride = 0;
		if (ppriv) {
			surf = (GenericSurfacePtr) (ppriv->mVidMemInfo);
			if (surf) {
#if GPU_VERSION_GREATER_THAN(5, 0, 9, 17083)
				if (surf->mVideoNode.mNode)
				    gcoHAL_NameVideoMemory(surf->mVideoNode.mNode, (gctUINT32 *)nodeName);
#else
				*backNode = (unsigned int)surf->mVideoNode.mNode;
#endif
				*phyAddress = (unsigned int)surf->mVideoNode.mPhysicalAddr;
				*stride = surf->mStride;
			}
		}

	} else {

		*relX = 0;
		*relY = 0;
		/* pixmap (or for GLX 1.3, a PBuffer) */
		pWinPixmap = (PixmapPtr)pDrawable;

		*X = (int)(pDrawable->x);
		*Y = (int)(pDrawable->y);
		*W = (int)(pDrawable->width);
		*H = (int)(pDrawable->height);

		if (pDrawable->type == DRAWABLE_PIXMAP) {
			Viv2DPixmapPtr ppriv = (Viv2DPixmapPtr)exaGetPixmapDriverPrivate((PixmapPtr)pDrawable);
			GenericSurfacePtr surf = (GenericSurfacePtr) (ppriv->mVidMemInfo);
			*alignedWidth = gcmALIGN(pWinPixmap->drawable.width, WIDTH_ALIGNMENT);
			*alignedHeight = gcmALIGN(pWinPixmap->drawable.height, HEIGHT_ALIGNMENT);
			if (surf) {
#if GPU_VERSION_GREATER_THAN(5, 0, 9, 17083)
				if (surf->mVideoNode.mNode)
				    gcoHAL_NameVideoMemory(surf->mVideoNode.mNode, (gctUINT32 *)nodeName);
#else
				*backNode = (unsigned int)surf->mVideoNode.mNode;
#endif
				*phyAddress = (unsigned int)surf->mVideoNode.mPhysicalAddr;
				*stride = surf->mStride;
			} else {
				*phyAddress = 0;
				*stride = 0;
			}
			*numClipRects = 0;
			*pClipRects = 0;
			return TRUE;
		} else {
			*alignedWidth = 0;
			*alignedHeight = 0;
			*phyAddress = 0;
			*stride = 0;
			return FALSE;
		}

	}

	return TRUE;


}

static void ClippedRects(ScreenPtr pScreen,
			PixmapPtr pWinPixmap,
			#ifndef COMPOSITE
			int screenX,
			int screenY,
			#endif
			drm_clip_rect_t *psrcrects,
			drm_clip_rect_t *prects,
			int *numrects)
{
	if (psrcrects && prects) {

		int i, j;

		for (i = 0, j = 0; i < *numrects; i++) {

				#ifdef COMPOSITE
				prects[j].x1 = psrcrects[i].x1 - pWinPixmap->screen_x;
				prects[j].y1 = psrcrects[i].y1 - pWinPixmap->screen_y;
				prects[j].x2 = psrcrects[i].x2 - pWinPixmap->screen_x;
				prects[j].y2 = psrcrects[i].y2 - pWinPixmap->screen_y;
				#else
				prects[j].x1 = psrcrects[i].x1 - screenX;
				prects[j].y1 = psrcrects[i].y1 - screenY;
				prects[j].x2 = psrcrects[i].x2 - screenX;
				prects[j].y2 = psrcrects[i].y2 - screenY;
				#endif

				if (prects[j].x1 < prects[j].x2 &&
				prects[j].y1 < prects[j].y2) {
					j++;
				}
			}

			*numrects = j;
	} else {
			*numrects = 0;
	}

}

static int
ProcVIVEXTDrawableInfo(register ClientPtr client)
{
	WindowPtr		pWin;
	PixmapPtr		pWinPixmap = NULL;
	xVIVEXTDrawableInfoReply rep = {
		.type = X_Reply,
		.sequenceNumber = client->sequence,
		.length = 0
	};
	DrawablePtr pDrawable;
	int X=0, Y=0, W=0, H=0;
	drm_clip_rect_t *pClipRects, *pClippedRects;
	int relX, relY, rc;
	#ifndef COMPOSITE
	int screenX;
	int screenY;
    #endif


	REQUEST(xVIVEXTDrawableInfoReq);
	REQUEST_SIZE_MATCH(xVIVEXTDrawableInfoReq);


	if (stuff->screen >= screenInfo.numScreens) {
		client->errorValue = stuff->screen;
		return BadValue;
	}

	rc = dixLookupDrawable(&pDrawable, stuff->drawable, client, 0,
		DixReadAccess);
	if (rc != Success)
	return rc;

	if (!VIVEXTDrawableInfo(screenInfo.screens[stuff->screen],
		pDrawable,
		(int *) &X,
		(int *) &Y,
		(int *) &W,
		(int *) &H,
		(int *) &rep.numClipRects,
		&pClipRects,
		&relX,
		&relY,
#if GPU_VERSION_GREATER_THAN(5, 0, 9, 17083)
		(unsigned int *)&rep.nodeName,
#else
		(unsigned int *)&rep.backNode,
#endif
		(unsigned int *)&rep.phyAddress,
		(unsigned int *)&rep.alignedWidth,
		(unsigned int *)&rep.alignedHeight,
		(unsigned int *)&rep.stride)) {
		return BadValue;
	}

	ScreenPtr pScreen = screenInfo.screens[stuff->screen];
	pWin = NULL;
	if (pDrawable->type == DRAWABLE_WINDOW) {
		pWin = (WindowPtr)pDrawable;
		pWinPixmap = pScreen->GetWindowPixmap(pWin);
	}

	if (pDrawable->type == DRAWABLE_PIXMAP) {
		pWinPixmap = (PixmapPtr)pDrawable;
	}

	rep.drawableX = X;
	rep.drawableY = Y;
	rep.drawableWidth = W;
	rep.drawableHeight = H;
	rep.length = (SIZEOF(xVIVEXTDrawableInfoReply) - SIZEOF(xGenericReply));

	#ifdef COMPOSITE
	rep.relX = relX;
	rep.relY = relY;
	#else
	if (pWin){
		if (VIVGetParentScreenXY(pScreen, pWin, &screenX, &screenY) == FALSE)
			return BadValue;
	}

	rep.relX = relX - screenX;
	rep.relY = relY - screenY;
	#endif

	pClippedRects = pClipRects;

	if (rep.numClipRects) {
		/* Clip cliprects to screen dimensions (redirected windows) */
		pClippedRects = malloc(rep.numClipRects * sizeof(drm_clip_rect_t));

		#ifdef COMPOSITE
		ClippedRects(pScreen, pWinPixmap, pClipRects, pClippedRects, (int *)&(rep.numClipRects));
		#else
		ClippedRects(pScreen, pWinPixmap,screenX, screenY, pClipRects, pClippedRects, (int *)&(rep.numClipRects));
		#endif

		rep.length += sizeof(drm_clip_rect_t) * rep.numClipRects;
	}

	rep.length = bytes_to_int32(rep.length);

	WriteToClient(client, sizeof(xVIVEXTDrawableInfoReply), &rep);

	if (rep.numClipRects) {
	WriteToClient(client,
	sizeof(drm_clip_rect_t) * rep.numClipRects,
	pClippedRects);
	}

    if(pClippedRects && pClippedRects != pClipRects)
        free(pClippedRects);

	return Success;

}

static Bool VIVFULLScreenCovered(ScreenPtr pScreen, WindowPtr pWin)
{
	WindowPtr preWin;
	WindowPtr pNext;
	PixmapPtr      pWinPixmap;
	Viv2DPixmapPtr ppriv;
	GenericSurfacePtr surf = NULL;
	int numClipRects = 0;
	drm_clip_rect_t *pClipRects = NULL;

	if ( pWin == NULL )
		return TRUE;

	pWinPixmap = pScreen->GetWindowPixmap(pWin);
	ppriv = (Viv2DPixmapPtr)exaGetPixmapDriverPrivate(pWinPixmap);

	if (ppriv) {
		surf = (GenericSurfacePtr) (ppriv->mVidMemInfo);
		if (surf) {
			if ( surf->mVideoNode.mNode == 0 )
			{
				numClipRects = RegionNumRects(&pWin->clipList);
				pClipRects = (drm_clip_rect_t *)RegionRects(&pWin->clipList);
				if ( numClipRects != 1)
					return TRUE;
				if ( (pClipRects[0].x2 - pClipRects[0].x1) != pWin->drawable.width
					|| (pClipRects[0].y2 - pClipRects[0].y1) != pWin->drawable.height )
				return TRUE;

				return FALSE;
			}
 		} else {
 			return TRUE;
 		}
	} else {
		return TRUE;
	}

	if ( pWin->parent == NULL )
		return TRUE;

	if ( pWin->firstChild )
	{
		return TRUE;
	}


	numClipRects = RegionNumRects(&pWin->clipList);
	pClipRects = (drm_clip_rect_t *)RegionRects(&pWin->clipList);

	/* if not 1 and size is not equal to Win size, his sibs cover this win */
	if ( numClipRects != 1)
		return TRUE;
	if ( (pClipRects[0].x2 - pClipRects[0].x1) != pWin->drawable.width
		|| (pClipRects[0].y2 - pClipRects[0].y1) != pWin->drawable.height )
		return TRUE;

	preWin = pWin;
	while( preWin )
	{
		if ( preWin->parent )
		{
			pNext = preWin->parent->firstChild;
			while( pNext )
			{
				if ( pNext->mapped && pNext->visibility != VisibilityNotViewable && preWin->redirectDraw == pNext->redirectDraw )
					break;

				pNext = pNext->nextSib;
			}

			if ( preWin == pNext )
			{
				preWin = preWin->parent;
			}
			else
				return TRUE;
		} else {
			break;
		}
	}
	return FALSE;
}

static int
ProcVIVEXTFULLScreenInfo(register ClientPtr client)
{
	DrawablePtr pDrawable;
	Bool isCovered = FALSE;
	int rc;

	xVIVEXTFULLScreenInfoReply rep = {
		.type = X_Reply,
		.sequenceNumber = client->sequence,
		.length = 0
	};

	REQUEST(xVIVEXTFULLScreenInfoReq);
	REQUEST_SIZE_MATCH(xVIVEXTFULLScreenInfoReq);


	if (stuff->screen >= screenInfo.numScreens) {
		client->errorValue = stuff->screen;
		return BadValue;
	}



	rc = dixLookupDrawable(&pDrawable, stuff->drawable, client, 0,
		DixReadAccess);
	if (rc != Success)
	return rc;


	ScreenPtr pScreen = screenInfo.screens[stuff->screen];

	if (pDrawable->type == DRAWABLE_WINDOW) {
		isCovered = VIVFULLScreenCovered(pScreen, (WindowPtr)pDrawable);
	}


	rep.fullscreenCovered = (CARD32)isCovered;



	WriteToClient(client, sizeof(xVIVEXTFULLScreenInfoReply), (char *)&rep);


	return Success;
}

static int
ProcVIVEXTPixmapPhysaddr(register ClientPtr client)
{

#if XORG_VERSION_CURRENT > XORG_VERSION_NUMERIC(1,12,0,0,0)
#else
	int n;
#endif

	REQUEST(xVIVEXTPixmapPhysaddrReq);
	REQUEST_SIZE_MATCH(xVIVEXTPixmapPhysaddrReq);

	/* Initialize reply */
	xVIVEXTPixmapPhysaddrReply rep;
	rep.type = X_Reply;
	rep.sequenceNumber = client->sequence;
	rep.length = 0;
	rep.pixmapState = VIV_PixmapUndefined;
	rep.PixmapPhysaddr = (CARD32)NULL;
	rep.pixmapStride = 0;

	/* Find the pixmap */
	PixmapPtr pPixmap;
	int rc = dixLookupResourceByType((pointer*)&pPixmap, stuff->pixmap, RT_PIXMAP, client,
					DixGetAttrAccess);
	if (Success == rc)
	{

		Viv2DPixmapPtr ppriv = (Viv2DPixmapPtr)exaGetPixmapDriverPrivate(pPixmap);
		GenericSurfacePtr surf = (GenericSurfacePtr) (ppriv->mVidMemInfo);
		if (surf) {

			rep.pixmapState = VIV_PixmapFramebuffer;
			rep.PixmapPhysaddr = (CARD32) surf->mVideoNode.mPhysicalAddr;
			rep.pixmapStride = surf->mStride;
		} else {
			rep.pixmapState = VIV_PixmapOther;
		}
	}

	/* Check if any reply values need byte swapping */
	if (client->swapped)
	{
#if XORG_VERSION_CURRENT > XORG_VERSION_NUMERIC(1,12,0,0,0)
		swaps(&rep.sequenceNumber);
		swapl(&rep.length);
		swapl(&rep.PixmapPhysaddr);
		swapl(&rep.pixmapStride);
#else
		swaps(&rep.sequenceNumber, n);
		swapl(&rep.length, n);
		swapl(&rep.PixmapPhysaddr, n);
		swapl(&rep.pixmapStride, n);
#endif
	}

	/* Reply to client */
	WriteToClient(client, sizeof(rep), (char*)&rep);
	return client->noClientException;

}

static int
ProcVIVEXTPixmapSync(register ClientPtr client)
{
    PixmapPtr pPixmap;
    int rc;

    REQUEST(xVIVEXTPixmapSyncReq);
    REQUEST_SIZE_MATCH(xVIVEXTPixmapSyncReq);

    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    /* Find the pixmap */
    rc = dixLookupResourceByType((pointer*)&pPixmap, stuff->pixmap, RT_PIXMAP, client,
                            DixGetAttrAccess);
    if (Success == rc)
    {
        ScreenPtr pScreen = screenInfo.screens[stuff->screen];
        VivPtr pViv = VIVPTR_FROM_SCREEN(pScreen);
        if(pViv)
        {
            Viv2DPixmapPtr ppriv = (Viv2DPixmapPtr)exaGetPixmapDriverPrivate(pPixmap);
            preCpuDraw(pViv, ppriv);
            return 0;
        }
    }

    return -1;
}

static int
ProcVIVEXTRefreshVideoModes(register ClientPtr client)
{
    int rc;
    char suggestMode[128];
    xVIVEXTRefreshVideoModesReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .preferModeLen = 0
    };

    suggestMode[0] = 0;

    REQUEST(xVIVEXTRefreshVideoModesReq);
    REQUEST_SIZE_MATCH(xVIVEXTRefreshVideoModesReq);


    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    ScreenPtr pScreen = screenInfo.screens[stuff->screen];
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    rc = imxRefreshModes(pScrn, stuff->fb, suggestMode);

    if(rc != 0) {
        return BadValue;
    }

    int extraLen = strlen(suggestMode);

    rep.length = bytes_to_int32(extraLen);
    rep.preferModeLen = extraLen;

    extraLen = rep.length << 2; // with padding

    WriteToClient(client, sizeof(xVIVEXTRefreshVideoModesReply), (char *)&rep);
    if(extraLen > 0)
        WriteToClient(client, extraLen, suggestMode);

    return Success;
}

extern Bool FbDoFlip(ScreenPtr pScreen, int restore);
extern unsigned int GetExaSettings();

static int
ProcVIVEXTDisplayFlip(register ClientPtr client)
{
    REQUEST(xVIVEXTDisplayFlipReq);
    REQUEST_SIZE_MATCH(xVIVEXTDisplayFlipReq);

    ScreenPtr pScreen = screenInfo.screens[stuff->screen];

    if(!FbDoFlip(pScreen, stuff->restore))
        return BadRequest;

    return Success;
}

static int
ProcVIVEXTGetExaSettings(register ClientPtr client)
{
    xVIVEXTGetExaSettingsReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .flags = 0
    };

    REQUEST(xVIVEXTGetExaSettingsReq);
    REQUEST_SIZE_MATCH(xVIVEXTGetExaSettingsReq);


    if (stuff->screen >= screenInfo.numScreens) {
        client->errorValue = stuff->screen;
        return BadValue;
    }

    rep.flags = GetExaSettings();

    WriteToClient(client, sizeof(xVIVEXTGetExaSettingsReply), (char *)&rep);

    return Success;
}

static int
ProcVIVEXTDispatch(register ClientPtr client)
{
	REQUEST(xReq);

	switch (stuff->data) {
		case X_VIVEXTDrawableFlush:
			return ProcVIVEXTDrawableFlush(client);
		case X_VIVEXTDrawableInfo:
			return ProcVIVEXTDrawableInfo(client);
		case X_VIVEXTFULLScreenInfo:
			return ProcVIVEXTFULLScreenInfo(client);
		case X_VIVEXTPixmapPhysaddr:
			return ProcVIVEXTPixmapPhysaddr(client);
		case X_VIVEXTDrawableSetFlag:
			return ProcVIVEXTDrawableSetFlag(client);
		case X_VIVEXTPixmapSync:
			return ProcVIVEXTPixmapSync(client);
		case X_VIVEXTRefreshVideoModes:
			return ProcVIVEXTRefreshVideoModes(client);
		case X_VIVEXTDisplayFlip:
			return ProcVIVEXTDisplayFlip(client);
		case X_VIVEXTGetExaSettings:
			return ProcVIVEXTGetExaSettings(client);
		default:
			return BadRequest;
	}

}

static int
ProcVIVEXTQueryVersion(
	register ClientPtr client
)
{
	xVIVEXTQueryVersionReply rep;
#if XORG_VERSION_CURRENT > XORG_VERSION_NUMERIC(1,12,0,0,0)
#else
	register int n;
#endif


	REQUEST_SIZE_MATCH(xVIVEXTQueryVersionReq);
	rep.type = X_Reply;
	rep.length = 0;
	rep.sequenceNumber = client->sequence;
	rep.majorVersion = VIVEXT_MAJOR_VERSION;
	rep.minorVersion = VIVEXT_MINOR_VERSION;
	rep.patchVersion = VIVEXT_PATCH_VERSION;

	if (client->swapped) {
#if XORG_VERSION_CURRENT > XORG_VERSION_NUMERIC(1,12,0,0,0)
		swaps(&rep.sequenceNumber);
		swapl(&rep.length);
		swaps(&rep.majorVersion);
		swaps(&rep.minorVersion);
		swapl(&rep.patchVersion);
#else
		swaps(&rep.sequenceNumber, n);
		swapl(&rep.length, n);
		swaps(&rep.majorVersion, n);
		swaps(&rep.minorVersion, n);
		swapl(&rep.patchVersion, n);
#endif
	}

	WriteToClient(client, sizeof(xVIVEXTQueryVersionReply), (char *)&rep);

	return Success;
}

static int
SProcVIVEXTQueryVersion(
	register ClientPtr	client
)
{

#if XORG_VERSION_CURRENT > XORG_VERSION_NUMERIC(1,12,0,0,0)
#else
	register int n;
#endif

	REQUEST(xVIVEXTQueryVersionReq);
#if XORG_VERSION_CURRENT > XORG_VERSION_NUMERIC(1,12,0,0,0)
	swaps(&stuff->length);
#else
	swaps(&stuff->length, n);
#endif
	return ProcVIVEXTQueryVersion(client);
}

static int
SProcVIVEXTDispatch (
	register ClientPtr	client
)
{
	REQUEST(xReq);
	/*
	* Only local clients are allowed vivhelp access, but remote clients still need
	* these requests to find out cleanly.
	*/
	switch (stuff->data)
	{
		case X_VIVEXTQueryVersion:
			return SProcVIVEXTQueryVersion(client);
		default:
			return VIVEXTErrorBase + VIVEXTClientNotLocal;
	}
}

/*ARGSUSED*/
static void
VIVEXTResetProc (
	ExtensionEntry* extEntry
)
{

}

void
VIVExtensionInit(void)
{
	ExtensionEntry *extEntry;

	extEntry = AddExtension(VIVEXTNAME,
				VIVEXTNumberEvents,
				VIVEXTNumberErrors,
				ProcVIVEXTDispatch,
				SProcVIVEXTDispatch,
				VIVEXTResetProc, StandardMinorOpcode);

	VIVEXTReqCode = (unsigned char) extEntry->base;
	VIVEXTErrorBase = extEntry->errorBase;

}
