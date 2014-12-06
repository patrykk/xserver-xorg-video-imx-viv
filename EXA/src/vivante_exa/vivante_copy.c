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
#include "vivante_priv.h"

Bool
DummyPrepareCopy(PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap,
    int xdir, int ydir, int alu, Pixel planemask) {
    return FALSE;
}

// FIXME! thresould is calculated on 24-bit pixmap
#define MIN_HW_HEIGHT 64
#define MIN_HW_SIZE_24BIT (400 * 120)

/**
 * PrepareCopy() sets up the driver for doing a copy within video
 * memory.
 *
 * @param pSrcPixmap source pixmap
 * @param pDstPixmap destination pixmap
 * @param dx X copy direction
 * @param dy Y copy direction
 * @param alu raster operation
 * @param planemask write mask for the fill
 *
 * This call should set up the driver for doing a series of copies from the
 * the pSrcPixmap to the pDstPixmap.  The dx flag will be positive if the
 * hardware should do the copy from the left to the right, and dy will be
 * positive if the copy should be done from the top to the bottom.  This
 * is to deal with self-overlapping copies when pSrcPixmap == pDstPixmap.
 * If your hardware can only support blits that are (left to right, top to
 * bottom) or (right to left, bottom to top), then you should set
 * #EXA_TWO_BITBLT_DIRECTIONS, and EXA will break down Copy operations to
 * ones that meet those requirements.  The alu raster op is one of the GX*
 * graphics functions listed in X.h, and typically maps to a similar
 * single-byte "ROP" setting in all hardware.  The planemask controls which
 * bits of the destination should be affected, and will only represent the
 * bits up to the depth of pPixmap.
 *
 * Note that many drivers will need to store some of the data in the driver
 * private record, for sending to the hardware with each drawing command.
 *
 * The PrepareCopy() call is required of all drivers, but it may fail for any
 * reason.  Failure results in a fallback to software rendering.
 */

Bool
VivPrepareCopy(PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap,
    int xdir, int ydir, int alu, Pixel planemask) {
    TRACE_ENTER();
    Viv2DPixmapPtr psrc = exaGetPixmapDriverPrivate(pSrcPixmap);
    Viv2DPixmapPtr pdst = exaGetPixmapDriverPrivate(pDstPixmap);
    VivPtr pViv = VIVPTR_FROM_PIXMAP(pDstPixmap);
    int fgop = 0xCC;
    int bgop = 0xCC;

    //SURF_SIZE_FOR_SW(pSrcPixmap->drawable.width, pSrcPixmap->drawable.height);
    //SURF_SIZE_FOR_SW(pDstPixmap->drawable.width, pDstPixmap->drawable.height);
    // early fail out
    // xdir: -1 for right to left; 1 for left to right
    // ydir: -1 for bottom to top; 1 for top to bottom
#if defined(GPU_NO_OVERLAP_BLIT)
    if(xdir != 1 || ydir != 1) {
        if(pViv->mGrCtx.mBlitInfo.mSrcSurfInfo.mFormat.mBpp == 16 && pViv->mGrCtx.mBlitInfo.mHelperRgb565Surf == NULL)
            TRACE_EXIT(FALSE);
        if(pViv->mGrCtx.mBlitInfo.mSrcSurfInfo.mFormat.mBpp == 32 && pViv->mGrCtx.mBlitInfo.mHelperRgba8888Surf == NULL)
            TRACE_EXIT(FALSE);
        // not support dimension exceeding SLICE_WIDTH
        if(pSrcPixmap->drawable.width >= SLICE_WIDTH && pDstPixmap->drawable.width >= SLICE_WIDTH)
            TRACE_EXIT(FALSE);
    }
#endif

    // FIXME! planemask? should be ALL_ONES

    if(pSrcPixmap->drawable.height < MIN_HW_HEIGHT || pSrcPixmap->drawable.width * pSrcPixmap->drawable.height < MIN_HW_SIZE_24BIT) {
        TRACE_EXIT(FALSE);
    }

    if(pDstPixmap->drawable.height < MIN_HW_HEIGHT || pDstPixmap->drawable.width * pDstPixmap->drawable.height < MIN_HW_SIZE_24BIT) {
        TRACE_EXIT(FALSE);
    }

    if (!CheckCPYValidity(pDstPixmap, alu, planemask)) {
        TRACE_EXIT(FALSE);
    }

    if (!GetDefaultFormat(pSrcPixmap->drawable.bitsPerPixel, &(pViv->mGrCtx.mBlitInfo.mSrcSurfInfo.mFormat))) {
        TRACE_EXIT(FALSE);
    }

    if (!GetDefaultFormat(pDstPixmap->drawable.bitsPerPixel, &(pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mFormat))) {
        TRACE_EXIT(FALSE);
    }

    ConvertXAluToOPS(pDstPixmap, alu, planemask, &fgop,&bgop);

    pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mHeight = pDstPixmap->drawable.height;
    pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mWidth = pDstPixmap->drawable.width;
    pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mStride = pDstPixmap->devKind;
    pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mPriv = pdst;

    pViv->mGrCtx.mBlitInfo.mSrcSurfInfo.mHeight = pSrcPixmap->drawable.height;
    pViv->mGrCtx.mBlitInfo.mSrcSurfInfo.mWidth = pSrcPixmap->drawable.width;
    pViv->mGrCtx.mBlitInfo.mSrcSurfInfo.mStride = pSrcPixmap->devKind;
    pViv->mGrCtx.mBlitInfo.mSrcSurfInfo.mPriv = psrc;

#if defined(GPU_NO_OVERLAP_BLIT)
    pViv->mGrCtx.mBlitInfo.xdir = xdir;
    pViv->mGrCtx.mBlitInfo.ydir = ydir;
#endif

    pViv->mGrCtx.mBlitInfo.mBgRop = fgop;
    pViv->mGrCtx.mBlitInfo.mFgRop = bgop;

    if ( alu == GXcopy)
        pViv->mGrCtx.mBlitInfo.mOperationCode = VIVSIMCOPY;
    else
        pViv->mGrCtx.mBlitInfo.mOperationCode = VIVCOPY;

    TRACE_EXIT(TRUE);
}

/**
 * Copy() performs a copy set up in the last PrepareCopy call.
 *
 * @param pDstPixmap destination pixmap
 * @param srcX source X coordinate
 * @param srcY source Y coordinate
 * @param dstX destination X coordinate
 * @param dstY destination Y coordinate
 * @param width width of the rectangle to be copied
 * @param height height of the rectangle to be copied.
 *
 * Performs the copy set up by the last PrepareCopy() call, copying the
 * rectangle from (srcX, srcY) to (srcX + width, srcY + width) in the source
 * pixmap to the same-sized rectangle at (dstX, dstY) in the destination
 * pixmap.  Those rectangles may overlap in memory, if
 * pSrcPixmap == pDstPixmap.  Note that this call does not receive the
 * pSrcPixmap as an argument -- if it's needed in this function, it should
 * be stored in the driver private during PrepareCopy().  As with Solid(),
 * the coordinates are in the coordinate space of each pixmap, so the driver
 * will need to set up source and destination pitches and offsets from those
 * pixmaps, probably using exaGetPixmapOffset() and exaGetPixmapPitch().
 *
 * This call is required if PrepareCopy ever succeeds.
 *
**/
void
VivCopy(PixmapPtr pDstPixmap, int srcX, int srcY,
    int dstX, int dstY, int width, int height) {
    TRACE_ENTER();
    VivPtr pViv = VIVPTR_FROM_PIXMAP(pDstPixmap);

    Viv2DPixmapPtr psrc = NULL;
    Viv2DPixmapPtr pdst = NULL;
    int overlap = 1;

    startDrawingCopy(width, height, pViv->mGrCtx.mBlitInfo.mOperationCode);

    pdst = pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mPriv;
    psrc = pViv->mGrCtx.mBlitInfo.mSrcSurfInfo.mPriv;

    /* when surface > IMX_EXA_NONCACHESURF_SIZE but actual copy size < IMX_EXA_NONCACHESURF_SIZE, go sw path */
    if (( height < MIN_HW_HEIGHT || ( width * height ) < MIN_HW_SIZE_24BIT ) && pViv->mGrCtx.mBlitInfo.mOperationCode == GXcopy)
    {
        /* mStride should be 4 aligned cause width is 8 aligned,Stride%4 !=0 shouldn't happen */
        gcmASSERT((pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mStride%4)==0);
        gcmASSERT((pViv->mGrCtx.mBlitInfo.mSrcSurfInfo.mStride%4)==0);

        if ( MapViv2DPixmap(psrc) != MapViv2DPixmap(pdst) )
        {
            preCpuDraw(pViv, psrc);
            preCpuDraw(pViv, pdst);

            pixman_blt((uint32_t *) MapViv2DPixmap(psrc),
                (uint32_t *) MapViv2DPixmap(pdst),
                pViv->mGrCtx.mBlitInfo.mSrcSurfInfo.mStride/4,
                pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mStride/4,
                pViv->mGrCtx.mBlitInfo.mSrcSurfInfo.mFormat.mBpp,
                pViv->mGrCtx.mBlitInfo.mDstSurfInfo.mFormat.mBpp,
                srcX,
                srcY,
                dstX,
                dstY,
                width,
                height);

            postCpuDraw(pViv, psrc);
            postCpuDraw(pViv, pdst);
            TRACE_EXIT();
        }
    }

    /*Setting up the rectangle*/
    pViv->mGrCtx.mBlitInfo.mDstBox.x1 = dstX;
    pViv->mGrCtx.mBlitInfo.mDstBox.y1 = dstY;
    pViv->mGrCtx.mBlitInfo.mDstBox.x2 = dstX + width;
    pViv->mGrCtx.mBlitInfo.mDstBox.y2 = dstY + height;

    pViv->mGrCtx.mBlitInfo.mSrcBox.x1 = srcX;
    pViv->mGrCtx.mBlitInfo.mSrcBox.y1 = srcY;
    pViv->mGrCtx.mBlitInfo.mSrcBox.x2 = srcX + width;
    pViv->mGrCtx.mBlitInfo.mSrcBox.y2 = srcY + height;

#if defined(GPU_NO_OVERLAP_BLIT)
    if(pViv->mGrCtx.mBlitInfo.xdir != 1 || pViv->mGrCtx.mBlitInfo.ydir != 1) {
        // possible overlap copy

        if(srcX <= dstX + width || dstX <= srcX + width ||
           srcY <= dstY + height || dstY <= srcY + height)
            overlap = 0;
        /*
              |-------|
              |  S        |
              |      |-------|
              |      | D         |
               ----|            |
                      |            |
                      ---------
              (xdir = -1, ydir = -1 -> bottom up)

              |-------|
              |  S        |
              |-------|
              | D         |
              |            |
              |            |
              ---------
              (xdir = 1, ydir = -1 -> bottom up)

                           |-------|
                           |  S        |
                     |-------|    |
                     | D         |    |
                     |            |---
                     |            |
                      ---------
              (xdir = 1, ydir = -1 -> bottom up)

                     |-------|-- |
                     | D         |    |
                     |            |    |
                     |            |    |
                      -----------
                     (normal case)


              |-------|
              |  D        |
              |      |-------|
              |      | S         |
               ----|            |
                      |            |
                      ---------
                    (normal case)


              |-------|
              |  D        |
              |-------|
              | S         |
              |            |
              |            |
              ---------
                    (normal case)


                           |-------|
                           |  D        |
                     |-------|    |
                     | S         |    |
                     |            |---
                     |            |
                      --------
                    (xdir = -1, ydir = 1, if dy >= TILE_HEIGHT, normal case; else, down)


                     |-------|-- |
                     | S         |    |
                     |            |    |
                     |            |    |
                      -----------
                    (xdir = -1, ydir = 1 -> up/down)

        */
        if(overlap && pViv->mGrCtx.mBlitInfo.ydir == 1 && srcY - dstY >= BLIT_TILE_HEIGHT)
            overlap = 0;
    }
    else
        overlap = 0;
#else
    // dont care about overlap; hw can handle it
    overlap = 0;
#endif


    // sync with cpu cache
    preGpuDraw(pViv, psrc, TRUE);
    preGpuDraw(pViv, pdst, FALSE);

    if(!overlap) {
        if (!SetDestinationSurface(&pViv->mGrCtx)) {
            TRACE_ERROR("Copy Blit Failed\n");
            goto quit;
        }

        if (!SetSourceSurface(&pViv->mGrCtx)) {
            TRACE_ERROR("Copy Blit Failed\n");
            goto quit;
        }

        if (!SetClipping(&pViv->mGrCtx)) {
            TRACE_ERROR("Copy Blit Failed\n");
            goto quit;
        }

        if (!DoCopyBlit(&pViv->mGrCtx)) {
            TRACE_ERROR("Copy Blit Failed\n");
            goto quit;
        }
    }
    else {
#if defined(GPU_NO_OVERLAP_BLIT)
        gceSTATUS status = gcvSTATUS_OK;
        VIVGPUPtr gpuctx = (VIVGPUPtr) (pViv->mGrCtx.mGpu);
        VIV2DBLITINFOPTR pBlt = &(pViv->mGrCtx.mBlitInfo);
        gcoSURF surfTemp;
        int surfTempAlignedWidth;
        int surfTempAlignedHeight;
        int surfTempStride;
        int surfTempPhyAddr;
        int surfTempLogAddr;
        gcsRECT srcRect;
        gcsRECT dstRect;
        gcsRECT sliceRect;
        int yc = (height + SLICE_HEIGHT - 1) / SLICE_HEIGHT;

        if(pBlt->mSrcSurfInfo.mFormat.mBpp == 16)
            surfTemp = pBlt->mHelperRgb565Surf;
        else
            surfTemp = pBlt->mHelperRgba8888Surf;

        status = gcoSURF_GetAlignedSize(surfTemp, &surfTempAlignedWidth, &surfTempAlignedHeight, &surfTempStride);
        if (status != gcvSTATUS_OK) {
            TRACE_ERROR("gcoSURF_GetAlignedSize failed\n");
            goto quit;
        }

        status = gcoSURF_Lock(surfTemp,  &surfTempPhyAddr, (void *)&surfTempLogAddr);
        if (status != gcvSTATUS_OK) {
            TRACE_ERROR("gcoSURF_Lock failed\n");
            goto quit;
        }

        sliceRect.left = 0;
        sliceRect.top = 0;
        sliceRect.right = width;
        sliceRect.bottom = (height >= SLICE_HEIGHT ? SLICE_HEIGHT : height);

        srcRect.left = srcX;
        srcRect.right = srcRect.left + width;

        dstRect.left = dstX;
        dstRect.right = dstRect.left + width;

        if(pViv->mGrCtx.mBlitInfo.ydir == 1) {
            // top-down
            srcRect.top = srcY;
            dstRect.top = dstY;
            if(height >= SLICE_HEIGHT) {
                srcRect.bottom = srcRect.top + SLICE_HEIGHT;
                dstRect.bottom = dstRect.top + SLICE_HEIGHT;
            }
            else {
                srcRect.bottom = srcRect.top + height;
                dstRect.bottom = dstRect.top + height;
            }
        }
        else {
            // bottom-up
            srcRect.bottom = srcY + height;
            dstRect.bottom = dstY + height;
            if(height >= SLICE_HEIGHT) {
                srcRect.top = srcRect.bottom - SLICE_HEIGHT;
                dstRect.top = dstRect.bottom - SLICE_HEIGHT;
            }
            else {
                srcRect.top = srcRect.bottom - height;
                dstRect.top = dstRect.bottom - height;
            }
        }

        while(yc--) {
            GenericSurfacePtr surf;
            // set source
            surf = (GenericSurfacePtr) (pBlt->mSrcSurfInfo.mPriv->mVidMemInfo);
            status = gco2D_SetGenericSource
                    (
                    gpuctx->mDriver->m2DEngine,
                    &surf->mVideoNode.mPhysicalAddr,
                    1,
                    &surf->mStride,
                    1,
                    surf->mTiling,
                    pBlt->mSrcSurfInfo.mFormat.mVivFmt,
                    gcvSURF_0_DEGREE,
                    surf->mAlignedWidth,
                    surf->mAlignedHeight
                    );
            if (status != gcvSTATUS_OK) {
                TRACE_ERROR("gco2D_SetGenericSource failed\n");
                goto quit;
            }

            // set temp as dest
            status = gco2D_SetGenericTarget
                    (
                    gpuctx->mDriver->m2DEngine,
                    &surfTempPhyAddr,
                    1,
                    &surfTempStride,
                    1,
                    surf->mTiling, // use source surface tiling
                    pBlt->mSrcSurfInfo.mFormat.mVivFmt,
                    gcvSURF_0_DEGREE,
                    surfTempAlignedWidth,
                    surfTempAlignedHeight
                    );
            if (status != gcvSTATUS_OK) {
                TRACE_ERROR("gco2D_SetGenericTarget failed\n");
                goto quit;
            }

            // clip
            status = gco2D_SetClipping(gpuctx->mDriver->m2DEngine, &sliceRect);
            if (status != gcvSTATUS_OK) {
                TRACE_ERROR("gco2D_SetClipping failed\n");
                goto quit;
            }

            // blit this slice to helper surface
            status = gco2D_BatchBlit(
                    gpuctx->mDriver->m2DEngine,
                    1,
                    &srcRect,
                    &sliceRect,
                    0xCC, /* copy */
                    0xCC,
                    pBlt->mSrcSurfInfo.mFormat.mVivFmt
                    );

            if (status != gcvSTATUS_OK) {
                TRACE_ERROR("gco2D_BatchBlit failed\n");
                goto quit;
            }

            // set temp as src
            status = gco2D_SetGenericSource
                    (
                    gpuctx->mDriver->m2DEngine,
                    &surfTempPhyAddr,
                    1,
                    &surfTempStride,
                    1,
                    surf->mTiling, // use source surface tiling
                    pBlt->mSrcSurfInfo.mFormat.mVivFmt,
                    gcvSURF_0_DEGREE,
                    surfTempAlignedWidth,
                    surfTempAlignedHeight
                    );
            if (status != gcvSTATUS_OK) {
                TRACE_ERROR("gco2D_SetGenericSource failed\n");
                goto quit;
            }

            // set dest
            surf = (GenericSurfacePtr) (pBlt->mDstSurfInfo.mPriv->mVidMemInfo);
            status = gco2D_SetGenericTarget
                    (
                    gpuctx->mDriver->m2DEngine,
                    &surf->mVideoNode.mPhysicalAddr,
                    1,
                    &surf->mStride,
                    1,
                    surf->mTiling,
                    pBlt->mDstSurfInfo.mFormat.mVivFmt,
                    surf->mRotation,
                    surf->mAlignedWidth,
                    surf->mAlignedHeight
                    );
            if (status != gcvSTATUS_OK) {
                TRACE_ERROR("gco2D_SetGenericSource failed\n");
                goto quit;
            }

            // clip
            if (!SetClipping(&pViv->mGrCtx)) {
                TRACE_ERROR("Copy Blit Failed\n");
                goto quit;
            }

            // blit this slice to dest surface
            status = gco2D_BatchBlit(
                    gpuctx->mDriver->m2DEngine,
                    1,
                    &sliceRect,
                    &dstRect,
                    pBlt->mFgRop,
                    pBlt->mBgRop,
                    pBlt->mDstSurfInfo.mFormat.mVivFmt
                    );
            if (status != gcvSTATUS_OK) {
                TRACE_ERROR("gco2D_BatchBlit failed\n");
                goto quit;
            }

            // move rects
            if(pViv->mGrCtx.mBlitInfo.ydir == 1) {
                // top-down
                int delta;
                srcRect.top = srcRect.bottom;
                dstRect.top = dstRect.bottom;
                if(yc == 1)
                    delta = height % SLICE_HEIGHT;
                else
                    delta = SLICE_HEIGHT;
                srcRect.bottom = srcRect.top + delta;
                dstRect.bottom = dstRect.top + delta;
            }
            else {
                // bottom-up
                int delta;
                srcRect.bottom = srcRect.top;
                dstRect.bottom = dstRect.top;
                if(yc == 1)
                    delta = height % SLICE_HEIGHT;
                else
                    delta = SLICE_HEIGHT;
                srcRect.top = srcRect.bottom - delta;
                dstRect.top = dstRect.bottom - delta;
            }
            if(yc == 1)
                sliceRect.bottom = height % SLICE_HEIGHT;
        }

        status = gcoSURF_Unlock(surfTemp, &surfTempLogAddr);
#endif
    }

    queuePixmapToGpu(psrc);
    queuePixmapToGpu(pdst);

quit:
    TRACE_EXIT();
}

/**
 * DoneCopy() finishes a set of copies.
 *
 * @param pPixmap destination pixmap.
 *
 * The DoneCopy() call is called at the end of a series of consecutive
 * Copy() calls following a successful PrepareCopy().  This allows drivers
 * to finish up emitting drawing commands that were buffered, or clean up
 * state from PrepareCopy().
 *
 * This call is required if PrepareCopy() ever succeeds.
 */
void
VivDoneCopy(PixmapPtr pDstPixmap) {

    TRACE_ENTER();
    VivPtr pViv = VIVPTR_FROM_PIXMAP(pDstPixmap);

    postGpuDraw(pViv);

    endDrawingCopy();

    TRACE_EXIT();
}
