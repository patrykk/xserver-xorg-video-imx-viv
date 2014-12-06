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


#include "vivante_exa.h"
#include "vivante.h"

Bool
DummyPrepareSolid(PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fg) {
    return FALSE;
}

// FIXME! thresould is calculated on 24-bit pixmap
#define MIN_HW_HEIGHT 64
#define MIN_HW_SIZE_24BIT (300 * 300)

/**
 * PrepareSolid() sets up the driver for doing a solid fill.
 * @param pPixmap Destination pixmap
 * @param alu raster operation
 * @param planemask write mask for the fill
 * @param fg "foreground" color for the fill
 *
 * This call should set up the driver for doing a series of solid fills
 * through the Solid() call.  The alu raster op is one of the GX*
 * graphics functions listed in X.h, and typically maps to a similar
 * single-byte "ROP" setting in all hardware.  The planemask controls
 * which bits of the destination should be affected, and will only represent
 * the bits up to the depth of pPixmap.  The fg is the pixel value of the
 * foreground color referred to in ROP descriptions.
 *
 * Note that many drivers will need to store some of the data in the driver
 * private record, for sending to the hardware with each drawing command.
 *
 * The PrepareSolid() call is required of all drivers, but it may fail for any
 * reason.  Failure results in a fallback to software rendering.
 */
Bool
VivPrepareSolid(PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fg) {
    TRACE_ENTER();
    Viv2DPixmapPtr pdst = exaGetPixmapDriverPrivate(pPixmap);
    VivPtr pViv = VIVPTR_FROM_PIXMAP(pPixmap);
    int fgop = 0xF0;
    int bgop = 0xF0;

    // early fail out
     if(pPixmap->drawable.height < MIN_HW_HEIGHT || pPixmap->drawable.width * pPixmap->drawable.height < MIN_HW_SIZE_24BIT) {
        TRACE_EXIT(FALSE);
    }

    if (!CheckFILLValidity(pPixmap, alu, planemask)) {
        TRACE_EXIT(FALSE);
    }
    if (!GetDefaultFormat(pPixmap->drawable.bitsPerPixel, &(pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mFormat))) {
        TRACE_EXIT(FALSE);
    }

    /*Populating the information*/
    pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mHeight = pPixmap->drawable.height;
    pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mWidth = pPixmap->drawable.width;
    pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mStride = pPixmap->devKind;
    pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mPriv = pdst;
    pViv->mGrCtx.mBlitInfo.mFgRop = fgop;
    pViv->mGrCtx.mBlitInfo.mBgRop = bgop;
    pViv->mGrCtx.mBlitInfo.mColorARGB32 = fg;
    pViv->mGrCtx.mBlitInfo.mColorConvert = FALSE;
    pViv->mGrCtx.mBlitInfo.mPlaneMask = planemask;
    pViv->mGrCtx.mBlitInfo.mOperationCode = VIVSOLID;

    TRACE_EXIT(TRUE);
}

/**
 * Solid() performs a solid fill set up in the last PrepareSolid() call.
 *
 * @param pPixmap destination pixmap
 * @param x1 left coordinate
 * @param y1 top coordinate
 * @param x2 right coordinate
 * @param y2 bottom coordinate
 *
 * Performs the fill set up by the last PrepareSolid() call, covering the
 * area from (x1,y1) to (x2,y2) in pPixmap.  Note that the coordinates are
 * in the coordinate space of the destination pixmap, so the driver will
 * need to set up the hardware's offset and pitch for the destination
 * coordinates according to the pixmap's offset and pitch within
 * framebuffer.  This likely means using exaGetPixmapOffset() and
 * exaGetPixmapPitch().
 *
 * This call is required if PrepareSolid() ever succeeds.
 */
void
VivSolid(PixmapPtr pPixmap, int x1, int y1, int x2, int y2) {
    TRACE_ENTER();
    VivPtr pViv = VIVPTR_FROM_PIXMAP(pPixmap);
    Viv2DPixmapPtr pdst = exaGetPixmapDriverPrivate(pPixmap);

    /* when surface > IMX_EXA_NONCACHESURF_SIZE but actual solid size < IMX_EXA_NONCACHESURF_SIZE, go sw path */
     if(( y2 - y1 ) < MIN_HW_HEIGHT || (  x2 - x1 ) * ( y2 - y1 ) < MIN_HW_SIZE_24BIT) {
        preCpuDraw(pViv, pdst);

        /* mStride should be 4 aligned cause width is 8 aligned,Stride%4 !=0 shouldn't happen */
        gcmASSERT((pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mStride%4)==0);

        pixman_fill((uint32_t *) MapViv2DPixmap(pdst), pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mStride/4, pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mFormat.mBpp, x1, y1 , x2-x1, y2-y1, pViv->mGrCtx.mBlitInfo.mColorARGB32);

        postCpuDraw(pViv, pdst);
        TRACE_EXIT();
    }

    startDrawingSolid(x2-x1, y2-y1);

    /*Setting up the rectangle*/
    pViv->mGrCtx.mBlitInfo.mDstBox.x1 = x1;
    pViv->mGrCtx.mBlitInfo.mDstBox.y1 = y1;
    pViv->mGrCtx.mBlitInfo.mDstBox.x2 = x2;
    pViv->mGrCtx.mBlitInfo.mDstBox.y2 = y2;
    
    // sync with cpu cache
    preGpuDraw(pViv, pdst, FALSE);

    if (!SetDestinationSurface(&pViv->mGrCtx)) {
            TRACE_ERROR("Solid Blit Failed\n");
        goto quit;
    }

    if (!SetClipping(&pViv->mGrCtx)) {
            TRACE_ERROR("Solid Blit Failed\n");
        goto quit;
    }

    if (!SetSolidBrush(&pViv->mGrCtx)) {
            TRACE_ERROR("Solid Blit Failed\n");
        goto quit;
    }

    if (!DoSolidBlit(&pViv->mGrCtx)) {
        TRACE_ERROR("Solid Blit Failed\n");
        goto quit;
    }

    // put this pixmap into gpu queue
    queuePixmapToGpu(pdst);

quit:
    TRACE_EXIT();
}

/**
 * DoneSolid() finishes a set of solid fills.
 *
 * @param pPixmap destination pixmap.
 *
 * The DoneSolid() call is called at the end of a series of consecutive
 * Solid() calls following a successful PrepareSolid().  This allows drivers
 * to finish up emitting drawing commands that were buffered, or clean up
 * state from PrepareSolid().
 *
 * This call is required if PrepareSolid() ever succeeds.
 */
void
VivDoneSolid(PixmapPtr pPixmap) {
    TRACE_ENTER();

    VivPtr pViv = VIVPTR_FROM_PIXMAP(pPixmap);

    postGpuDraw(pViv);

    endDrawingSolid();

    TRACE_EXIT();
}
