/****************************************************************************
*
*    Copyright (C) 2013 Freescale Semiconductor
*
*****************************************************************************/

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


#include "vivante_common.h"
#include "vivante.h"
#include "vivante_exa.h"
#include "vivante_ext.h"
#include "imx_display.h"
#include <errno.h>
#include <linux/fb.h>
#include <xorg/shmint.h>

#define USE_VIV_FB

#if defined(USE_VIV_FB)
static Bool gVivFb = TRUE;
static Bool gEnableXRandR = TRUE;
static Bool gEnableDRI = TRUE;
static Bool gEnableFbSyncExt = TRUE;
#else
static Bool gVivFb = FALSE;
static Bool gEnableXRandR = FALSE;
static Bool gEnableDRI = FALSE;
static Bool gEnableFbSyncExt = FALSE;
#endif

#define XV 1

#include "mipointer.h"

#ifdef XSERVER_LIBPCIACCESS
#include <pciaccess.h>
#endif

static Bool debug = 0;

#if defined(TRACE_ENTER)
#undef TRACE_ENTER
#endif
#if defined(TRACE_EXIT)
#undef TRACE_EXIT
#endif
#if defined(TRACE)
#undef TRACE
#endif
#if defined(TRACE_ERROR)
#undef TRACE_ERROR
#endif

#define TRACE_ENTER(str) \
    do { if (debug) ErrorF("fbdev: " str " %d\n",pScrn->scrnIndex); } while (0)
#define TRACE_EXIT(str) \
    do { if (debug) ErrorF("fbdev: " str " done\n"); } while (0)
#define TRACE(str) \
    do { if (debug) ErrorF("fbdev trace: " str "\n"); } while (0)
#define TRACE_ERROR(str) \
    do { if (1) ErrorF("fbdev trace: " str "\n"); } while (0)

/* -------------------------------------------------------------------- */
/* prototypes                                                           */

static const OptionInfoRec *FBDevAvailableOptions(int chipid, int busid);
static void	FBDevIdentify(int flags);
static Bool FBDevProbe(DriverPtr drv, int flags);
#ifdef XSERVER_LIBPCIACCESS
static Bool	FBDevPciProbe(DriverPtr drv, int entity_num,
     struct pci_device *dev, intptr_t match_data);
#endif
static Bool FBDevPreInit(ScrnInfoPtr pScrn, int flags);
static Bool FBDevScreenInit(SCREEN_INIT_ARGS_DECL);
static Bool FBDevCloseScreen(CLOSE_SCREEN_ARGS_DECL);
static void *	FBDevWindowLinear(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode,
				  CARD32 *size, void *closure);
static void	FBDevPointerMoved(SCRN_ARG_TYPE arg, int x, int y);
static Bool	FBDevDGAInit(ScrnInfoPtr pScrn, ScreenPtr pScreen);
static Bool FBDevDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op,
        pointer ptr);


enum { FBDEV_ROTATE_NONE=0, FBDEV_ROTATE_CW=270, FBDEV_ROTATE_UD=180, FBDEV_ROTATE_CCW=90 };

//-----------------------------------------------------------------------------------
// Freescale & Vivante implementation
//-----------------------------------------------------------------------------------
static Bool FBDevShadowInit(ScreenPtr pScreen);

static void FBDevFreeScreen(FREE_SCREEN_ARGS_DECL);
static Bool InitExaLayer(ScreenPtr pScreen);
static Bool DestroyExaLayer(ScreenPtr pScreen);
static void InitShmPixmap(ScreenPtr pScreen);
static Bool SaveBuildInModeSyncFlags(ScrnInfoPtr pScrn);
static Bool RestoreSyncFlags(ScrnInfoPtr pScrn);
static void CheckChipSet(ScrnInfoPtr pScrn);
static Bool tearingSetVideoBuffer(ScrnInfoPtr pScrn);
static Bool tearingWrapSurfaces(ScrnInfoPtr pScrn);

static Bool noVIVExtension;

static ExtensionModule VIVExt =
{
	VIVExtensionInit,
	VIVEXTNAME,
	&noVIVExtension
#ifndef XF86_SCRN_INTERFACE
	,
	NULL,
	NULL
#endif
};

Bool vivEnableCacheMemory = TRUE;
Bool vivEnableSyncDraw = FALSE;
Bool vivNoTearing = FALSE;

typedef VivRec FBDevRec;
typedef VivPtr FBDevPtr;

//-----------------------------------------------------------------------------------


/* -------------------------------------------------------------------- */

/*
 * This is intentionally screen-independent.  It indicates the binding
 * choice made in the first PreInit.
 */
static int pix24bpp = 0;

#define FBDEV_VERSION           1000
#define FBDEV_NAME              "VIVANTE"
#define FBDEV_DRIVER_NAME       "vivante"
#define PACKAGE_VERSION_MAJOR      1
#define PACKAGE_VERSION_MINOR      0
#define PACKAGE_VERSION_PATCHLEVEL 0

#ifdef XSERVER_LIBPCIACCESS
static const struct pci_id_match fbdev_device_match[] = {
    {
	PCI_MATCH_ANY, PCI_MATCH_ANY, PCI_MATCH_ANY, PCI_MATCH_ANY,
	0x00030000, 0x00ffffff, 0
    },

    { 0, 0, 0 },
};
#endif

_X_EXPORT DriverRec FBDEV = {
    FBDEV_VERSION,
    FBDEV_DRIVER_NAME,
#if 0
	"driver for linux framebuffer devices",
#endif
    FBDevIdentify,
    FBDevProbe,
    FBDevAvailableOptions,
    NULL,
    0,
    FBDevDriverFunc,

#ifdef XSERVER_LIBPCIACCESS
    fbdev_device_match,
    FBDevPciProbe
#endif
};

typedef enum _chipSetID {
    GC500_ID = 0x33,
    GC2100_ID,
    GCCORE_ID
} CHIPSETID;

/*CHIP NAMES*/
#define GCCORE_STR "VivanteGCCORE"
#define GC500_STR  "VivanteGC500"
#define GC2100_STR "VivanteGC2100"

/* Supported "chipsets" */
static SymTabRec FBDevChipsets[] = {
    {GC500_ID, GC500_STR},
    {GC2100_ID, GC2100_STR},
    {GCCORE_ID, GCCORE_STR},
    {-1, NULL}
};

/* Supported options */
typedef enum {
    OPTION_SHADOW_FB,
    OPTION_ROTATE,
    OPTION_FBDEV,
    OPTION_DEBUG,
    /* viv special */
    OPTION_VIV,
    OPTION_NOACCEL,
    OPTION_ACCELMETHOD,
    OPTION_SYNCDRAW,
    OPTION_VIVCACHEMEM,
    OPTION_NOTEARING
} FBDevOpts;

static const OptionInfoRec FBDevOptions[] = {
	{ OPTION_SHADOW_FB,	"ShadowFB",	OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_ROTATE,	"Rotate",	OPTV_STRING,	{0},	FALSE },
	{ OPTION_FBDEV,		"fbdev",	OPTV_STRING,	{0},	FALSE },
	{ OPTION_DEBUG,		"debug",	OPTV_BOOLEAN,	{0},	FALSE },
    { OPTION_VIV,		"vivante",	OPTV_STRING,	{0},	FALSE },
    { OPTION_NOACCEL,	"NoAccel",	OPTV_BOOLEAN,	{0},	FALSE },
    { OPTION_ACCELMETHOD,	"AccelMethod",	OPTV_STRING,	{0},	FALSE },
    { OPTION_VIVCACHEMEM,	"VivCacheMem",	OPTV_BOOLEAN,	{0},	FALSE },
    { OPTION_SYNCDRAW,	"SyncDraw",	OPTV_BOOLEAN,	{0},	FALSE },
    { OPTION_NOTEARING,	"NoTearing",	OPTV_BOOLEAN,	{0},	FALSE },
	{ -1,			    NULL,		OPTV_NONE,		{0},	FALSE }
};

/* -------------------------------------------------------------------- */

#ifdef XFree86LOADER

MODULESETUPPROTO(FBDevSetup);

static XF86ModuleVersionInfo FBDevVersRec =
{
    FBDEV_DRIVER_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
	MOD_CLASS_VIDEODRV,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData vivanteModuleData = { &FBDevVersRec, FBDevSetup, NULL };

pointer
FBDevSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    static Bool setupDone = FALSE;

    if (!setupDone) {
        setupDone = TRUE;
        xf86AddDriver(&FBDEV, module, HaveDriverFuncs);
        if(gVivFb)
            LoadExtension(&VIVExt, FALSE);
		return (pointer)1;
    } else {
        if (errmaj) *errmaj = LDR_ONCEONLY;
		return NULL;
    }
}

#endif /* XFree86LOADER */


#define FBDEVPTR(p) ((FBDevPtr)((p)->driverPrivate))

static Bool
FBDevGetRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate != NULL)
		return TRUE;

    pScrn->driverPrivate = xnfcalloc(sizeof (FBDevRec), 1);
    if(!pScrn->driverPrivate)
        return FALSE;

    VivPtr vPtr = GET_VIV_PTR(pScrn);

    vPtr->fbAlignOffset = ADDRESS_ALIGNMENT;
    vPtr->fbAlignWidth  = WIDTH_ALIGNMENT;
    vPtr->fbAlignHeight = HEIGHT_ALIGNMENT;

    imxInitSyncFlagsStorage(pScrn);
	return TRUE;
}

static void
FBDevFreeRec(ScrnInfoPtr pScrn)
{
    FBDevPtr fPtr = FBDEVPTR(pScrn);

	if (pScrn->driverPrivate == NULL)
		return;

    if(fPtr->lastVideoMode) {
        xf86DeleteMode(&fPtr->lastVideoMode, fPtr->lastVideoMode);
    }

    imxFreeSyncFlagsStorage(pScrn);
    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

/* -------------------------------------------------------------------- */

static const OptionInfoRec *
FBDevAvailableOptions(int chipid, int busid)
{
	return FBDevOptions;
}

static void
FBDevIdentify(int flags)
{
    xf86PrintChipsets(FBDEV_NAME, "driver for vivante fb", FBDevChipsets);
}

static Bool
FBDevProbe(DriverPtr drv, int flags)
{
    int i;
    ScrnInfoPtr pScrn;
    GDevPtr *devSections;
    int numDevSections;
#ifndef XSERVER_LIBPCIACCESS
	int bus,device,func;
#endif
    const char *dev;
    Bool foundScreen = FALSE;

	TRACE("probe start");

    /* For now, just bail out for PROBE_DETECT. */
	if (flags & PROBE_DETECT)
		return FALSE;

    if ((numDevSections = xf86MatchDevice(FBDEV_DRIVER_NAME, &devSections)) <= 0)
	    return FALSE;

    if (!xf86LoadDrvSubModule(drv, "fbdevhw"))
	    return FALSE;

    for (i = 0; i < numDevSections; i++) {
	    Bool isIsa = FALSE;
	    Bool isPci = FALSE;

        dev = xf86FindOptionValue(devSections[i]->options, "vivante");
	    if (devSections[i]->busID) {
#ifndef XSERVER_LIBPCIACCESS
	        if (xf86ParsePciBusString(devSections[i]->busID,&bus,&device,
					  &func)) {
		    if (!xf86CheckPciSlot(bus,device,func))
		        continue;
		    isPci = TRUE;
		} else
#endif
#ifdef HAVE_ISA
		if (xf86ParseIsaBusString(devSections[i]->busID))
		    isIsa = TRUE;
		else
#endif
		    0;

	    }
        if (fbdevHWProbe(NULL,(char *)dev,NULL)) {
            pScrn = NULL;
		if (isPci) {
#ifndef XSERVER_LIBPCIACCESS
		    /* XXX what about when there's no busID set? */
		    int entity;

		    entity = xf86ClaimPciSlot(bus,device,func,drv,
					      0,devSections[i],
					      TRUE);
		    pScrn = (ScrnInfoPtr)xf86ConfigPciEntity(pScrn,0,entity,
						      NULL,RES_SHARED_VGA,
						      NULL,NULL,NULL,NULL);
		    /* xf86DrvMsg() can't be called without setting these */
		    pScrn->driverName    = FBDEV_DRIVER_NAME;
		    pScrn->name          = FBDEV_NAME;
		    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
			       "claimed PCI slot %d:%d:%d\n",bus,device,func);

#endif
		} else if (isIsa) {
#ifdef HAVE_ISA
		    int entity;

		    entity = xf86ClaimIsaSlot(drv, 0,
					      devSections[i], TRUE);
		    pScrn = xf86ConfigIsaEntity(pScrn,0,entity,
						      NULL,RES_SHARED_VGA,
						      NULL,NULL,NULL,NULL);
#endif
		} else {
		   int entity;

            entity = xf86ClaimFbSlot(drv, 0,
                    devSections[i], TRUE);
            pScrn = xf86ConfigFbEntity(pScrn, 0, entity,
                    NULL, NULL, NULL, NULL);

		}
            if (pScrn) {
                foundScreen = TRUE;

                /* detect chipset */
                CheckChipSet(pScrn);

                pScrn->driverVersion = FBDEV_VERSION;
                pScrn->driverName = FBDEV_DRIVER_NAME;
                pScrn->name = FBDEV_NAME;
                pScrn->Probe = FBDevProbe;
                pScrn->PreInit = FBDevPreInit;
                pScrn->ScreenInit = FBDevScreenInit;

                if(!gEnableXRandR) {
                    pScrn->SwitchMode = fbdevHWSwitchModeWeak();
                    pScrn->AdjustFrame = fbdevHWAdjustFrameWeak();
                    pScrn->EnterVT = fbdevHWEnterVTWeak();
                    pScrn->LeaveVT = fbdevHWLeaveVTWeak();
                    pScrn->ValidMode = fbdevHWValidModeWeak();
                }
                else {
                    pScrn->FreeScreen    = FBDevFreeScreen;
                    pScrn->SwitchMode = imxDisplaySwitchMode;
                    pScrn->AdjustFrame = imxDisplayAdjustFrame;
                    pScrn->EnterVT = imxDisplayEnterVT;
                    pScrn->LeaveVT = imxDisplayLeaveVT;
                    pScrn->ValidMode = imxDisplayValidMode;
                    pScrn->PMEvent = imxPMEvent;
                }

                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "using %s\n", dev ? dev : "default device");
            }
        }
    }
    free(devSections);
	TRACE("probe done");
	return foundScreen;
}

static Bool
FBDevPreInit(ScrnInfoPtr pScrn, int flags)
{
	FBDevPtr fPtr;
    int default_depth, fbbpp;
    const char *s;
    int type;
    const char *fbDeviceName;

	if (flags & PROBE_DETECT) return FALSE;

	TRACE_ENTER("PreInit");

    /* Check the number of entities, and fail if it isn't one. */
    if (pScrn->numEntities != 1)
		return FALSE;

    pScrn->monitor = pScrn->confScreen->monitor;

    FBDevGetRec(pScrn);
    fPtr = FBDEVPTR(pScrn);

    fPtr->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

#ifndef XSERVER_LIBPCIACCESS
	pScrn->racMemFlags = RAC_FB | RAC_COLORMAP | RAC_CURSOR | RAC_VIEWPORT;
	/* XXX Is this right?  Can probably remove RAC_FB */
	pScrn->racIoFlags = RAC_FB | RAC_COLORMAP | RAC_CURSOR | RAC_VIEWPORT;

	if (fPtr->pEnt->location.type == BUS_PCI &&
	    xf86RegisterResources(fPtr->pEnt->index,NULL,ResExclusive)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "xf86RegisterResources() found resource conflicts\n");
		return FALSE;
	}
#endif
    /* open device */
    fbDeviceName = xf86FindOptionValue(fPtr->pEnt->device->options,"fbdev");
    strcpy(fPtr->fbDeviceName, fbDeviceName + 5); // skip past "/dev/"
    if (!fbdevHWInit(pScrn,NULL,(char *)fbDeviceName))
		return FALSE;

    /* get device preferred video mode */
    imxGetDevicePreferredMode(pScrn);

    /* save sync value */
    if(!SaveBuildInModeSyncFlags(pScrn))
        return FALSE;

    default_depth = fbdevHWGetDepth(pScrn, &fbbpp);

    /* let fb device allocate 32-bpp buffer (make gpu happy) */
    if(gVivFb) {
        if(fbbpp == 24)
            fbbpp = 32;
    }

    if (!xf86SetDepthBpp(pScrn, default_depth, default_depth, fbbpp,
            Support24bppFb | Support32bppFb))
		return FALSE;
    xf86PrintDepthBpp(pScrn);

    /* Get the depth24 pixmap format */
    if (pScrn->depth == 24 && pix24bpp == 0)
        pix24bpp = xf86GetBppFromDepth(pScrn, 24);

    /* color weight */
    if (pScrn->depth > 8) {
        rgb zeros = {0, 0, 0};
		if (!xf86SetWeight(pScrn, zeros, zeros))
			return FALSE;
    }

    /* visual init */
	if (!xf86SetDefaultVisual(pScrn, -1))
		return FALSE;

    /* We don't currently support DirectColor at > 8bpp */
    if (pScrn->depth > 8 && pScrn->defaultVisual != TrueColor) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "requested default visual"
                " (%s) is not supported at depth %d\n",
                xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
		return FALSE;
    }

    {
        Gamma zeros = {0.0, 0.0, 0.0};

        if (!xf86SetGamma(pScrn, zeros)) {
			return FALSE;
        }
    }

	pScrn->progClock = TRUE;
	pScrn->rgbBits   = 8;
    pScrn->chipset = "vivante";

    /* handle options */
    xf86CollectOptions(pScrn, NULL);
    if (!(fPtr->Options = malloc(sizeof (FBDevOptions))))
		return FALSE;
    memcpy(fPtr->Options, FBDevOptions, sizeof (FBDevOptions));
    xf86ProcessOptions(pScrn->scrnIndex, fPtr->pEnt->device->options, fPtr->Options);

    vivEnableCacheMemory = xf86ReturnOptValBool(fPtr->Options, OPTION_VIVCACHEMEM, TRUE);
    vivEnableSyncDraw = xf86ReturnOptValBool(fPtr->Options, OPTION_SYNCDRAW, FALSE);
    vivNoTearing = xf86ReturnOptValBool(fPtr->Options, OPTION_NOTEARING, FALSE);

    if(vivNoTearing) {
        gEnableXRandR = FALSE;
        pScrn->SwitchMode = fbdevHWSwitchModeWeak();
        pScrn->AdjustFrame = fbdevHWAdjustFrameWeak();
        pScrn->EnterVT = fbdevHWEnterVTWeak();
        pScrn->LeaveVT = fbdevHWLeaveVTWeak();
        pScrn->ValidMode = fbdevHWValidModeWeak();
    }

	/* dont use shadow framebuffer by default */
	fPtr->shadowFB = xf86ReturnOptValBool(fPtr->Options, OPTION_SHADOW_FB, FALSE);

	debug = xf86ReturnOptValBool(fPtr->Options, OPTION_DEBUG, FALSE);

	/* rotation */
	fPtr->rotate = FBDEV_ROTATE_NONE;
	if ((s = xf86GetOptValString(fPtr->Options, OPTION_ROTATE)))
	{
	  if(!xf86NameCmp(s, "CW"))
	  {
	    fPtr->shadowFB = TRUE;
	    fPtr->rotate = FBDEV_ROTATE_CW;
	    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		       "rotating screen clockwise\n");
	  }
	  else if(!xf86NameCmp(s, "CCW"))
	  {
	    fPtr->shadowFB = TRUE;
	    fPtr->rotate = FBDEV_ROTATE_CCW;
	    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		       "rotating screen counter-clockwise\n");
	  }
	  else if(!xf86NameCmp(s, "UD"))
	  {
	    fPtr->shadowFB = TRUE;
	    fPtr->rotate = FBDEV_ROTATE_UD;
	    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		       "rotating screen upside-down\n");
	  }
	  else
	  {
	    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		       "\"%s\" is not a valid value for Option \"Rotate\"\n", s);
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "valid options are \"CW\", \"CCW\" and \"UD\"\n");
	  }
	}

    // override options specified in xorg.conf
    if(fPtr->shadowFB) {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Shadow buffer enabled, 2D GPU acceleration disabled.\n");
        fPtr->mFakeExa.mUseExaFlag = FALSE;
        fPtr->mFakeExa.mNoAccelFlag = TRUE;
        vivEnableSyncDraw = TRUE;
    }
    else if(gVivFb) {
        fPtr->mFakeExa.mUseExaFlag = TRUE;
        fPtr->mFakeExa.mNoAccelFlag = xf86ReturnOptValBool(fPtr->Options, OPTION_NOACCEL, FALSE);
        if (fPtr->mFakeExa.mNoAccelFlag) {
            vivEnableSyncDraw = TRUE;
        }
    }
    else {
        fPtr->mFakeExa.mUseExaFlag = FALSE;
        fPtr->mFakeExa.mNoAccelFlag = TRUE;
        vivEnableSyncDraw = TRUE;
    }

    /* select video modes */

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "checking modes against framebuffer device...\n");
    fbdevHWSetVideoModes(pScrn);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "checking modes against monitor...\n");
    {
        DisplayModePtr mode, first = mode = pScrn->modes;

        if (mode != NULL) do {
            mode->status = xf86CheckModeForMonitor(mode, pScrn->monitor);
            mode = mode->next;
        } while (mode != NULL && mode != first);

        xf86PruneDriverModes(pScrn);
    }

	if (NULL == pScrn->modes) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"Use built in mode (bpp %d)\n",
			pScrn->bitsPerPixel);
        if(!gEnableXRandR)
            fbdevHWUseBuildinMode(pScrn);
    }
    pScrn->currentMode = pScrn->modes;

    /* First approximation, may be refined in ScreenInit */
    pScrn->displayWidth = pScrn->virtualX;

    xf86PrintModes(pScrn);

    /* Set display resolution */
    xf86SetDpi(pScrn, 0, 0);

	/* Load bpp-specific modules */
	switch ((type = fbdevHWGetType(pScrn)))
	{
	case FBDEVHW_PACKED_PIXELS:
		switch (pScrn->bitsPerPixel)
		{
		case 8:
		case 16:
		case 24:
		case 32:
			break;
		default:
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"unsupported number of bits per pixel: %d",
			pScrn->bitsPerPixel);
			return FALSE;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
               /* Not supported yet, don't know what to do with this */
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "interleaved planes are not yet supported by the "
			  "fbdev driver\n");
		return FALSE;
	case FBDEVHW_TEXT:
               /* This should never happen ...
                * we should check for this much much earlier ... */
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "text mode is not supported by the fbdev driver\n");
		return FALSE;
       case FBDEVHW_VGA_PLANES:
               /* Not supported yet */
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "EGA/VGA planes are not yet supported by the fbdev "
			  "driver\n");
               return FALSE;
       default:
               xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "unrecognised fbdev hardware type (%d)\n", type);
               return FALSE;
	}
    if (xf86LoadSubModule(pScrn, "fb") == NULL) {
        FBDevFreeRec(pScrn);
		return FALSE;
    }

	/* Load shadow if needed */
	if (fPtr->shadowFB) {
		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "using shadow"
			   " framebuffer\n");
		if (!xf86LoadSubModule(pScrn, "shadow")) {
			FBDevFreeRec(pScrn);
			return FALSE;
		}
	}

    /* Load EXA acceleration if needed */
    if (fPtr->mFakeExa.mUseExaFlag) {
        if (!xf86LoadSubModule(pScrn, "exa")) {
            FBDevFreeRec(pScrn);
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Error on loading exa submodule\n");
            return FALSE;
        }
    }

    /* init imx display engine */
    if(gEnableXRandR)
        imxDisplayPreInit(pScrn);

    if(vivNoTearing) {
        if(!tearingSetVideoBuffer(pScrn)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to create back surface buffers\n");
            return FALSE;
        }
    }

    /* make sure display width is correctly aligned */
    pScrn->displayWidth = IMX_ALIGN(pScrn->virtualX, fPtr->fbAlignWidth);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "FBDevPreInit: adjust display width %d\n", pScrn->displayWidth);

    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "PreInit done\n");
	TRACE_EXIT("PreInit");
	return TRUE;
}


static Bool
FBDevCreateScreenResources(ScreenPtr pScreen)
{
    PixmapPtr pPixmap;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    Bool ret;

    pScreen->CreateScreenResources = fPtr->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = FBDevCreateScreenResources;

    if (!ret)
	return FALSE;

    pPixmap = pScreen->GetScreenPixmap(pScreen);

    if(fPtr->shadowFB) {
        if (!shadowAdd(pScreen, pPixmap, fPtr->rotate ?
            shadowUpdateRotatePackedWeak() : shadowUpdatePackedWeak(),
                FBDevWindowLinear, fPtr->rotate, NULL)) {
            return FALSE;
        }
    }

    return TRUE;
}

static Bool
FBDevScreenInit(SCREEN_INIT_ARGS_DECL)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	FBDevPtr fPtr = FBDEVPTR(pScrn);
    VisualPtr visual;
    int init_picture = 0;
    int ret, flags;
	int type;

	TRACE_ENTER("FBDevScreenInit");

#if DEBUG
	ErrorF("\tbitsPerPixel=%d, depth=%d, defaultVisual=%s\n"
	       "\tmask: %x,%x,%x, offset: %d,%d,%d\n",
	       pScrn->bitsPerPixel,
	       pScrn->depth,
	       xf86GetVisualName(pScrn->defaultVisual),
	       pScrn->mask.red,pScrn->mask.green,pScrn->mask.blue,
	       pScrn->offset.red,pScrn->offset.green,pScrn->offset.blue);
#endif

    initPixmapQueue();

    fbdevHWSave(pScrn);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Init mode for fb device\n");
    if (!fbdevHWModeInit(pScrn, pScrn->currentMode)) {
		xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"mode initialization failed\n");
		return FALSE;
    }

    /* record last video mode for later hdmi hot plugout/in */
    if(fPtr->lastVideoMode) {
        xf86DeleteMode(&fPtr->lastVideoMode, fPtr->lastVideoMode);
    }
    fPtr->lastVideoMode = xf86DuplicateMode(pScrn->currentMode); // pScrn->currentMode != NULL

    /* now video ram size is change */
    pScrn->videoRam = fbdevHWGetVidmem(pScrn);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hardware: %s (video memory:"
            " %dkB)\n", fbdevHWGetName(pScrn), pScrn->videoRam / 1024);

    /*Mapping the Video memory*/
    if (NULL == (fPtr->mFB.mFBMemory = fbdevHWMapVidmem(pScrn))) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "mapping of video memory"
                " failed\n");
        return FALSE;
    }
    /*Getting the  linear offset*/
    fPtr->mFB.mFBOffset = fbdevHWLinearOffset(pScrn);

    /*Setting the physcal addr*/
    fPtr->mFB.memPhysBase = pScrn->memPhysBase;

    /*Pitch*/
    pScrn->displayWidth = fbdevHWGetLineLength(pScrn) /
            (pScrn->bitsPerPixel / 8);
    if (pScrn->displayWidth != pScrn->virtualX) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "Pitch updated to %d after ModeInit\n",
                pScrn->displayWidth);
    }
    /*Logical start address*/
    fPtr->mFB.mFBStart = fPtr->mFB.mFBMemory + fPtr->mFB.mFBOffset;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "FB Start = %p  FB Base = %p  FB Offset = %p FB PhyBase %p\n",
            fPtr->mFB.mFBStart, fPtr->mFB.mFBMemory, (void *)fPtr->mFB.mFBOffset, (void *)fPtr->mFB.memPhysBase);

    if(gEnableXRandR)
        imxSetShadowBuffer(pScreen);
    else {
        fPtr->fbMemorySize = pScrn->videoRam - fPtr->mFB.mFBOffset;
        fPtr->fbMemoryScreenReserve = fPtr->fbMemorySize;
        fPtr->fbMemoryStart2 = NULL;
    }

    if(vivNoTearing) {
        // virtual size is reset by xserver; change it to our settings
        tearingSetVideoBuffer(pScrn);

        // wrap these buffers into Gpu surfaces
        if(!tearingWrapSurfaces(pScrn)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to wrap back surface buffers\n");
            return FALSE;
        }
    }


    fbdevHWSaveScreen(pScreen, SCREEN_SAVER_ON);
    fbdevHWAdjustFrame(FBDEVHWADJUSTFRAME_ARGS(0, 0));
    /* mi layer */
    miClearVisualTypes();
    if (pScrn->bitsPerPixel > 8) {
        if (!miSetVisualTypes(pScrn->depth, TrueColorMask, pScrn->rgbBits, TrueColor)) {
			xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"visual type setup failed"
                    " for %d bits per pixel [1]\n",
                    pScrn->bitsPerPixel);
			return FALSE;
        }
    } else {
        if (!miSetVisualTypes(pScrn->depth,
                miGetDefaultVisualMask(pScrn->depth),
                pScrn->rgbBits, pScrn->defaultVisual)) {
			xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"visual type setup failed"
                    " for %d bits per pixel [2]\n",
                    pScrn->bitsPerPixel);
			return FALSE;
        }
    }
    if (!miSetPixmapDepths()) {
	  xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"pixmap depth setup failed\n");
        return FALSE;
    }

	if(fPtr->rotate==FBDEV_ROTATE_CW || fPtr->rotate==FBDEV_ROTATE_CCW)
	{
	  int tmp = pScrn->virtualX;
	  pScrn->virtualX = pScrn->displayWidth = pScrn->virtualY;
	  pScrn->virtualY = tmp;
	} else if (!fPtr->shadowFB) {
		/* FIXME: this doesn't work for all cases, e.g. when each scanline
			has a padding which is independent from the depth (controlfb) */
		pScrn->displayWidth = fbdevHWGetLineLength(pScrn) /
				      (pScrn->bitsPerPixel / 8);

		if (pScrn->displayWidth != pScrn->virtualX) {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,
				   "Pitch updated to %d after ModeInit\n",
				   pScrn->displayWidth);
		}
	}

	if(fPtr->rotate && !fPtr->PointerMoved) {
		fPtr->PointerMoved = pScrn->PointerMoved;
		pScrn->PointerMoved = FBDevPointerMoved;
	}

	if (fPtr->shadowFB) {
	    fPtr->shadow = calloc(1, pScrn->virtualX * pScrn->virtualY *
				  pScrn->bitsPerPixel);

	    if (!fPtr->shadow) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Failed to allocate shadow framebuffer\n");
		return FALSE;
	    }
	}

	switch ((type = fbdevHWGetType(pScrn)))
	{
	case FBDEVHW_PACKED_PIXELS:
    switch (pScrn->bitsPerPixel) {
        case 8:
        case 16:
        case 24:
        case 32:
            ret = fbScreenInit(pScreen, fPtr->shadowFB ? fPtr->shadow
                    : fPtr->mFB.mFBStart, pScrn->virtualX,
                    pScrn->virtualY, pScrn->xDpi,
                    pScrn->yDpi, pScrn->displayWidth,
                    pScrn->bitsPerPixel);
            init_picture = 1;
            break;
        default:
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "internal error: invalid number of bits per"
                    " pixel (%d) encountered in"
				   " FBDevScreenInit()\n", pScrn->bitsPerPixel);
			ret = FALSE;
			break;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
		/* This should never happen ...
		* we should check for this much much earlier ... */
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: interleaved planes are not yet "
			   "supported by the fbdev driver\n");
		ret = FALSE;
		break;
	case FBDEVHW_TEXT:
		/* This should never happen ...
		* we should check for this much much earlier ... */
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: text mode is not supported by the "
			   "fbdev driver\n");
		ret = FALSE;
		break;
	case FBDEVHW_VGA_PLANES:
		/* Not supported yet */
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: EGA/VGA Planes are not yet "
			   "supported by the fbdev driver\n");
		ret = FALSE;
		break;
	default:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: unrecognised hardware type (%d) "
			   "encountered in FBDevScreenInit()\n", type);
            ret = FALSE;
            break;
    }
	if (!ret)
		return FALSE;

    if (pScrn->bitsPerPixel > 8) {
        /* Fixup RGB ordering */
        visual = pScreen->visuals + pScreen->numVisuals;
        while (--visual >= pScreen->visuals) {
            if ((visual->class | DynamicClass) == DirectColor) {
                visual->offsetRed = pScrn->offset.red;
                visual->offsetGreen = pScrn->offset.green;
                visual->offsetBlue = pScrn->offset.blue;
                visual->redMask = pScrn->mask.red;
                visual->greenMask = pScrn->mask.green;
                visual->blueMask = pScrn->mask.blue;
            }
        }
    }

    /* must be after RGB ordering fixed */
	if (init_picture && !fbPictureInit(pScreen, NULL, 0))
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                "Render extension initialisation failed\n");

	if (fPtr->shadowFB && !FBDevShadowInit(pScreen)) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "shadow framebuffer initialization failed\n");
	    return FALSE;
	}

	if (!fPtr->rotate)
	  FBDevDGAInit(pScrn, pScreen);
	else {
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "display rotated; disabling DGA\n");
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "using driver rotation; disabling "
			                "XRandR\n");
	  xf86DisableRandR();
	  if (pScrn->bitsPerPixel == 24)
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "rotation might be broken at 24 "
                                             "bits per pixel\n");
	}

    fPtr->mFakeExa.mIsInited = FALSE;
    if (fPtr->mFakeExa.mUseExaFlag) {
        TRACE_INFO("Loading EXA");
        if (!InitExaLayer(pScreen)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "internal error: initExaLayer failed "
                    "in FBDevScreenInit()\n");
        }

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Init SHM pixmap support\n");
        InitShmPixmap(pScreen);
    }

    xf86SetBlackWhitePixels(pScreen);
#if !defined(FIX_NO_MI_BACKINGSTORE)
    miInitializeBackingStore(pScreen);
#endif
    xf86SetBackingStore(pScreen);

    if(gVivFb)
        pScrn->vtSema = TRUE;

    /* software cursor */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    /* colormap */
	switch ((type = fbdevHWGetType(pScrn)))
	{
	/* XXX It would be simpler to use miCreateDefColormap() in all cases. */
	case FBDEVHW_PACKED_PIXELS:
    if (!miCreateDefColormap(pScreen)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "internal error: miCreateDefColormap failed "
				   "in FBDevScreenInit()\n");
			return FALSE;
		}
		break;
	case FBDEVHW_INTERLEAVED_PLANES:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: interleaved planes are not yet "
			   "supported by the fbdev driver\n");
		return FALSE;
	case FBDEVHW_TEXT:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: text mode is not supported by "
			   "the fbdev driver\n");
		return FALSE;
	case FBDEVHW_VGA_PLANES:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: EGA/VGA planes are not yet "
			   "supported by the fbdev driver\n");
		return FALSE;
	default:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "internal error: unrecognised fbdev hardware type "
			   "(%d) encountered in FBDevScreenInit()\n", type);
		return FALSE;
    }
    flags = CMAP_PALETTED_TRUECOLOR;
    if (!xf86HandleColormaps(pScreen, 256, 8, fbdevHWLoadPaletteWeak(),
				NULL, flags))
		return FALSE;

    xf86DPMSInit(pScreen, fbdevHWDPMSSetWeak(), 0);

    pScreen->SaveScreen = fbdevHWSaveScreenWeak();

    /* Wrap the current CloseScreen function */
    fPtr->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = FBDevCloseScreen;

    if(gVivFb) {
        fPtr->CreateScreenResources = pScreen->CreateScreenResources;
	    pScreen->CreateScreenResources = FBDevCreateScreenResources;
    }

#if XV
    {
        XF86VideoAdaptorPtr *ptr;

        int n = xf86XVListGenericAdaptors(pScrn, &ptr);
        if (n) {
            xf86XVScreenInit(pScreen, ptr, n);
        }
    }
#endif

    if(gEnableXRandR) {
        if (!imxDisplayFinishScreenInit(pScrn->scrnIndex, pScreen)) {
            return FALSE;
        }
    }

    if(gEnableDRI) {
        if (VivDRIScreenInit(pScreen)) {
            VivDRIFinishScreenInit(pScreen);
        }
    }

    /* restore sync for FSL extension */
    if(!RestoreSyncFlags(pScrn))
        return FALSE;

    TRACE_EXIT("FBDevScreenInit");

    return TRUE;
}

static Bool
FBDevCloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);
	if (pScrn->vtSema == TRUE) {
        if(gEnableXRandR)
            imxDisplayLeaveVT(VT_FUNC_ARGS(0));
	}

    freePixmapQueue();

    if(gEnableDRI)
        VivDRICloseScreen(pScreen);

    if (fPtr->mFakeExa.mUseExaFlag) {
        LOG("UnLoading EXA");
        if (fPtr->mFakeExa.mIsInited && !DestroyExaLayer(pScreen)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "internal error: DestroyExaLayer failed "
                    "in VivCloseScreen()\n");
        }
    }

    if(gVivFb)
        xf86_cursors_fini(pScreen);

    fbdevHWRestore(pScrn);
    fbdevHWUnmapVidmem(pScrn);
	if (fPtr->shadow) {
	    shadowRemove(pScreen, pScreen->GetScreenPixmap(pScreen));
	    free(fPtr->shadow);
	    fPtr->shadow = NULL;
	}
	if (fPtr->pDGAMode) {
	  free(fPtr->pDGAMode);
	  fPtr->pDGAMode = NULL;
	  fPtr->nDGAMode = 0;
	}
	pScrn->vtSema = FALSE;

	pScreen->CreateScreenResources = fPtr->CreateScreenResources;
    pScreen->CloseScreen = fPtr->CloseScreen;
	return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}

static Bool
FBDevDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
    xorgHWFlags *flag;

    switch (op) {
        case GET_REQUIRED_HW_INTERFACES:
            flag = (CARD32*) ptr;
            (*flag) = 0;
	    return TRUE;
        default:
	    return FALSE;
    }
}

#ifdef XSERVER_LIBPCIACCESS
static Bool FBDevPciProbe(DriverPtr drv, int entity_num,
			  struct pci_device *dev, intptr_t match_data)
{
    ScrnInfoPtr pScrn = NULL;

    if (!xf86LoadDrvSubModule(drv, "fbdevhw"))
	return FALSE;

    pScrn = xf86ConfigPciEntity(NULL, 0, entity_num, NULL, NULL,
				NULL, NULL, NULL, NULL);
    if (pScrn) {
	const char *device;
	GDevPtr devSection = xf86GetDevFromEntity(pScrn->entityList[0],
						  pScrn->entityInstanceList[0]);

	device = xf86FindOptionValue(devSection->options, "vivante");
	if (fbdevHWProbe(NULL, (char *)device, NULL)) {
	    pScrn->driverVersion = FBDEV_VERSION;
	    pScrn->driverName    = FBDEV_DRIVER_NAME;
	    pScrn->name          = FBDEV_NAME;
	    pScrn->Probe         = FBDevProbe;
	    pScrn->PreInit       = FBDevPreInit;
	    pScrn->ScreenInit    = FBDevScreenInit;
	    pScrn->SwitchMode    = fbdevHWSwitchModeWeak();
	    pScrn->AdjustFrame   = fbdevHWAdjustFrameWeak();
	    pScrn->EnterVT       = fbdevHWEnterVTWeak();
	    pScrn->LeaveVT       = fbdevHWLeaveVTWeak();
	    pScrn->ValidMode     = fbdevHWValidModeWeak();

	    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		       "claimed PCI slot %d@%d:%d:%d\n",
		       dev->bus, dev->domain, dev->dev, dev->func);
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "using %s\n", device ? device : "default device");
	}
	else {
	    pScrn = NULL;
	}
    }

    return (pScrn != NULL);
}
#endif

/***********************************************************************
 * Shadow stuff
 ***********************************************************************/

static Bool
FBDevShadowInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);

    if (!shadowSetup(pScreen)) {
	return FALSE;
    }

    fPtr->CreateScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = FBDevCreateScreenResources;

    return TRUE;
}

static void *
FBDevWindowLinear(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode,
		 CARD32 *size, void *closure)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    FBDevPtr fPtr = FBDEVPTR(pScrn);

    if (!pScrn->vtSema)
      return NULL;

    if (fPtr->lineLength)
      *size = fPtr->lineLength;
    else
      *size = fPtr->lineLength = fbdevHWGetLineLength(pScrn);

    return ((CARD8 *)fPtr->mFB.mFBStart + row * fPtr->lineLength + offset);
}

static void
FBDevPointerMoved(SCRN_ARG_TYPE arg, int x, int y)
{
    SCRN_INFO_PTR(arg);
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    int newX, newY;

    switch (fPtr->rotate)
    {
    case FBDEV_ROTATE_CW:
	/* 90 degrees CW rotation. */
	newX = pScrn->pScreen->height - y - 1;
	newY = x;
	break;

    case FBDEV_ROTATE_CCW:
	/* 90 degrees CCW rotation. */
	newX = y;
	newY = pScrn->pScreen->width - x - 1;
	break;

    case FBDEV_ROTATE_UD:
	/* 180 degrees UD rotation. */
	newX = pScrn->pScreen->width - x - 1;
	newY = pScrn->pScreen->height - y - 1;
	break;

    default:
	/* No rotation. */
	newX = x;
	newY = y;
	break;
    }

    /* Pass adjusted pointer coordinates to wrapped PointerMoved function. */
    (*fPtr->PointerMoved)(arg, newX, newY);
}


/***********************************************************************
 * DGA stuff
 ***********************************************************************/
static Bool FBDevDGAOpenFramebuffer(ScrnInfoPtr pScrn, char **DeviceName,
				   unsigned char **ApertureBase,
				   int *ApertureSize, int *ApertureOffset,
				   int *flags);
static Bool FBDevDGASetMode(ScrnInfoPtr pScrn, DGAModePtr pDGAMode);
static void FBDevDGASetViewport(ScrnInfoPtr pScrn, int x, int y, int flags);

static Bool
FBDevDGAOpenFramebuffer(ScrnInfoPtr pScrn, char **DeviceName,
		       unsigned char **ApertureBase, int *ApertureSize,
		       int *ApertureOffset, int *flags)
{
    *DeviceName = NULL;		/* No special device */
    *ApertureBase = (unsigned char *)(pScrn->memPhysBase);
    *ApertureSize = pScrn->videoRam;
    *ApertureOffset = pScrn->fbOffset;
    *flags = 0;

    return TRUE;
}

static Bool
FBDevDGASetMode(ScrnInfoPtr pScrn, DGAModePtr pDGAMode)
{
    DisplayModePtr pMode;
    int scrnIdx = pScrn->pScreen->myNum;
    int frameX0, frameY0;

    if (pDGAMode) {
	pMode = pDGAMode->mode;
	frameX0 = frameY0 = 0;
    }
    else {
	if (!(pMode = pScrn->currentMode))
	    return TRUE;

	frameX0 = pScrn->frameX0;
	frameY0 = pScrn->frameY0;
    }

    if (!(*pScrn->SwitchMode)(SWITCH_MODE_ARGS(pScrn, pMode)))
	return FALSE;
    (*pScrn->AdjustFrame)(ADJUST_FRAME_ARGS(pScrn, frameX0, frameY0));

    return TRUE;
}

static void
FBDevDGASetViewport(ScrnInfoPtr pScrn, int x, int y, int flags)
{
    (*pScrn->AdjustFrame)(ADJUST_FRAME_ARGS(pScrn, x, y));
}

static int
FBDevDGAGetViewport(ScrnInfoPtr pScrn)
{
    return (0);
}

static DGAFunctionRec FBDevDGAFunctions =
{
    FBDevDGAOpenFramebuffer,
    NULL,       /* CloseFramebuffer */
    FBDevDGASetMode,
    FBDevDGASetViewport,
    FBDevDGAGetViewport,
    NULL,       /* Sync */
    NULL,       /* FillRect */
    NULL,       /* BlitRect */
    NULL,       /* BlitTransRect */
};

static void
FBDevDGAAddModes(ScrnInfoPtr pScrn)
{
    FBDevPtr fPtr = FBDEVPTR(pScrn);
    DisplayModePtr pMode = pScrn->modes;
    DGAModePtr pDGAMode;

    do {
	pDGAMode = realloc(fPtr->pDGAMode,
		           (fPtr->nDGAMode + 1) * sizeof(DGAModeRec));
	if (!pDGAMode)
	    break;

	fPtr->pDGAMode = pDGAMode;
	pDGAMode += fPtr->nDGAMode;
	(void)memset(pDGAMode, 0, sizeof(DGAModeRec));

	++fPtr->nDGAMode;
	pDGAMode->mode = pMode;
	pDGAMode->flags = DGA_CONCURRENT_ACCESS | DGA_PIXMAP_AVAILABLE;
	pDGAMode->byteOrder = pScrn->imageByteOrder;
	pDGAMode->depth = pScrn->depth;
	pDGAMode->bitsPerPixel = pScrn->bitsPerPixel;
	pDGAMode->red_mask = pScrn->mask.red;
	pDGAMode->green_mask = pScrn->mask.green;
	pDGAMode->blue_mask = pScrn->mask.blue;
	pDGAMode->visualClass = pScrn->bitsPerPixel > 8 ?
	    TrueColor : PseudoColor;
	pDGAMode->xViewportStep = 1;
	pDGAMode->yViewportStep = 1;
	pDGAMode->viewportWidth = pMode->HDisplay;
	pDGAMode->viewportHeight = pMode->VDisplay;

	if (fPtr->lineLength)
	  pDGAMode->bytesPerScanline = fPtr->lineLength;
	else
	  pDGAMode->bytesPerScanline = fPtr->lineLength = fbdevHWGetLineLength(pScrn);

	pDGAMode->imageWidth = pMode->HDisplay;
	pDGAMode->imageHeight =  pMode->VDisplay;
	pDGAMode->pixmapWidth = pDGAMode->imageWidth;
	pDGAMode->pixmapHeight = pDGAMode->imageHeight;
	pDGAMode->maxViewportX = pScrn->virtualX -
				    pDGAMode->viewportWidth;
	pDGAMode->maxViewportY = pScrn->virtualY -
				    pDGAMode->viewportHeight;

	pDGAMode->address = fPtr->mFB.mFBStart;

	pMode = pMode->next;
    } while (pMode != pScrn->modes);
}

static Bool
FBDevDGAInit(ScrnInfoPtr pScrn, ScreenPtr pScreen)
{
#ifdef XFreeXDGA
    FBDevPtr fPtr = FBDEVPTR(pScrn);

    if (pScrn->depth < 8)
	return FALSE;

    if (!fPtr->nDGAMode)
	FBDevDGAAddModes(pScrn);

    return (DGAInit(pScreen, &FBDevDGAFunctions,
	    fPtr->pDGAMode, fPtr->nDGAMode));
#else
    return TRUE;
#endif
}

/************************************************************************
 * X Window System Registration (START)
 ************************************************************************/

static Bool InitExaLayer(ScreenPtr pScreen)
{
    ExaDriverPtr pExa;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    VivPtr pViv = FBDEVPTR(pScrn);
    Bool acc = !pViv->mFakeExa.mNoAccelFlag;

    xf86DrvMsg(pScreen->myNum, X_INFO, "test Initializing EXA\n");

    #if defined(COMMIT)
    #define COMMITSTRING(commit) #commit
    #define VERSION_STRING(commit) COMMITSTRING(commit)
    xf86DrvMsg(pScreen->myNum, X_INFO, "(driver build from: " VERSION_STRING(COMMIT) ")\n");
    #endif

    /*Initing EXA*/
    pExa = exaDriverAlloc();
    if (!pExa) {
        TRACE_ERROR("Unable to allocate exa driver");
        pViv->mFakeExa.mNoAccelFlag = TRUE;
        return FALSE;
    }

    pViv->mFakeExa.mExaDriver = pExa;
    pViv->pScreen = pScreen;

    /*Exa version*/
    pExa->exa_major = EXA_VERSION_MAJOR;
    pExa->exa_minor = EXA_VERSION_MINOR;

    /* 12 bit coordinates */
    pExa->maxX = VIV_MAX_WIDTH;
    pExa->maxY = VIV_MAX_HEIGHT;

    /*Memory Manager*/
    pExa->memoryBase = pViv->mFB.mFBStart; /*logical*/
    pExa->memorySize = pViv->fbMemorySize;
    pExa->offScreenBase = pViv->fbMemoryScreenReserve;

    if (!VIV2DGPUUserMemMap((char*) pExa->memoryBase, pScrn->memPhysBase, pExa->memorySize, &pViv->mFB.mMappingInfo, (unsigned int *)&pViv->mFB.memGpuBase)) {
        TRACE_ERROR("ERROR ON MAPPING FB\n");
        return FALSE;
    }

    /*flags*/
    pExa->flags = EXA_HANDLES_PIXMAPS | EXA_SUPPORTS_PREPARE_AUX | EXA_OFFSCREEN_PIXMAPS;

    /*Subject to change*/
    pExa->pixmapOffsetAlign = ADDRESS_ALIGNMENT;
    /*This is for sure*/
    pExa->pixmapPitchAlign = PIXMAP_PITCH_ALIGN;

    pExa->WaitMarker = VivEXASync;

    pExa->PrepareSolid = acc ? VivPrepareSolid : DummyPrepareSolid;
    pExa->Solid = VivSolid;
    pExa->DoneSolid = VivDoneSolid;

    pExa->PrepareCopy = acc ? VivPrepareCopy : DummyPrepareCopy;
    pExa->Copy = VivCopy;
    pExa->DoneCopy = VivDoneCopy;

    pExa->UploadToScreen = acc ? VivUploadToScreen : DummyUploadToScreen;

    pExa->CheckComposite = acc ? VivCheckComposite : DummyCheckComposite;
    pExa->PrepareComposite = acc ? VivPrepareComposite : DummyPrepareComposite;
    pExa->Composite = VivComposite;
    pExa->DoneComposite = VivDoneComposite;

    pExa->CreatePixmap = VivCreatePixmap;
    pExa->DestroyPixmap = VivDestroyPixmap;
    pExa->ModifyPixmapHeader = VivModifyPixmapHeader;
    pExa->PixmapIsOffscreen = VivPixmapIsOffscreen;
    pExa->PrepareAccess = VivPrepareAccess;
    pExa->FinishAccess = VivFinishAccess;

    if (!exaDriverInit(pScreen, pExa)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "exaDriverinit failed.\n");
        return FALSE;
    }

    if (!VIV2DGPUCtxInit(&pViv->mGrCtx)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "internal error: GPU Ctx Init Failed\n");
        return FALSE;
    }

    pViv->mFakeExa.mIsInited = TRUE;
    return TRUE;
}

static Bool DestroyExaLayer(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    VivPtr pViv = FBDEVPTR(pScrn);
    xf86DrvMsg(pScreen->myNum, X_INFO, "Shutdown EXA\n");

    ExaDriverPtr pExa = pViv->mFakeExa.mExaDriver;
    if (!VIV2DGPUUserMemUnMap((char*) pExa->memoryBase, pExa->memorySize, pViv->mFB.mMappingInfo, pViv->mFB.memGpuBase)) {
        TRACE_ERROR("Unmapping User memory Failed\n");
    }

    exaDriverFini(pScreen);

    if (!VIV2DGPUCtxDeInit(&pViv->mGrCtx)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "internal error: GPU Ctx DeInit Failed\n");
        return FALSE;
    }
    return TRUE;
}

static void
FBDevFreeScreen(FREE_SCREEN_ARGS_DECL)
{
#ifndef XF86_SCRN_INTERFACE
    ScrnInfoPtr pScrn = xf86Screens[arg];
#else
    ScrnInfoPtr pScrn = arg;
#endif

    FBDevFreeRec(pScrn);
}

void
OnCrtcModeChanged(ScrnInfoPtr pScrn)
{
    if(gEnableDRI)
        VivUpdateDriScreen(pScrn);
}

static ShmFuncs gShmFuncs =
{
    .CreatePixmap = ShmCreatePixmap,
    .PutImage     = NULL//ShmPutImage
};

static void InitShmPixmap(ScreenPtr pScreen)
{
    ShmRegisterFuncs(pScreen, &gShmFuncs);
}

// call this function at startup
static Bool
SaveBuildInModeSyncFlags(ScrnInfoPtr pScrn)
{
    if(gEnableFbSyncExt) {
        int fdDev = fbdevHWGetFD(pScrn);
        struct fb_var_screeninfo fbVarScreenInfo;
        if (0 != ioctl(fdDev, FBIOGET_VSCREENINFO, &fbVarScreenInfo)) {

            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "unable to get VSCREENINFO %s\n",
                strerror(errno));
            return FALSE;
        }
        else {
            imxStoreSyncFlags(pScrn, "current", fbVarScreenInfo.sync);
        }
    }

    return TRUE;
}

static Bool
RestoreSyncFlags(ScrnInfoPtr pScrn)
{
    if(gEnableFbSyncExt) {
        char *modeName = "current";
        unsigned int fbSync = 0;
        if(pScrn->currentMode)
            modeName = pScrn->currentMode->name;

        if(!imxLoadSyncFlags(pScrn, modeName, &fbSync)) {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                "Failed to load FB_SYNC_ flags from storage for mode %s\n",
                modeName);
            return TRUE;
        }

        struct fb_var_screeninfo fbVarScreenInfo;
        int fdDev = fbdevHWGetFD(pScrn);
        if (-1 == fdDev) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "frame buffer device not available or initialized\n");
            return FALSE;
        }

        /* Query the FB variable screen info */
        if (0 != ioctl(fdDev, FBIOGET_VSCREENINFO, &fbVarScreenInfo)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "unable to get FB VSCREENINFO for current mode: %s\n",
                strerror(errno));
            return FALSE;
        }

        fbVarScreenInfo.sync = fbSync;

        if (0 != ioctl(fdDev, FBIOPUT_VSCREENINFO, &fbVarScreenInfo)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "unable to restore FB VSCREENINFO: %s\n",
                strerror(errno));
            return FALSE;
        }
    }

    return TRUE;
}

static void
CheckChipSet(ScrnInfoPtr pScrn)
{
    int isSL = 0;
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if(fp == NULL)
        return;

    char *buf = (char *)malloc(4096);
    while (NULL != fgets(buf, 4096, fp)) {
        // look for 'Hardware'
        if(strncmp("Hardware", buf, strlen("Hardware")) == 0) {
            if(strstr(buf, "Freescale i.MX6 SoloLite") != NULL) {
                isSL = 1;
            }
            else if(strstr(buf, "Freescale i.MX6 SoloX") != NULL) {
                isSL = 1;
            }
            break;
        }
    }

    if(isSL) {
        // enable XRandR for SL/SX, with some limitations
#if 0
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "SoloLite: disable XRandR\n");
        gEnableXRandR = FALSE;
#endif
    }
}

#include "vivante_priv.h"
#include "vivante_gal.h"

#define MAX_BACK_SURFACES 3
static Bool
tearingSetVideoBuffer(ScrnInfoPtr pScrn)
{
    int fd = fbdevHWGetFD(pScrn);

	/* Set video buffer */
	struct fb_var_screeninfo fbVarScreenInfo;
	if (0 != ioctl(fd, FBIOGET_VSCREENINFO, &fbVarScreenInfo)) {
		return FALSE;
	}

    fbVarScreenInfo.xres_virtual = gcmALIGN(fbVarScreenInfo.xres_virtual, WIDTH_ALIGNMENT);
    fbVarScreenInfo.yres_virtual = gcmALIGN(fbVarScreenInfo.yres_virtual, HEIGHT_ALIGNMENT);

	fbVarScreenInfo.yres_virtual *= (1 + MAX_BACK_SURFACES);
	fbVarScreenInfo.bits_per_pixel = pScrn->bitsPerPixel;

	if (0 != ioctl(fd, FBIOPUT_VSCREENINFO, &fbVarScreenInfo)) {
		return FALSE;
	}

    return TRUE;
}

static GenericSurfacePtr
tearingWrapSurface(ScrnInfoPtr pScrn, int index)
{
    GenericSurfacePtr surf = gcvNULL;
    gceSTATUS status = gcvSTATUS_OK;
    gctPOINTER mHandle = gcvNULL;

    int fd = fbdevHWGetFD(pScrn);

	/* Set video buffer */
	struct fb_var_screeninfo fbVarScreenInfo;
	if (0 != ioctl(fd, FBIOGET_VSCREENINFO, &fbVarScreenInfo)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to get fb var info!\n");
		return NULL;
	}

    int fbSize = fbdevHWGetLineLength(pScrn) * fbVarScreenInfo.yres_virtual / (1 + MAX_BACK_SURFACES);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
        "frame size %dx%d\n",
        fbVarScreenInfo.xres_virtual, fbVarScreenInfo.yres_virtual);

    unsigned int phy = (unsigned int)pScrn->memPhysBase + fbdevHWLinearOffset(pScrn) + fbSize * index;
    unsigned int vir = (unsigned int)fbdevHWMapVidmem(pScrn) + fbdevHWLinearOffset(pScrn) + fbSize * index;

    status = gcoOS_Allocate(gcvNULL, sizeof (GenericSurface), &mHandle);
    if (status != gcvSTATUS_OK)
    {
        return NULL;
    }
    memset(mHandle, 0, sizeof (GenericSurface));
    surf = (GenericSurfacePtr) mHandle;

    surf->mVideoNode.mSizeInBytes = fbSize;
    surf->mVideoNode.mPool = gcvPOOL_USER;

    surf->mVideoNode.mPhysicalAddr = phy;
    surf->mVideoNode.mLogicalAddr = (gctPOINTER) vir;

    surf->mBytesPerPixel = pScrn->bitsPerPixel / 8;
    surf->mTiling = gcvLINEAR;
    surf->mAlignedWidth = fbVarScreenInfo.xres_virtual;
    surf->mAlignedHeight = fbVarScreenInfo.yres_virtual / (1 + MAX_BACK_SURFACES);
    surf->mStride = fbdevHWGetLineLength(pScrn);
    surf->mRotation = gcvSURF_0_DEGREE;
    surf->mLogicalAddr = surf->mVideoNode.mLogicalAddr;
    surf->mIsWrapped = gcvTRUE;

    return surf;
}

static GenericSurfacePtr _surfaces[1+MAX_BACK_SURFACES];

static Bool
tearingWrapSurfaces(ScrnInfoPtr pScrn)
{
    int i;
    for(i=0; i<sizeof(_surfaces) / sizeof(_surfaces[0]); i++)
        _surfaces[i] = NULL;

    for(i=0; i<sizeof(_surfaces) / sizeof(_surfaces[0]); i++) {
        _surfaces[i] = tearingWrapSurface(pScrn, i);
        if(_surfaces[i] == NULL)
            return FALSE;
    }

    return TRUE;
}

static int
tearingFlip(ScrnInfoPtr pScrn, int index)
{
    struct fb_var_screeninfo   var;
    int fd = fbdevHWGetFD(pScrn);

    // calculate xoff and yoff
    if (-1 == ioctl(fd, FBIOGET_VSCREENINFO, (void*)&var))
        return BadRequest;

    var.yoffset = var.yres_virtual * index / (1 + MAX_BACK_SURFACES);

    if (-1 == ioctl(fd, FBIOPAN_DISPLAY, (void*)&var))
        return BadRequest;

    return Success;
}

static int
tearingCopy(ScreenPtr pScreen, int index)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VivPtr pViv = VIVPTR_FROM_SCREEN(pScreen);
    GenericSurfacePtr src;
    GenericSurfacePtr dst;
    gcsRECT srcRect;
    VivPictFormat vivFmt;
    gceSTATUS status = gcvSTATUS_OK;

    VIVGPUPtr gpuctx = (VIVGPUPtr) (pViv->mGrCtx.mGpu);

    GetDefaultFormat(pScrn->bitsPerPixel, &vivFmt);

    // set source
    src = _surfaces[0];
    status = gco2D_SetGenericSource
            (
            gpuctx->mDriver->m2DEngine,
            &src->mVideoNode.mPhysicalAddr,
            1,
            &src->mStride,
            1,
            src->mTiling,
            vivFmt.mVivFmt,
            gcvSURF_0_DEGREE,
            src->mAlignedWidth,
            src->mAlignedHeight
            );
    if (status != gcvSTATUS_OK) {
        return -1;
    }

    // set dest
    dst = _surfaces[index];
    status = gco2D_SetGenericTarget
            (
            gpuctx->mDriver->m2DEngine,
            &dst->mVideoNode.mPhysicalAddr,
            1,
            &dst->mStride,
            1,
            dst->mTiling, // use source surface tiling
            vivFmt.mVivFmt,
            gcvSURF_0_DEGREE,
            dst->mAlignedWidth,
            dst->mAlignedHeight
            );
    if (status != gcvSTATUS_OK) {
        return -1;
    }

    // clip
    srcRect.left = 0;
    srcRect.top = 0;
    srcRect.right = pScrn->virtualX;
    srcRect.bottom = pScrn->virtualY;

    status = gco2D_SetClipping(gpuctx->mDriver->m2DEngine, &srcRect);
    if (status != gcvSTATUS_OK) {
        return -1;
    }

    // blit this slice to helper surface
    status = gco2D_BatchBlit(
            gpuctx->mDriver->m2DEngine,
            1,
            &srcRect,
            &srcRect,
            0xCC, /* copy */
            0xCC,
            vivFmt.mVivFmt
            );

    if (status != gcvSTATUS_OK) {
        return -1;
    }

    // must complete
    VIV2DGPUBlitComplete(&pViv->mGrCtx, TRUE);
    freePixmapQueue();

    return 0;
}

static Bool dirty = FALSE;

Bool FbDoFlip(ScreenPtr pScreen, int restore)
{
    static int index = 1;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    if(restore) {
        return tearingFlip(pScrn, 0);
    }

    // if !dirty

    if(tearingCopy(pScreen, index) != 0)
        return FALSE;
    if(tearingFlip(pScrn, index) != Success)
        return FALSE;

    index++;
    if(index >= (1 + MAX_BACK_SURFACES))
        index = 1;

    dirty = FALSE;
    return TRUE;
}

void OnSurfaceDamaged(ScreenPtr pScreen)
{
    dirty = TRUE;
}

unsigned int GetExaSettings()
{
    unsigned int flags = 0;

    if(vivEnableCacheMemory)
        flags |= 1;
    if(vivEnableSyncDraw)
        flags |= 2;
    if(vivNoTearing)
        flags |= 4;

    return flags;
}

