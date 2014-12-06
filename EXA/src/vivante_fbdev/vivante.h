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


#ifndef VIVANTE_H
#define    VIVANTE_H

#ifdef __cplusplus
extern "C" {
#endif

    /*GAL*/
#include "vivante_gal.h"

#define VIV_MAX_WIDTH   (1 <<11)
#define VIV_MAX_HEIGHT (1 <<11)
#define PIXMAP_PITCH_ALIGN    (WIDTH_ALIGNMENT*4)

#if defined(GPU_NO_OVERLAP_BLIT)
#define SLICE_WIDTH 1920
#define SLICE_HEIGHT 256
#define BLIT_TILE_HEIGHT 16
#endif

    /********************************************************************************
     *  Rectangle Structs (START)
     ********************************************************************************/
    typedef struct _vivFakeExa {
        ExaDriverPtr mExaDriver;
        /*Feature Switches  For Exa*/
        Bool mNoAccelFlag;
        Bool mUseExaFlag;
        Bool mIsInited;
        /*Fake EXA Operations*/
        int op;
        PicturePtr pSrcPicture, pMaskPicture, pDstPicture;
        PixmapPtr pDst, pSrc, pMask;
        GCPtr pGC;
        CARD32 copytmpval[2];
        CARD32 solidtmpval[3];
    } VivFakeExa, *VivFakeExaPtr;

    typedef struct _fbInfo {
        void * mMappingInfo;
        unsigned long memPhysBase;
        unsigned char* mFBStart; /*logical memory start address*/
        unsigned char* mFBMemory; /*memory  address*/
        unsigned long memGpuBase; /*address in gpu-2D space (gpu address space: 2D is same as 3D, but different with VG355)*/
        int mFBOffset; /*framebuffer offset*/
    } FBINFO, *FBINFOPTR;

    typedef struct _fbSyncFlags {
        char * modeName;
        unsigned int syncFlags;
    } FBSYNCFLAGS;

#define MAX_MODES_SUPPORTED 256
    typedef struct _vivRec {
        /*Graphics Context*/
        GALINFO mGrCtx;
        ScreenPtr pScreen;
        /*FBINFO*/
        FBINFO mFB;
        /*EXA STUFF*/
        VivFakeExa mFakeExa;
        /*Entity & Options*/
        EntityInfoPtr pEnt; /*Entity To Be Used with this driver*/
        OptionInfoPtr Options; /*Options to be parsed in xorg.conf*/
        /*Funct Pointers*/
        CloseScreenProcPtr CloseScreen; /*Screen Close Function*/
        CreateScreenResourcesProcPtr CreateScreenResources;

        /* DRI information */
        void * pDRIInfo;
        int drmSubFD;

        /* ---- from fb ----*/
        int         lineLength;
        int         rotate;
        Bool        shadowFB;
        void       *shadow;
        void      (*PointerMoved)(SCRN_ARG_TYPE arg, int x, int y);

        /* DGA info */
        DGAModePtr  pDGAMode;
        int         nDGAMode;

        /* ---- imx display section ----*/

        char    fbId[80];
        char    fbDeviceName[32];

        /* size of FB memory mapped; includes offset for page align */
        int    fbMemorySize;

        /* virtual addr for start 2nd FB memory for XRandR rotation */
        unsigned char*    fbMemoryStart2;

        /* total bytes FB memory to reserve for screen(s) */
        int    fbMemoryScreenReserve;

        /* frame buffer alignment properties */
        int    fbAlignOffset;
        int    fbAlignWidth;
        int    fbAlignHeight;

        /* Driver phase/state information */
        Bool suspended;

        void* displayPrivate;

        /* sync value: support FSL extension */
        FBSYNCFLAGS fbSync[MAX_MODES_SUPPORTED];
        char  bootupVideoMode[64];
        DisplayModePtr  lastVideoMode;
    } VivRec, * VivPtr;

    /********************************************************************************
     *  Rectangle Structs (END)
     ********************************************************************************/
#define GET_VIV_PTR(p) ((VivPtr)((p)->driverPrivate))

#define VIVPTR_FROM_PIXMAP(x)        \
        GET_VIV_PTR(xf86ScreenToScrn((x)->drawable.pScreen))
#define VIVPTR_FROM_SCREEN(x)        \
        GET_VIV_PTR(xf86ScreenToScrn((x)))
#define VIVPTR_FROM_PICTURE(x)    \
        GET_VIV_PTR(xf86ScreenToScrn((x)->pDrawable->pScreen))

    /********************************************************************************
     *
     *  Macros For Access (END)
     *
     ********************************************************************************/

#ifdef __cplusplus
}
#endif

#endif    /* VIVANTE_H */

