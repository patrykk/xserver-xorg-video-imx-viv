/*
 * Copyright (C) 2011,2013 Freescale Semiconductor, Inc.  All Rights Reserved.
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

#include "vivante_common.h"
#include "vivante.h"

#include <fcntl.h>
#include <errno.h>
 
#include <linux/fb.h>
#include "xf86DDC.h"

#include "imx_display.h"

#include <X11/Xatom.h>

#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,7,6,0,0)
#include <X11/extensions/dpmsconst.h>
#else
#ifndef DPMS_SERVER
#define DPMS_SERVER 1
#include <X11/extensions/dpms.h>
#undef DPMS_SERVER
#endif
#endif

typedef VivPtr ImxPtr;
typedef VivRec ImxRec;

#if !defined(max)
#define max(a, b) ((a) < (b) ? (b) : (a))
#endif

extern void OnCrtcModeChanged(ScrnInfoPtr pScrn);


#define IMXPTR(pScrnInfo) ((ImxPtr)((pScrnInfo)->driverPrivate))

static int
GCD(int a, int b)
{
	/* Euclidean's algorithm */

	if (0 == a)
	{
		return b;
	}

	while (0 != b)
	{
		if (a > b)
		{
			a -= b;
		}
		else
		{
			b -= a;
		}
	}

	return a;
}

static int
LCM(a, b)
{
	return (a * b) / GCD(a, b);
}

typedef struct {
	xf86CrtcConfigFuncsRec	imxCrtcConfigFuncs;
	xf86CrtcFuncsRec	imxCrtcFuncs;
	xf86OutputFuncsRec	imxOutputFuncs;

	/* Atoms for output properties */
	Atom	atomEdid;

	/* TODO - maybe don't need to store these? */
	xf86CrtcPtr	crtcPtr;
	xf86OutputPtr	outputPtr;

	/* Which mode is currently set */
	char		fbModeNameCurrent[64];

	Bool		fbShadowAllocated;
	Bool		edidModesAvail;

	/* Buffer for reading EDID monitor data */
	Uchar		edidDataBytes[128];

	/* List of modes supported by frame buffer. */
	DisplayModePtr	fbModesList;

	/* Range of frame buffer modes supported. */
	int		fbMinWidth;
	int		fbMinHeight;
	int		fbMaxWidth;
	int		fbMaxHeight;

} ImxDisplayRec, *ImxDisplayPtr;

#define IMXDISPLAYPTR(imxPtr) ((ImxDisplayPtr)((imxPtr)->displayPrivate))

static void
imxDisplayGetPreInitMaxSize(ScrnInfoPtr pScrn, int* pMaxWidth, int* pMaxHeight);
static Bool
imxDisplayStartScreenInit(int scrnIndex, ScreenPtr pScreen);
static void
imxSetPreferFlag(ScrnInfoPtr pScrn, DisplayModePtr mode);
static BOOL
imxDisplayCheckModeXRandR(ScrnInfoPtr pScrn);
static BOOL
imxDisplayCheckBpp(ScrnInfoPtr pScrn);

Bool imxSetShadowBuffer(ScreenPtr pScreen)
{
    VivPtr fPtr;
    int scrnIndex = pScreen->myNum;
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    fPtr = GET_VIV_PTR(pScrn);

	fPtr->fbMemorySize = fbdevHWGetVidmem(pScrn) - fPtr->mFB.mFBOffset;

    /* reserve second frame buffer for shadow */

	/* Retrieve the max sizes supported by frame buffer. */
	int fbMaxWidth;
	int fbMaxHeight;
	imxDisplayGetPreInitMaxSize(pScrn, &fbMaxWidth, &fbMaxHeight);

	/* Take user mode into account */
	fbMaxWidth = max(fbMaxWidth, 1920);
	fbMaxHeight = max(fbMaxHeight, 1088);

	/* Apply alignment requirements */
	fbMaxWidth = IMX_ALIGN(fbMaxWidth, fPtr->fbAlignWidth);
	fbMaxHeight = IMX_ALIGN(fbMaxHeight, fPtr->fbAlignHeight);

	/* What is aligned bytes per line? */
	const int fbBytesPerPixel = (pScrn->bitsPerPixel + 7) / 8;
	const int fbBytesPerLine = fbMaxWidth * fbBytesPerPixel;

	/* Determine if there is enough memory to reserve a */
	/* second frame buffer for XRandR rotation support. */
	const int fbMaxScreenSize = fbMaxWidth * fbMaxHeight * fbBytesPerPixel;
	const int fbOffsetScreen2 = IMX_ALIGN(fbMaxScreenSize, fPtr->fbAlignOffset);
	fPtr->fbMemoryScreenReserve = fbMaxScreenSize;

	xf86DrvMsg(scrnIndex, X_INFO,
		"reserve %d bytes for on screen frame buffer; total fb memory size %d bytes; offset of shadow buffer %d\n",
		fPtr->fbMemoryScreenReserve, fPtr->fbMemorySize, fbOffsetScreen2);

	fPtr->fbMemoryStart2 = NULL;
	if ((unsigned int)(fbOffsetScreen2 + fbMaxScreenSize) <= (unsigned int)fPtr->fbMemorySize) {
		fPtr->fbMemoryStart2 = fPtr->mFB.mFBStart + fbOffsetScreen2;
		fPtr->fbMemoryScreenReserve += fbOffsetScreen2;
	}
	else {
		xf86DrvMsg(scrnIndex, X_ERROR, "fb memory is not big enough to hold shadow buffer!\n");
		return FALSE;
	}

	if (!imxDisplayStartScreenInit(scrnIndex, pScreen)) {
		return FALSE;
	}
}

/* -------------------------------------------------------------------- */

static void
imxRemoveTrailingNewLines(char* str)
{
	int len = strlen(str);

	while ((len > 0) && ('\n' == str[len-1])) {

		str[--len] = '\0';
	}
}

/* -------------------------------------------------------------------- */

static ImxFbType
imxDisplayGetFrameBufferType(struct fb_fix_screeninfo* pFixInfo)
{
	if (0 == strcmp("mxc_epdc_fb", pFixInfo->id)) {
		return ImxFbTypeEPDC;
	}

	if (0 == strcmp("DISP3 BG", pFixInfo->id)) {
		return ImxFbTypeDISP3_BG;
	}

	if (0 == strcmp("DISP3 FG", pFixInfo->id)) {
		return ImxFbTypeDISP3_FG;
	}

	if (0 == strcmp("DISP3 BG - DI1", pFixInfo->id)) {
		return ImxFbTypeDISP3_BG_D1;
	}

	return ImxFbTypeUnknown;
}

/* -------------------------------------------------------------------- */

static const char* imxSysnodeNameMonitorInfoArray[] =
{
	"/sys/devices/platform/mxc_ddc.0/",
	"/sys/devices/platform/sii902x.0/"
};
static const int imxSysnodeNameMonitorInfoCount =
	sizeof(imxSysnodeNameMonitorInfoArray) /
		sizeof(imxSysnodeNameMonitorInfoArray[0]);

static xf86OutputStatus
imxDisplayGetCableState(int scrnIndex, const char* fbId)
{
#if 1
	return XF86OutputStatusConnected;
#else
	/* Loop through each sysnode entry looking for the cable state */
	/* for the frame buffer device matching the specified ID. */
	int iEntry;
	for (iEntry = 0; iEntry < imxSysnodeNameMonitorInfoCount; ++iEntry) {

		char sysnodeName[80];

		/* Look for this sysnode entry which contains the id */
		/* of the associated frame buffer device driver. */
		strcpy(sysnodeName, imxSysnodeNameMonitorInfoArray[iEntry]);
		strcat(sysnodeName, "fb_name");
		FILE* fp = fopen(sysnodeName, "r");
		if (NULL == fp) {

			continue;
		}

		/* The name of the frame buffer device */
		char linebuf[80] = "";
		const BOOL bNoName =
			(NULL == fgets(linebuf, sizeof(linebuf), fp));
		fclose(fp);
		if (bNoName || (0 != strncmp(linebuf, fbId, strlen(fbId)))) {

			continue;
		}

		/* Look for sysnode entry which contains cable state info. */
		strcpy(sysnodeName, imxSysnodeNameMonitorInfoArray[iEntry]);
		strcat(sysnodeName, "cable_state");
		fp = fopen(sysnodeName, "r");
		if (NULL == fp) {

			continue;
		}

		/* Read the line that contains the cable state. */
		char strCableState[80];
		strcpy(strCableState, "");
		const Bool bNoInfo =
			(NULL == fgets(strCableState, sizeof(strCableState), fp));
		fclose(fp);
		if (bNoInfo) {

			break;
		}
	
		imxRemoveTrailingNewLines(strCableState);

		/* Determine cable state from the string. */
		if (0 == strcmp(strCableState, "plugin")) {

			return XF86OutputStatusConnected;

		} else if (0 == strcmp(strCableState, "plugout")) {

#if 0
			return XF86OutputStatusDisconnected;
#else
			return XF86OutputStatusUnknown;
#endif
		}

		/* No need to keep looking. Found file we were looking for. */
		break;
	}

	return XF86OutputStatusUnknown;
#endif
}

static xf86MonPtr
imxDisplayGetEdid(ScrnInfoPtr pScrn, const char* fbId, Uchar edidDataBytes[],
			const int edidDataMaxBytes)
{
	/* Loop through each sysnode entry looking for the EDID info. */
	int iEntry;
	for (iEntry = 0; iEntry < imxSysnodeNameMonitorInfoCount; ++iEntry) {

		char sysnodeName[80];

		/* Look for this sysnode entry which contains the id */
		/* of the associated frame buffer device driver. */
		strcpy(sysnodeName, imxSysnodeNameMonitorInfoArray[iEntry]);
		strcat(sysnodeName, "fb_name");
		FILE* fp = fopen(sysnodeName, "r");
		if (NULL == fp) {

			continue;
		}

		/* The name of the frame buffer device */
		char linebuf[80] = "";
		const BOOL bNoName =
			(NULL == fgets(linebuf, sizeof(linebuf), fp));
		fclose(fp);
		if (bNoName || (0 != strncmp(linebuf, fbId, strlen(fbId)))) {

			continue;
		}

		/* Look for sysnode entry which contains cable state info. */
		strcpy(sysnodeName, imxSysnodeNameMonitorInfoArray[iEntry]);
		strcat(sysnodeName, "cable_state");
		fp = fopen(sysnodeName, "r");
		if (NULL == fp) {

			continue;
		}

		/* Read the line that contains the cable state. */
		char strCableState[80];
		strcpy(strCableState, "");
		const Bool bNoInfo =
			(NULL == fgets(strCableState, sizeof(strCableState), fp));
		fclose(fp);
		if (bNoInfo) {

			continue;
		}
	
		imxRemoveTrailingNewLines(strCableState);

		/* Determine cable state from the string. */
		if (0 != strcmp(strCableState, "plugin")) {

			continue;
		}

		/* Look for this sysnode entry which contains EDID info. */
		strcpy(sysnodeName, imxSysnodeNameMonitorInfoArray[iEntry]);
		strcat(sysnodeName, "edid");
		fp = fopen(sysnodeName, "r");
		if (NULL == fp) {

			continue;
		}

		/* The bytes in the sysnode entry are stored in */
		/* ASCII 0x%02x format. */
		unsigned int byte;
		int nBytes;
		for (nBytes = 0; nBytes < edidDataMaxBytes; ++nBytes) {

			if (1 != fscanf(fp, "%i", &byte)) {
				break;
			}

			edidDataBytes[nBytes] = byte;
		}
		fclose(fp);

		/* Were all the bytes successfully read? */
		if (edidDataMaxBytes != nBytes) {

			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   		"sysnode '%s' contains only %d of %d bytes\n",
				sysnodeName, nBytes, edidDataMaxBytes);

			continue;
		}

		/* Interpret the EDID monitor info. */
		xf86MonPtr pMonitor =
			xf86InterpretEDID(pScrn->scrnIndex, edidDataBytes);
		if (NULL == pMonitor) {

			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   		"cannot interpret EDID info in sysnode '%s'\n",
				sysnodeName);

			continue;
		}

		return pMonitor;
	}

	return NULL;
}

static DisplayModePtr
imxDisplayGetMonitorPreferredMode(DisplayModePtr modesList)
{
	DisplayModePtr mode, first = mode = modesList;

	if (NULL != mode) do {

		if (0 != (M_T_PREFERRED & mode->type)) {

			return mode;
		}

		mode = mode->next;
	} while (mode != NULL && mode != first);

	return NULL;
}

/* -------------------------------------------------------------------- */

static Bool
imxDisplaySetMode(ScrnInfoPtr pScrn, const char* fbDeviceName,
			const char* modeName)
{
	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);
	
	/* Access display private screen data */
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

	/* Can only change the mode if we have monitor modes available. */
	if (fPtr->edidModesAvail) {

		/* Create the name of the sysnode file that contains the */
		/* name of the currently selected mode. */
		char sysnodeName[80];
		sprintf(sysnodeName, "/sys/class/graphics/%s/mode",
				fbDeviceName);
		int fd = open(sysnodeName, O_RDWR);
		if (-1 == fd) {

			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"unable to open sysnode '%s':%s\n",
				sysnodeName, strerror(errno));
			return FALSE;
		}

		/* Make sure mode name has a newline at end on the write. */
		char validModeName[80];
		strcpy(validModeName, modeName);
		strcat(validModeName, "\n");

		/* Write the desired mode name */
		if (-1 == write(fd, validModeName, strlen(validModeName))) {
			
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"unable to write '%s' to sysnode '%s': %s\n",
				validModeName, sysnodeName, strerror(errno));
			return FALSE;
		}

		close(fd);

		/* Store the name of the mode that was set. */
		strcpy(fPtr->fbModeNameCurrent, modeName);
	}

	/* Access the fd for the FB driver */
	int fdDev = fbdevHWGetFD(pScrn);

	/* Query the FB fixed screen info */
	struct fb_fix_screeninfo fbFixScreenInfo;
	if (0 != ioctl(fdDev, FBIOGET_FSCREENINFO, &fbFixScreenInfo)) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"unable to get FSCREENINFO for mode '%s': %s\n",
			modeName, strerror(errno));
		return FALSE;
	}

	/* Query the FB variable screen info */
	struct fb_var_screeninfo fbVarScreenInfo;
	if (0 != ioctl(fdDev, FBIOGET_VSCREENINFO, &fbVarScreenInfo)) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"unable to get VSCREENINFO for mode '%s': %s\n",
			modeName, strerror(errno));
		return FALSE;
	}

	/* If the shadow memory is allocated, then we have some */
	/* adjustments to do. */
	if (fPtr->fbShadowAllocated) {

		/* How many bytes from start of 1st buffer to start */
		/* of 2nd buffer? */
		const int offsetBytes =
			imxPtr->fbMemoryStart2 - imxPtr->mFB.mFBStart;

		/* What should the yoffset by to start of 2nd buffer? */
		const int yoffset = offsetBytes / fbFixScreenInfo.line_length;

		/* What should virtual resolution be adjusted to */
		/* based on the 2 buffers? */
		const int vyres = yoffset * 2;

		/* pScrn->displayWidth: not display width in case of rotation. It is desktop width. Use fbFixScreenInfo.line_length */
		/* to calculate offset */
		fbVarScreenInfo.xoffset = offsetBytes - yoffset * fbFixScreenInfo.line_length;
		fbVarScreenInfo.yoffset = yoffset;
		fbVarScreenInfo.yres_virtual = vyres;

	/* If the shadow memory is not allocated, then we need to */
	/* reset any FB pan display back to (0,0). */
	} else {

		fbVarScreenInfo.xoffset = 0;
		fbVarScreenInfo.yoffset = 0;
		fbVarScreenInfo.xres_virtual = IMX_ALIGN(fbVarScreenInfo.xres, imxPtr->fbAlignWidth);
		fbVarScreenInfo.yres_virtual = IMX_ALIGN(fbVarScreenInfo.yres, imxPtr->fbAlignHeight);
	}

	/* Make the adjustments to the variable screen info. */
	if (0 != ioctl(fdDev, FBIOPUT_VSCREENINFO, &fbVarScreenInfo)) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"unable to set VSCREENINFO for mode '%s': %s\n",
			modeName, strerror(errno));
		return FALSE;
	}

	// re-mapping video memory (for ipu, it is needless)

	return TRUE;
}

// currently only overlay supports this feature
static Bool
imxDisplaySetUserMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);

	/* Access display private screen data */
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

	/* Access the fd for the FB driver */
	int fdDev = fbdevHWGetFD(pScrn);

	/* Query the FB fixed screen info */
	struct fb_fix_screeninfo fbFixScreenInfo;
	if (0 != ioctl(fdDev, FBIOGET_FSCREENINFO, &fbFixScreenInfo)) {
		return FALSE;
	}

	/* Query the FB variable screen info */
	struct fb_var_screeninfo fbVarScreenInfo;
	if (0 != ioctl(fdDev, FBIOGET_VSCREENINFO, &fbVarScreenInfo)) {
		return FALSE;
	}

	/* If the shadow memory is allocated, then we have some */
	/* adjustments to do. */
	if (fPtr->fbShadowAllocated) {
		const int fbBytesPerPixel = (pScrn->bitsPerPixel + 7) / 8;
		fbVarScreenInfo.xres = mode->HDisplay;
		fbVarScreenInfo.yres = mode->VDisplay;
		fbVarScreenInfo.xres_virtual = IMX_ALIGN(fbVarScreenInfo.xres, imxPtr->fbAlignWidth);
		fbVarScreenInfo.yres_virtual = IMX_ALIGN(fbVarScreenInfo.yres, imxPtr->fbAlignHeight);

		/* How many bytes from start of 1st buffer to start */
		/* of 2nd buffer? */
		const int offsetBytes =
			imxPtr->fbMemoryStart2 - imxPtr->mFB.mFBStart;

		/* What should the yoffset by to start of 2nd buffer? */
		const int yoffset = offsetBytes / (fbVarScreenInfo.xres_virtual * fbBytesPerPixel);

		/* What should virtual resolution be adjusted to */
		/* based on the 2 buffers? */
		const int vyres = yoffset * 2;

		fbVarScreenInfo.xoffset = offsetBytes - yoffset * fbVarScreenInfo.xres_virtual * fbBytesPerPixel;
		fbVarScreenInfo.yoffset = yoffset;

	/* If the shadow memory is not allocated, then we need to */
	/* reset any FB pan display back to (0,0). */
	} else {

		fbVarScreenInfo.xoffset = 0;
		fbVarScreenInfo.yoffset = 0;
		fbVarScreenInfo.xres = pScrn->virtualX;
		fbVarScreenInfo.yres = pScrn->virtualY;
		fbVarScreenInfo.xres_virtual = IMX_ALIGN(fbVarScreenInfo.xres, imxPtr->fbAlignWidth);
		fbVarScreenInfo.yres_virtual = IMX_ALIGN(fbVarScreenInfo.yres, imxPtr->fbAlignHeight);
	}

	/* timings */
	/* See xfree2fbdev_timing. XServer 1.14 does not support following flags:
	FB_VMODE_ODD_FLD_FIRST
	FB_SYNC_ON_GREEN
	FB_SYNC_EXT
	And FSL extensions (See video-mx3fb.h)
	FB_SYNC_OE_ACT_HIGH (0x80000000)
	FB_SYNC_CLK_INVERT (0x40000000)
	FB_SYNC_DATA_INVERT (0x20000000)
	FB_SYNC_CLK_IDLE_EN (0x10000000)
	FB_SYNC_SHARP_MODE (0x08000000)
	FB_SYNC_SWAP_RGB (0x04000000)
	FB_SYNC_CLK_SEL_EN (0x02000000)
	User mode is described through modeline so assume it is standard and recognized
	by xserver.
	*/
	fbVarScreenInfo.pixclock = mode->Clock ? 1000000000 / mode->Clock : 0;
	fbVarScreenInfo.left_margin = mode->HTotal - mode->HSyncEnd;
	fbVarScreenInfo.right_margin = mode->HSyncStart - mode->HDisplay;
	fbVarScreenInfo.upper_margin = mode->VTotal - mode->VSyncEnd;
	fbVarScreenInfo.lower_margin = mode->VSyncStart - mode->VDisplay;
	fbVarScreenInfo.hsync_len = mode->HSyncEnd - mode->HSyncStart;
	fbVarScreenInfo.vsync_len = mode->VSyncEnd - mode->VSyncStart;
	fbVarScreenInfo.vmode = 0;
	if(mode->Flags & V_INTERLACE)
		fbVarScreenInfo.vmode |= FB_VMODE_INTERLACED;
	if(mode->Flags & V_DBLSCAN)
		fbVarScreenInfo.vmode |= FB_VMODE_DOUBLE;
	fbVarScreenInfo.sync = 0;
	if(mode->Flags & V_PHSYNC)
		fbVarScreenInfo.sync |= FB_SYNC_HOR_HIGH_ACT;
	if(mode->Flags & V_PVSYNC)
		fbVarScreenInfo.sync |= FB_SYNC_VERT_HIGH_ACT;
	if(mode->Flags & V_PCSYNC)
		fbVarScreenInfo.sync |= FB_SYNC_COMP_HIGH_ACT;
	if(mode->Flags & V_BCAST)
		fbVarScreenInfo.sync |= FB_SYNC_BROADCAST;

	/* Make the adjustments to the variable screen info. */
	if (0 != ioctl(fdDev, FBIOPUT_VSCREENINFO, &fbVarScreenInfo)) {
		return FALSE;
	}

	// re-mapping video memory (for ipu, it is needless)

	return TRUE;
}

/* -------------------------------------------------------------------- */

static void
imxConvertFrameBufferTiming(struct fb_var_screeninfo *var, DisplayModePtr mode)
{
	mode->Clock = var->pixclock ? 1000000000/var->pixclock : 0;
	mode->HDisplay = var->xres;
	mode->HSyncStart = mode->HDisplay+var->right_margin;
	mode->HSyncEnd = mode->HSyncStart+var->hsync_len;
	mode->HTotal = mode->HSyncEnd+var->left_margin;
	mode->VDisplay = var->yres;
	mode->VSyncStart = mode->VDisplay+var->lower_margin;
	mode->VSyncEnd = mode->VSyncStart+var->vsync_len;
	mode->VTotal = mode->VSyncEnd+var->upper_margin;
	mode->Flags = 0;
	mode->Flags |= var->sync & FB_SYNC_HOR_HIGH_ACT ? V_PHSYNC : V_NHSYNC;
	mode->Flags |= var->sync & FB_SYNC_VERT_HIGH_ACT ? V_PVSYNC : V_NVSYNC;
	mode->Flags |= var->sync & FB_SYNC_COMP_HIGH_ACT ? V_PCSYNC : V_NCSYNC;
	if (var->sync & FB_SYNC_BROADCAST)
		mode->Flags |= V_BCAST;
	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED)
		mode->Flags |= V_INTERLACE;
	else if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE)
		mode->Flags |= V_DBLSCAN;
	mode->SynthClock = mode->Clock;
	mode->CrtcHDisplay = mode->HDisplay;
	mode->CrtcHSyncStart = mode->HSyncStart;
	mode->CrtcHSyncEnd = mode->HSyncEnd;
	mode->CrtcHTotal = mode->HTotal;
	mode->CrtcVDisplay = mode->VDisplay;
	mode->CrtcVSyncStart = mode->VSyncStart;
	mode->CrtcVSyncEnd = mode->VSyncEnd;
	mode->CrtcVTotal = mode->VTotal;
	mode->CrtcHAdjusted = FALSE;
	mode->CrtcVAdjusted = FALSE;
}

static DisplayModePtr
imxDisplayMatchFrameBufferMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);

	/* Access driver private screen display data */
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

	DisplayModePtr fbMode = fPtr->fbModesList;
	DisplayModePtr fbFirstMode = fbMode;

	if (NULL != fbMode) do {

		/* Check horizontal and vertical timing numbers. */
		if (mode->HDisplay == fbMode->HDisplay &&
			mode->HSyncStart == fbMode->HSyncStart &&
			mode->HSyncEnd == fbMode->HSyncEnd &&
			mode->HTotal == fbMode->HTotal &&
			mode->HSkew == fbMode->HSkew &&
			mode->VDisplay == fbMode->VDisplay &&
			mode->VSyncStart == fbMode->VSyncStart &&
			mode->VSyncEnd == fbMode->VSyncEnd &&
			mode->VTotal == fbMode->VTotal &&
			mode->VScan == fbMode->VScan &&
			abs(mode->Clock - fbMode->Clock) < CLOCK_TOLERANCE) {

			/* Check horizontal and vertical sync. */
			int flags = mode->Flags ^ fbMode->Flags;
			if ((0 == (flags & V_PHSYNC)) &&
				(0 == (flags & V_NHSYNC)) && 
				(0 == (flags & V_PVSYNC)) &&
				(0 == (flags & V_NVSYNC))) {

				return fbMode;
			}
		}

		fbMode = fbMode->next;

	} while ((NULL != fbMode) && (fbMode != fbFirstMode));

	return NULL;
}

static ModeStatus
imxDisplayFrameBufferModeSupport(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
	return (NULL != imxDisplayMatchFrameBufferMode(pScrn, mode))
			? MODE_OK
			: MODE_NOMODE;
}

/* -------------------------------------------------------------------- */

static Bool
imxDisplayIsValidMode(DisplayModePtr modesList, DisplayModePtr mode)
{
	while (NULL != modesList) {

		DisplayModePtr testMode = modesList;
		modesList = modesList->next;

		if (0 == strcmp(testMode->name, mode->name)) {
			return TRUE;
		}
	}

	return FALSE;
}

static DisplayModePtr
imxDisplayGetCurrentMode(ScrnInfoPtr pScrn, int fd, const char* modeName)
{
	/* Query the frame buffer variable screen info. */
	struct fb_var_screeninfo fbVarScreenInfo;
	if (0 != ioctl(fd, FBIOGET_VSCREENINFO, &fbVarScreenInfo)) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"unable to get VSCREENINFO for mode '%s': %s\n",
			modeName, strerror(errno));
		return NULL;
	}
		
	/* Allocate a new mode structure. */
	DisplayModePtr mode = malloc(sizeof(DisplayModeRec));

	/* Transfer info from fbdev var screen info */
	/* into the X DisplayModeRec. */
	imxConvertFrameBufferTiming(&fbVarScreenInfo, mode);

	/* Add the new mode to the list. */
	mode->type = M_T_DRIVER;
	mode->status = MODE_OK;
	mode->name = xstrdup(modeName);
	mode->prev = NULL;
	mode->next = NULL;

	imxStoreSyncFlags(pScrn, modeName, fbVarScreenInfo.sync);

	return mode;
}

static DisplayModePtr
imxDisplayGetModes(ScrnInfoPtr pScrn, const char* fbDeviceName)
{
	FILE* fpModes = NULL;
	int fdDev = -1;
	DisplayModePtr modesList = NULL;
	Bool savedVarScreenInfo = FALSE;
	struct fb_var_screeninfo fbVarScreenInfo;

	/* Access the frame buffer device. */
	fdDev = fbdevHWGetFD(pScrn);
	if (-1 == fdDev) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
	   		"frame buffer device not available or initialized\n");
		goto errorGetModes;
	}

	/* Query the FB variable screen info */
	if (0 != ioctl(fdDev, FBIOGET_VSCREENINFO, &fbVarScreenInfo)) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"unable to get FB VSCREENINFO for current mode: %s\n",
			strerror(errno));
		goto errorGetModes;
	}
	savedVarScreenInfo = TRUE;

	/* Create the name of the sysnode file that contains the */
	/* names of all the frame buffer modes. */
	char sysnodeName[80];
	sprintf(sysnodeName, "/sys/class/graphics/%s/modes", fbDeviceName);
	fpModes = fopen(sysnodeName, "r");
	if (NULL == fpModes) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
	   		"unable to open sysnode '%s':%s \n",
			sysnodeName, strerror(errno));
		goto errorGetModes;
	}

	/* Create name for the frame buffer device. */
	char fullDeviceName[80];
	strcpy(fullDeviceName, "/dev/");
	strcat(fullDeviceName, fbDeviceName);

	/* Turn on frame buffer blanking. */
	if (0 != ioctl(fdDev, FBIOBLANK, FB_BLANK_NORMAL)) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
	   		"unable to blank frame buffer device '%s': %s\n",
			fullDeviceName, strerror(errno));
		goto errorGetModes;
	}

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"printing discovered frame buffer '%s' supported modes:\n",
		fbDeviceName);

	/* Iterate over all the modes in the frame buffer list. */
	char modeName[80];
	while (NULL != fgets(modeName, sizeof(modeName), fpModes)) {

		imxRemoveTrailingNewLines(modeName);

		/* Attempt to set the mode */
		if (!imxDisplaySetMode(pScrn, fbDeviceName, modeName)) {

			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
	   			"unable to set frame buffer mode '%s'\n",
				modeName);
			continue;
		}

		DisplayModePtr mode =
			imxDisplayGetCurrentMode(pScrn, fdDev, modeName);

        /* Check whether meet XRandR requirement (SL/SX: some modes are not supported) */
        if (!imxDisplayCheckModeXRandR(pScrn)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "Mode '%s' is eliminated from XRandR support\n",
                modeName);
            continue;
        }

		if ((NULL != mode) &&
			(mode->HDisplay > 0) &&
				(mode->VDisplay > 0)) {

			/* device preferred mode ? */
			imxSetPreferFlag(pScrn, mode);

			xf86PrintModeline(pScrn->scrnIndex, mode);
			modesList = xf86ModesAdd(modesList, mode);
		}
	}

	/* if no modes found, use builtin mode. Builtin mode name is 'current', which will result wrong result for mode matching 
	and searching.
	*/
	if(modesList == NULL) {
		/* Add current builtin mode */
		DisplayModePtr builtinMode = fbdevHWGetBuildinMode(pScrn);
		xf86PrintModeline(pScrn->scrnIndex, builtinMode);
		modesList = xf86ModesAdd(modesList, xf86DuplicateMode(builtinMode));
	}

errorGetModes:

	/* Close file with list of modes. */
	if (NULL != fpModes) {

		fclose(fpModes);
	}

	/* Restore FB back to the current mode */
	if (savedVarScreenInfo) {

		if (0 != ioctl(fdDev, FBIOPUT_VSCREENINFO, &fbVarScreenInfo)) {
	
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"unable to restore FB VSCREENINFO: %s\n",
				strerror(errno));
		}
	}

	/* Turn off frame buffer blanking */
	if (-1 != fdDev) {

		ioctl(fdDev, FBIOBLANK, FB_BLANK_UNBLANK);
	}

	/* Remove any duplicate modes found. */
	modesList = xf86PruneDuplicateModes(modesList);

	return modesList;
}

static void
imxDisplayDeleteModes(DisplayModePtr modesList)
{
	while (NULL != modesList) {

		DisplayModePtr mode = modesList;

		modesList = mode->next;
		if (modesList == mode) {
			modesList = NULL;
		}

		if (NULL != mode->name) {
			free(mode->name);
		}
		free(mode);
	}
}

/* -------------------------------------------------------------------- */
/* This function is always called; but imxDisplaySetMode/imxDisplaySetUserMode
only is called when mode is changing. */
static Bool
imxCrtcResize(ScrnInfoPtr pScrn, int width, int height)
{
	/* Access the screen. */
	ScreenPtr pScreen = pScrn->pScreen;
	if (NULL == pScreen) {

		return FALSE;
	}

	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);

	/* Access the screen pixmap */
	PixmapPtr pScreenPixmap = (*pScreen->GetScreenPixmap)(pScreen);
	if (NULL == pScreenPixmap) {

		return FALSE;
	}

	pScrn->virtualX = width;
	pScrn->virtualY = height;
	pScrn->displayWidth = IMX_ALIGN(width, imxPtr->fbAlignWidth);

	const int bytesPerPixel = (pScrn->bitsPerPixel + 7) / 8;
	const int stride = pScrn->displayWidth * bytesPerPixel;

	/* Resize the screen pixmap to new size */
	(*pScreen->ModifyPixmapHeader)(
		pScreenPixmap,
		width,
		height,
		-1,			/* same depth */
		-1, 			/* same bitsperpixel */
		stride,			/* devKind = stride */
		NULL);			/* same memory ptr */

	/* update displayWidth to new value set by gpu. displayWidth will be used to 
	update fb device virtual x resolution in imxDisplaySetMode
	*/
	pScrn->displayWidth = pScreenPixmap->devKind / bytesPerPixel;

	return TRUE;
}

static void
imxCrtcDPMS(xf86CrtcPtr crtc, int mode)
{
   /**
    * Turns the crtc on/off, or sets intermediate power levels if available.
    *
    * Unsupported intermediate modes drop to the lower power setting.  If the
    * mode is DPMSModeOff, the crtc must be disa be safe to call mode_set.
    */

	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = crtc->scrn;

	/* Access the frame buffer driver */
	int fd = fbdevHWGetFD(pScrn);
	if (-1 != fd) {

		/* Enable power */
		if (DPMSModeOn == mode) {

			ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);

		/* Unsupported intermediate modes drop to lower power setting */
		} else {

			ioctl(fd, FBIOBLANK, FB_BLANK_NORMAL);
		}
	}
}

static void
imxCrtcSave(xf86CrtcPtr crtc)
{
   /**
    * Saves the crtc's state for restoration on VT switch.
    */

	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = crtc->scrn;

	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);
	
	/* Access display private screen data */
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

	/* If we don't know the monitor modes, then just remember */
	/* the current built in mode */
	if (!fPtr->edidModesAvail) {

		fbdevHWSave(pScrn);
	}
}

static void
imxCrtcRestore(xf86CrtcPtr crtc)
{
   /**
    * Restore's the crtc's state at VT switch.
    */

	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = crtc->scrn;

	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);
	
	/* Access display private screen data */
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

	/* If we don't have monitor modes available ... */
	if (!fPtr->edidModesAvail) {

		fbdevHWRestore(pScrn);

	} else {
	
		/* Restore the current mode if it was saved. */
		if (0 != strlen(fPtr->fbModeNameCurrent)) {

			imxDisplaySetMode(pScrn, imxPtr->fbDeviceName,
						fPtr->fbModeNameCurrent);
		}
	}
}

static Bool
imxCrtcLock(xf86CrtcPtr crtc)
{
    /**
     * Lock CRTC prior to mode setting, mostly for DRI.
     * Returns whether unlock is needed
     */

	/* nothing to do, but return FALSE since unlock is not needed */
	return FALSE;
}

static void
imxCrtcUnlock(xf86CrtcPtr crtc)
{
    /**
     * Unlock CRTC after mode setting, mostly for DRI
     */

	/* nothing to do */
}

static Bool
imxCrtcModeFixup(xf86CrtcPtr crtc, DisplayModePtr mode, DisplayModePtr adjMode)
{
    /**
     * Callback to adjust the mode to be set in the CRTC.
     *
     * This allows a CRTC to adjust the clock or even the entire set of
     * timings, which is used for panels with fixed timings or for
     * buses with clock limitations.
     */

	/* nothing to do */
	return TRUE;
}

static void
imxCrtcPrepare(xf86CrtcPtr crtc)
{
    /**
     * Prepare CRTC for an upcoming mode set.
     */

	/* nothing to do */
}

static void
imxCrtcModeSet(xf86CrtcPtr crtc, DisplayModePtr mode, DisplayModePtr adjMode,
		int x, int y)
{
    /**
     * Callback for setting up a video mode after fixups have been made.
     */

	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = crtc->scrn;

	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);
	
	/* Find the matching mode. */
	DisplayModePtr fbMode = imxDisplayMatchFrameBufferMode(pScrn, mode);

	if (NULL != fbMode) {

		imxDisplaySetMode(pScrn, imxPtr->fbDeviceName, fbMode->name);

		/* record last video mode for later hdmi hot plugout/in */
		if(imxPtr->lastVideoMode) {
			xf86DeleteMode(&imxPtr->lastVideoMode, imxPtr->lastVideoMode);
		}
		imxPtr->lastVideoMode = xf86DuplicateMode(fbMode);

	}
	else {
		imxDisplaySetUserMode(pScrn, mode);

		/* record last video mode for later hdmi hot plugout/in */
		if(imxPtr->lastVideoMode) {
			xf86DeleteMode(&imxPtr->lastVideoMode, imxPtr->lastVideoMode);
		}
		imxPtr->lastVideoMode = xf86DuplicateMode(mode);
	}

//    crtc->desiredMode = *mode;

	OnCrtcModeChanged(pScrn);
}

static void
imxCrtcCommit(xf86CrtcPtr crtc)
{
    /**
     * Commit mode changes to a CRTC
     */

	/* TODO - unblank display after changing modes? */
}

static void*
imxCrtcShadowAllocate(xf86CrtcPtr crtc, int width, int height)
{
    /**
     * Allocate the shadow area, delay the pixmap creation until needed
     */

	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = crtc->scrn;

	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);

	/* Access driver private screen display data */
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

	/* Make sure memory for 2nd buffer is there and not */
	/* already allocated. */
	if ((NULL != imxPtr->fbMemoryStart2) && !fPtr->fbShadowAllocated) {

		fPtr->fbShadowAllocated = TRUE;
		return imxPtr->fbMemoryStart2;
	}

	return NULL;
}

static PixmapPtr
imxCrtcShadowCreate(xf86CrtcPtr crtc, void* data, int width, int height)
{
    /**
     * Create shadow pixmap for rotation support
     */

	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = crtc->scrn;

	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);

	/* Check if memory allocated. */
	if (NULL == data) {

		data = imxCrtcShadowAllocate(crtc, width, height);
		if (NULL == data) {

			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
					"Could not allocate shadow pixmap\n");
			return NULL;
		}
	}

	/* Compute the pitch for the pixmap. */
	const int bytesPerPixel = (pScrn->bitsPerPixel + 7) / 8;
	const int pitch =
		IMX_ALIGN(width, imxPtr->fbAlignWidth) * bytesPerPixel;

	PixmapPtr pPixmap =
		GetScratchPixmapHeader(
			pScrn->pScreen,
			width, height,
			pScrn->depth,
			pScrn->bitsPerPixel,
			pitch,
			data);

	return pPixmap;
}

static void
imxCrtcShadowDestroy(xf86CrtcPtr crtc, PixmapPtr pPixmap, void* data)
{
    /**
     * Destroy shadow pixmap
     */

	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = crtc->scrn;

	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);

	/* Access driver private screen display data */
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

	/* Mark the shadow memory as being available */
	if (imxPtr->fbMemoryStart2 == data) {

		fPtr->fbShadowAllocated = FALSE;
	}

	/* Release the pixmap */
	if (NULL != pPixmap) {

		FreeScratchPixmapHeader(pPixmap);
	}
}

static void
imxCrtcDestroy(xf86CrtcPtr crtc)
{
	/* TODO */
}

/* -------------------------------------------------------------------- */

static void
imxOutputCreateResources(xf86OutputPtr output)
{
	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = output->scrn;

	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);

	/* Access driver private screen display data */
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

	/* Create atom for accessing EDID data */
	static const char AtomNameEdid[] = "EDID";
	fPtr->atomEdid = MakeAtom(AtomNameEdid, strlen(AtomNameEdid), TRUE);
}

static void
imxOutputDPMS(xf86OutputPtr output, int mode)
{
	/* nothing to do */
}

static void
imxOutputSave(xf86OutputPtr output)
{
	/* TODO */
}

static void
imxOutputRestore(xf86OutputPtr output)
{
	/* TODO */
}

static int
imxOutputModeValid(xf86OutputPtr output, DisplayModePtr mode)
{
	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = output->scrn;

	return MODE_OK;
	//return imxDisplayFrameBufferModeSupport(pScrn, mode);
}

static Bool
imxOutputModeFixup(xf86OutputPtr output, DisplayModePtr mode,
			DisplayModePtr adjMode)
{
	/* nothing to do */

	return TRUE;
}

static void
imxOutputPrepare(xf86OutputPtr output)
{
	/* nothing to do */
}

static void
imxOutputModeSet(xf86OutputPtr output, DisplayModePtr mode,
			DisplayModePtr adjMode)
{
    /**
     * Callback for setting up a video mode after fixups have been made.
     *
     * This is only called while the output is disabled.  The dpms callback
     * must be all that's necessary for the output, to turn the output on
     * after this function is called.
     */

	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = output->scrn;

	/* Enable the output */
	xf86DPMSSet(pScrn, DPMSModeOn, 0);
}

static void
imxOutputCommit(xf86OutputPtr output)
{
	/* nothing to do */
}

static xf86OutputStatus
imxOutputDetect(xf86OutputPtr output)
{
	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = output->scrn;

	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);

	return imxDisplayGetCableState(pScrn->scrnIndex, imxPtr->fbId);
}

static DisplayModePtr
imxOutputGetModes(xf86OutputPtr output)
{
	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = output->scrn;

	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);

	/* Access driver private screen display data */
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

    if(fPtr->fbModesList) {
        return xf86DuplicateModes(pScrn, fPtr->fbModesList);
    }

    DisplayModePtr builtinMode = fbdevHWGetBuildinMode(pScrn);
    xf86PrintModeline(pScrn->scrnIndex, builtinMode);
    DisplayModePtr modesList = xf86DuplicateMode(builtinMode);
    return modesList;
}

static void
imxOutputDestroy(xf86OutputPtr output)
{
	/* TODO */
}

static Bool
imxOutputGetProperty(xf86OutputPtr output, Atom property)
{
    /**
     * Callback to get an updated property value
     */

	/* Access the associated screen info. */
	ScrnInfoPtr pScrn = output->scrn;

	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);

	/* Access driver private screen display data */
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

	/* Request for raw EDID data? */
	if (property == fPtr->atomEdid) {

		RRChangeOutputProperty(
			output->randr_output,		/* RROutputPtr */
			property,			/* Atom property */
			XA_INTEGER,			/* Atom type */
			8,				/* int format */
			PropModeReplace,		/* int mode */
			sizeof(fPtr->edidDataBytes),	/* unsigned len */
			fPtr->edidDataBytes,		/* pointer value */
			FALSE,				/* Bool sendEvent? */
			TRUE);				/* Bool pending */

		return TRUE;
	}

	return FALSE;
}


/* -------------------------------------------------------------------- */

static void
imxDisplayGetPreInitMaxSize(ScrnInfoPtr pScrn, int* pMaxWidth, int* pMaxHeight)
{
	/* Access driver private screen data */
	ImxPtr imxPtr = IMXPTR(pScrn);

	/* Access display private screen data */
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

	if (NULL != pMaxWidth) {

		*pMaxWidth = fPtr->fbMaxWidth;
	}

	if (NULL != pMaxHeight) {

		*pMaxHeight = fPtr->fbMaxHeight;
	}
}

Bool
imxDisplayPreInit(ScrnInfoPtr pScrn)
{
    VivPtr vPtr = GET_VIV_PTR(pScrn);
    int fd = fbdevHWGetFD(pScrn);
    ImxPtr imxPtr = vPtr;

    /*****************************************************************/
    /* retrieve fb id */
    /*****************************************************************/
	struct fb_fix_screeninfo fbFixScreenInfo;
	if (0 != ioctl(fd,FBIOGET_FSCREENINFO,(void*)(&fbFixScreenInfo))) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "FBIOGET_FSCREENINFO: %s\n", strerror(errno));
        TRACE_EXIT(FALSE);
	}

	strcpy(vPtr->fbId, fbFixScreenInfo.id);

    /*****************************************************************/
    /* set ImxDisplayRec */
    /*****************************************************************/
	/* Private data structure must not already be in use. */
	if (NULL != imxPtr->displayPrivate) {
		return FALSE;
	}
	
	/* Allocate memory for display private data */
	imxPtr->displayPrivate = calloc(sizeof(ImxDisplayRec), 1);
	if (NULL == imxPtr->displayPrivate) {
		return FALSE;
	}
	ImxDisplayPtr fPtr = IMXDISPLAYPTR(imxPtr);

	/* Initialize display private data structure. */
	fPtr->crtcPtr = NULL;
	fPtr->outputPtr = NULL;
	fPtr->atomEdid = 0;
	fPtr->fbShadowAllocated = FALSE;
	fPtr->edidModesAvail = TRUE;
	strcpy(fPtr->fbModeNameCurrent, "");

	/* Set video buffer */
    /*****************************************************************/
	/* set virtual size to reserve a big enough buffer */
    /*****************************************************************/
	struct fb_var_screeninfo fbVarScreenInfo;
	if (0 != ioctl(fd, FBIOGET_VSCREENINFO, &fbVarScreenInfo)) {
		return FALSE;
	}
	// user may create a mode which is larger than native mode(s)
	// SL/SX does not support larger xres_virtual; so we extend yres_virtual only
	const int max_algined_width = IMX_ALIGN(1920, imxPtr->fbAlignWidth);
	const int max_aligned_height = IMX_ALIGN(1080, imxPtr->fbAlignHeight);
	const int max_size = max_algined_width * max_aligned_height * 2;
	fbVarScreenInfo.yres_virtual = max_size / fbVarScreenInfo.xres_virtual + 2;
	fbVarScreenInfo.bits_per_pixel = pScrn->bitsPerPixel;

	if (0 != ioctl(fd, FBIOPUT_VSCREENINFO, &fbVarScreenInfo)) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			"unable to support largest resolution (%s)", strerror(errno));
		return FALSE;
	}

	/* Access all the modes supported by frame buffer driver. */
	fPtr->fbModesList = imxDisplayGetModes(pScrn, imxPtr->fbDeviceName);

	/* Compute the range of sizes supported by frame buffer. */
	if (NULL != fPtr->fbModesList) {

		DisplayModePtr mode = fPtr->fbModesList;

		fPtr->fbMinWidth = mode->HDisplay;
		fPtr->fbMaxWidth = mode->HDisplay;
		fPtr->fbMinHeight = mode->VDisplay;
		fPtr->fbMaxHeight = mode->VDisplay;

		while (NULL != (mode = mode->next)) {

			if (mode->HDisplay < fPtr->fbMinWidth) {

				fPtr->fbMinWidth = mode->HDisplay;

			} else if (mode->HDisplay > fPtr->fbMaxWidth) {

				fPtr->fbMaxWidth = mode->HDisplay;
			}

			if (mode->VDisplay < fPtr->fbMinHeight) {

				fPtr->fbMinHeight = mode->VDisplay;

			} else if (mode->VDisplay > fPtr->fbMaxHeight) {

				fPtr->fbMaxHeight = mode->VDisplay;
			}
		}

	/* If modes not available from frame buffer, then use builtin mode */
	} else {

		DisplayModePtr mode = fbdevHWGetBuildinMode(pScrn);

		fPtr->fbMinWidth = mode->HDisplay;
		fPtr->fbMaxWidth = mode->HDisplay;
		fPtr->fbMinHeight = mode->VDisplay;
		fPtr->fbMaxHeight = mode->VDisplay;
	}

	/* Initialize display private data structure. */
	fPtr->crtcPtr = NULL;
	fPtr->outputPtr = NULL;

	fPtr->imxCrtcConfigFuncs.resize = imxCrtcResize;

	xf86CrtcConfigInit(pScrn, &fPtr->imxCrtcConfigFuncs);

    /* to support XRandR, set larger max size and smaller min size */
	xf86CrtcSetSizeRange(
		pScrn,
		240, // overlay default size: 240x320
		240,
		8192,
		8192);

	/* Establish CRTC callbacks */
	fPtr->imxCrtcFuncs.dpms = imxCrtcDPMS;
	fPtr->imxCrtcFuncs.save = imxCrtcSave;
	fPtr->imxCrtcFuncs.restore = imxCrtcRestore;
	fPtr->imxCrtcFuncs.lock = imxCrtcLock;
	fPtr->imxCrtcFuncs.unlock = imxCrtcUnlock;
	fPtr->imxCrtcFuncs.mode_fixup = imxCrtcModeFixup;
	fPtr->imxCrtcFuncs.prepare = imxCrtcPrepare;
	fPtr->imxCrtcFuncs.mode_set = imxCrtcModeSet;
	fPtr->imxCrtcFuncs.commit = imxCrtcCommit;
//	fPtr->imxCrtcFuncs.gamma_set = imxCrtcGammaSet;
	fPtr->imxCrtcFuncs.shadow_allocate = imxCrtcShadowAllocate;
	fPtr->imxCrtcFuncs.shadow_create = imxCrtcShadowCreate;
	fPtr->imxCrtcFuncs.shadow_destroy = imxCrtcShadowDestroy;
	fPtr->imxCrtcFuncs.destroy = imxCrtcDestroy;

	/* Allocate and initialize CRTC */
	fPtr->crtcPtr = xf86CrtcCreate(pScrn, &fPtr->imxCrtcFuncs);
	if (NULL == fPtr->crtcPtr) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"xf86CrtcCreate failed\n");
		return FALSE;
	}

	/* Establish output callbacks. */
	fPtr->imxOutputFuncs.create_resources = imxOutputCreateResources;
	fPtr->imxOutputFuncs.dpms = imxOutputDPMS;
	fPtr->imxOutputFuncs.save = imxOutputSave;
	fPtr->imxOutputFuncs.restore = imxOutputRestore;
	fPtr->imxOutputFuncs.mode_valid = imxOutputModeValid;
	fPtr->imxOutputFuncs.mode_fixup = imxOutputModeFixup;
	fPtr->imxOutputFuncs.prepare = imxOutputPrepare;
	fPtr->imxOutputFuncs.mode_set = imxOutputModeSet;
	fPtr->imxOutputFuncs.commit = imxOutputCommit;
	fPtr->imxOutputFuncs.detect = imxOutputDetect;
	fPtr->imxOutputFuncs.get_modes = imxOutputGetModes;
#ifdef RANDR_13_INTERFACE
	fPtr->imxOutputFuncs.get_property = imxOutputGetProperty;
#endif
	fPtr->imxOutputFuncs.destroy = imxOutputDestroy;

	/* Allocate and initialize output */
	fPtr->outputPtr =
		xf86OutputCreate(pScrn, &fPtr->imxOutputFuncs, imxPtr->fbId);
	if (NULL == fPtr->outputPtr) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"xf86OutputCreate failed\n");
		return FALSE;
	}
	fPtr->outputPtr->possible_crtcs = 1;

	/* Compute initial configuration */
	const Bool bCanGrow = TRUE;
	if (!xf86InitialConfiguration(pScrn, bCanGrow)) {

		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"xf86InitialConfiguration failed\n");
		return FALSE;
	}


	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		"imxDisplayPreInit: virtual set %d x %d, display width %d\n",
		pScrn->virtualX, pScrn->virtualY, pScrn->displayWidth);

	return TRUE;
}

static Bool
imxDisplayStartScreenInit(int scrnIndex, ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	ImxPtr fPtr = IMXPTR(pScrn);

	if (!xf86SetDesiredModes(pScrn)) {

		xf86DrvMsg(scrnIndex, X_ERROR, "mode initialization failed\n");
		return FALSE;
	}
/*
	if (!fbdevHWModeInit(pScrn, pScrn->currentMode)) {

		xf86DrvMsg(scrnIndex, X_ERROR, "mode initialization failed\n");
		return FALSE;
	}
*/
    /* now video ram size is change */
    pScrn->videoRam = fbdevHWGetVidmem(pScrn);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hardware: %s (video memory:"
            " %dkB)\n", fbdevHWGetName(pScrn), pScrn->videoRam / 1024);

	pScrn->displayWidth =
		fbdevHWGetLineLength(pScrn) / (pScrn->bitsPerPixel / 8);

	xf86SaveScreen(pScreen, SCREEN_SAVER_ON);

	return TRUE;
}

Bool
imxDisplayFinishScreenInit(int scrnIndex, ScreenPtr pScreen)
{
	/* Completes the screen initialization for outputs and CRTCs */
	if (!xf86CrtcScreenInit(pScreen)) {
		xf86DrvMsg(scrnIndex, X_ERROR, "xf86CrtcScreenInit failed\n");
		return FALSE;
	}

	/* All DPMS mode switching will be managed by using the dpms */
	/* DPMS functions provided by the outputs and CRTCs */
//	xf86DPMSInit(pScreen, xf86DPMSSet, 0);

	return TRUE;
}

/* -------------------------------------------------------------------- */

Bool
imxDisplaySwitchMode(SWITCH_MODE_ARGS_DECL)
{
#ifndef XF86_SCRN_INTERFACE
	ScrnInfoPtr pScrn = xf86Screens[arg];
#else
	ScrnInfoPtr pScrn = arg;
#endif
    // deprecated?

	return xf86SetSingleMode(pScrn, mode, RR_Rotate_0);
}

void
imxDisplayAdjustFrame(ADJUST_FRAME_ARGS_DECL)
{
}

Bool
imxDisplayEnterVT(VT_FUNC_ARGS_DECL)
{
#ifndef XF86_SCRN_INTERFACE
	ScrnInfoPtr pScrn = xf86Screens[arg];
#else
	ScrnInfoPtr pScrn = arg;
#endif

	return xf86SetDesiredModes(pScrn);
}

void
imxDisplayLeaveVT(VT_FUNC_ARGS_DECL)
{
#ifndef XF86_SCRN_INTERFACE
	ScrnInfoPtr pScrn = xf86Screens[arg];
#else
	ScrnInfoPtr pScrn = arg;
#endif

	xf86RotateFreeShadow(pScrn);

	xf86_hide_cursors(pScrn);
}

ModeStatus
imxDisplayValidMode(VALID_MODE_DECL)
{
#ifndef XF86_SCRN_INTERFACE
	ScrnInfoPtr pScrn = xf86Screens[arg];
#else
	ScrnInfoPtr pScrn = arg;
#endif

	if (mode->Flags & V_INTERLACE) {
		if (verbose) {
			xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
				   "Removing interlaced mode \"%s\"\n",
				   mode->name);
		}
		return MODE_BAD;
	}
	return MODE_OK;
}

#ifndef SUSPEND_SLEEP
#define SUSPEND_SLEEP 0
#endif
#ifndef RESUME_SLEEP
#define RESUME_SLEEP 0
#endif

/*
 * This function is only required if we need to do anything differently from
 * DoApmEvent() in common/xf86PM.c, including if we want to see events other
 * than suspend/resume.
 */
Bool
imxPMEvent(PM_EVENT_DECL)
{
#ifndef XF86_SCRN_INTERFACE
	ScrnInfoPtr pScrn = xf86Screens[arg];
#else
	ScrnInfoPtr pScrn = arg;
#endif
	ImxPtr fPtr = IMXPTR(pScrn);

	switch (event) {
	case XF86_APM_SYS_SUSPEND:
	case XF86_APM_CRITICAL_SUSPEND:	/*do we want to delay a critical suspend? */
	case XF86_APM_USER_SUSPEND:
	case XF86_APM_SYS_STANDBY:
	case XF86_APM_USER_STANDBY:
		if (!undo && !fPtr->suspended) {
			pScrn->LeaveVT(VT_FUNC_ARGS(0));
			fPtr->suspended = TRUE;
			sleep(SUSPEND_SLEEP);
		} else if (undo && fPtr->suspended) {
			sleep(RESUME_SLEEP);
			pScrn->EnterVT(VT_FUNC_ARGS(0));
			fPtr->suspended = FALSE;
		}
		break;
	case XF86_APM_STANDBY_RESUME:
	case XF86_APM_NORMAL_RESUME:
	case XF86_APM_CRITICAL_RESUME:
		if (fPtr->suspended) {
			sleep(RESUME_SLEEP);
			pScrn->EnterVT(VT_FUNC_ARGS(0));
			fPtr->suspended = FALSE;
			/*
			 * Turn the screen saver off when resuming.  This seems to be
			 * needed to stop xscreensaver kicking in (when used).
			 *
			 * XXX DoApmEvent() should probably call this just like
			 * xf86VTSwitch() does.  Maybe do it here only in 4.2
			 * compatibility mode.
			 */
			SaveScreens(SCREEN_SAVER_FORCER, ScreenSaverReset);
		}
		break;
		/* This is currently used for ACPI */
	case XF86_APM_CAPABILITY_CHANGED:
		ErrorF("Vivante PMEvent: Capability change\n");

		SaveScreens(SCREEN_SAVER_FORCER, ScreenSaverReset);

		break;
	default:
		ErrorF("Vivante PMEvent: received APM event %d\n", event);
	}
	return TRUE;
}

void imxInitSyncFlagsStorage(ScrnInfoPtr pScrn)
{
    ImxPtr fPtr = IMXPTR(pScrn);
    memset(fPtr->fbSync, 0, sizeof(fPtr->fbSync));
}

void imxFreeSyncFlagsStorage(ScrnInfoPtr pScrn)
{
    ImxPtr fPtr = IMXPTR(pScrn);
    int i;
    for(i=0; i<MAX_MODES_SUPPORTED; i++)
    {
        if(fPtr->fbSync[i].modeName)
        {
            free(fPtr->fbSync[i].modeName);
            fPtr->fbSync[i].modeName = NULL;
        }
    }
}

Bool imxStoreSyncFlags(ScrnInfoPtr pScrn, const char *modeName, unsigned int value)
{
    ImxPtr fPtr = IMXPTR(pScrn);
    int i;

    // is there a duplicate?
    for(i=0; i<MAX_MODES_SUPPORTED; i++)
    {
        if(fPtr->fbSync[i].modeName == NULL)
            break;

        if(strcmp(fPtr->fbSync[i].modeName, modeName) != 0)
        {
            continue;
        }
        else
        {
            // find duplicate; do not overwrite
            return TRUE;
        }
    }

    // insert a new entry

    if(i == MAX_MODES_SUPPORTED)
    {
        return FALSE;
    }
    else
    {
        fPtr->fbSync[i].modeName = strdup(modeName);
        fPtr->fbSync[i].syncFlags = value;
        return TRUE;
    }
}

Bool imxLoadSyncFlags(ScrnInfoPtr pScrn, const char *modeName, unsigned int *pSyncFlags)
{
    ImxPtr fPtr = IMXPTR(pScrn);
    int i;

    for(i=0; i<MAX_MODES_SUPPORTED; i++)
    {
        if(fPtr->fbSync[i].modeName == NULL)
            break;

        if(strcmp(fPtr->fbSync[i].modeName, modeName) != 0)
        {
            continue;
        }
        else
        {
            *pSyncFlags = fPtr->fbSync[i].syncFlags;
            return TRUE;
        }
    }

    return FALSE;
}

/* Get device preferred video mode, not monitor preferred. Must be called
 * at xserver start up (it is not necessary to be same as kernel command line,
 * considering xserver be configured to a new mode and restart xserverr
 */
Bool imxGetDevicePreferredMode(ScrnInfoPtr pScrn)
{
    ImxPtr fPtr = IMXPTR(pScrn);
    char sysnodeName[80];
    char modeName[64];
    FILE *fpMode;
    Bool got = FALSE;
    int fd = fbdevHWGetFD(pScrn);

    // Turn on frame buffer blanking to setup sys node mode
    if (0 != ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
            "unable to blank frame buffer device '%s':%s \n",
            fPtr->fbDeviceName, strerror(errno));
    }

    sprintf(sysnodeName, "/sys/class/graphics/%s/mode", fPtr->fbDeviceName);

    fpMode = fopen(sysnodeName, "r");

    if (NULL == fpMode)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
            "unable to open sysnode '%s':%s \n",
            sysnodeName, strerror(errno));
        return FALSE;
    }

    if(NULL != fgets(modeName, sizeof(modeName), fpMode))
    {
        imxRemoveTrailingNewLines(modeName);
        strncpy(fPtr->bootupVideoMode, modeName, 64-1);
        fPtr->bootupVideoMode[64-1] = 0;
        got = TRUE;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "Device preferred mode '%s':%s \n",
            fPtr->fbDeviceName, modeName);
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
            "Cannot get device preferred mode '%s (%s)' \n",
            sysnodeName, strerror(errno));
    }

    fclose(fpMode);
    return got;
}

/* Preferred flag: need query EDID, but we simply use start up mode as perferred */
static void imxSetPreferFlag(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    ImxPtr fPtr = IMXPTR(pScrn);
    if(strcmp(mode->name, fPtr->bootupVideoMode) == 0)
    {
        mode->type |= M_T_PREFERRED;
    }
}

/* fix fbdevHWModeInit
  *
  * fbdevHWModeInit does not support non-standard FB_SYNC_ flags
  * fbdevHWModeInit translates timing and there's a slight deviation but causes FB driver to
  * create a non-fully-supported video mode
  *
  * This function will update sys node (mode)
  */
Bool
imxPostHWModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
#if 0
    ImxPtr fPtr = IMXPTR(pScrn);
    const char *pModeName;
    FILE *fpMode;
    char sysnodeName[80];

    // mode != NULL

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "executing imxPostHWModeInit\n");

    if(strcmp("current", mode->name) != 0)
    {
        pModeName = mode->name;
    }
    else
    {
        pModeName = fPtr->bootupVideoMode;
    }

    sprintf(sysnodeName, "/sys/class/graphics/%s/mode", fPtr->fbDeviceName);

    fpMode = fopen(sysnodeName, "w");

    if (NULL == fpMode)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
            "unable to open sysnode '%s':%s \n",
            sysnodeName, strerror(errno));
        return FALSE;
    }

    if(fwrite(mode->name, strlen(mode->name), 1, fpMode) != 1)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
            "unable to write sysnode '%s':%s \n",
            sysnodeName, strerror(errno));
        fclose(fpMode);
        return FALSE;
    }

    fclose(fpMode);
#endif
	return TRUE;
}

int
imxRefreshModes(ScrnInfoPtr pScrn, int fbIndex, char *suggestMode)
{
    FILE* fpModes = NULL;
    int fdDev = -1;
    DisplayModePtr modesList = NULL;
    ImxPtr fPtr = IMXPTR(pScrn);
    ImxDisplayPtr imxDispPtr = IMXDISPLAYPTR(fPtr);
    int rc = -1;

    suggestMode[0] = 0;

    /* check fb index */
    char fbName[32];
    sprintf(fbName, "fb%d", fbIndex);
    if(strcmp(fbName, fPtr->fbDeviceName) != 0) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "HDMI not used by xserver\n");
        return -1;
    }

    /* Access the frame buffer device. */
    fdDev = fbdevHWGetFD(pScrn);
    if (-1 == fdDev) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
            "frame buffer device not available or initialized\n");
        goto errorGetModes;
    }

    /* Create the name of the sysnode file that contains the */
    /* names of all the frame buffer modes. */
    char sysnodeName[80];
    sprintf(sysnodeName, "/sys/class/graphics/%s/modes", fPtr->fbDeviceName);
    fpModes = fopen(sysnodeName, "r");
    if (NULL == fpModes) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
            "unable to open sysnode '%s':%s \n",
            sysnodeName, strerror(errno));
        goto errorGetModes;
    }

    // do not use pScrn->currentMode to check last mode: volatile

    /* Turn on frame buffer blanking. */
    if (0 != ioctl(fdDev, FBIOBLANK, FB_BLANK_NORMAL)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
            "unable to blank frame buffer device '%s': %s\n",
            fPtr->fbDeviceName, strerror(errno));
        goto errorGetModes;
    }

    /* SL: external HDMI device may choose different bpp from LVDS; restore back */
    if(!imxDisplayCheckBpp(pScrn)) {
        goto errorGetModes;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
        "printing discovered frame buffer '%s' supported modes:\n",
        fPtr->fbDeviceName);

    /* Iterate over all the modes in the frame buffer list. */
    char modeName[80];
    while (NULL != fgets(modeName, sizeof(modeName), fpModes)) {
        imxRemoveTrailingNewLines(modeName);

        /* Attempt to set the mode */
        if (!imxDisplaySetMode(pScrn, fPtr->fbDeviceName, modeName)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "unable to set frame buffer mode '%s'\n",
                modeName);
            continue;
        }

        /* Check whether meet XRandR requirement (SL/SX: some modes are not supported) */
        if (!imxDisplayCheckModeXRandR(pScrn)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "Mode '%s' is eliminated from XRandR support\n",
                modeName);
            continue;
        }

        DisplayModePtr mode =
            imxDisplayGetCurrentMode(pScrn, fdDev, modeName);

        if ((NULL != mode) &&
            (mode->HDisplay > 0) &&
            (mode->VDisplay > 0)) {
            /* device preferred mode ? */
            imxSetPreferFlag(pScrn, mode);

            xf86PrintModeline(pScrn->scrnIndex, mode);
            modesList = xf86ModesAdd(modesList, mode);
        }
    }

    if(modesList == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
            "unable to find mode for device '%s'\n",
            fPtr->fbDeviceName);
        goto errorGetModes;
    }

    rc = 0;

errorGetModes:

    /* Close file with list of modes. */
    if (NULL != fpModes) {
        fclose(fpModes);
    }

    /* Remove any duplicate modes found. */
    modesList = xf86PruneDuplicateModes(modesList);

    while (imxDispPtr->fbModesList)
        xf86DeleteMode(&imxDispPtr->fbModesList, imxDispPtr->fbModesList);

    imxDispPtr->fbModesList = modesList;

    /* find a good mode to return */
    if(rc == 0) {
        // 1. same mode name as previous (xrandr will skip?)
        // 2. same resolution as previous
        // 3. largest resolution
        DisplayModePtr pSameNameMode = NULL;
        DisplayModePtr pSameSizeMode = NULL;
        DisplayModePtr pLargestSizeMode = imxDispPtr->fbModesList;
        DisplayModePtr p = imxDispPtr->fbModesList;

        while (p) {
            if(fPtr->lastVideoMode) {
                // Use the previous mode. Set video mode here
                if(fPtr->lastVideoMode->name != NULL && strcmp(p->name, fPtr->lastVideoMode->name) == 0) {
                    imxDisplaySetMode(pScrn, fPtr->fbDeviceName, p->name);
                    pSameNameMode = p;
                    break;
                }

                // is the mode same size as previous one?
                if(p->HDisplay == fPtr->lastVideoMode->HDisplay &&
                    p->VDisplay == fPtr->lastVideoMode->VDisplay)
                    pSameSizeMode = p;
            }

            // is this mode largest size?
            if(p->HDisplay > pLargestSizeMode->HDisplay)
                pLargestSizeMode = p;

            p = p->next;
        }

        if(pSameNameMode)
            strcpy(suggestMode, pSameNameMode->name);
        else if(pSameSizeMode)
            strcpy(suggestMode, pSameSizeMode->name);
        else //pLargestSizeMode != NULL
            strcpy(suggestMode, pLargestSizeMode->name);

        if(suggestMode[0] != 0)
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Choose %s as new mode\n", suggestMode);
    }

#if 0
    while (pScrn->modes)
    xf86DeleteMode(&pScrn->modes, pScrn->modes);

    while (pScrn->modePool)
    xf86DeleteMode(&pScrn->modePool, pScrn->modePool);

    pScrn->currentMode;
#endif

    /* Turn off frame buffer blanking */
    if (-1 != fdDev) {
        ioctl(fdDev, FBIOBLANK, FB_BLANK_UNBLANK);
    }

    return rc;
}

static BOOL
imxDisplayCheckModeXRandR(ScrnInfoPtr pScrn)
{
    int fdDev = fbdevHWGetFD(pScrn);
    VivPtr vPtr = GET_VIV_PTR(pScrn);
    ImxPtr imxPtr = vPtr;

    /* Query the FB variable screen info */
    struct fb_var_screeninfo fbVarScreenInfo;
    if (0 != ioctl(fdDev, FBIOGET_VSCREENINFO, &fbVarScreenInfo)) {
        return FALSE;
    }

    fbVarScreenInfo.xres_virtual = IMX_ALIGN(fbVarScreenInfo.xres, imxPtr->fbAlignWidth);
    fbVarScreenInfo.yres_virtual = 2 * IMX_ALIGN(fbVarScreenInfo.yres, imxPtr->fbAlignHeight);

    /* Make the adjustments to the variable screen info. */
    if (0 != ioctl(fdDev, FBIOPUT_VSCREENINFO, &fbVarScreenInfo)) {
        return FALSE;
    }

    return TRUE;
}

static BOOL
imxDisplayCheckBpp(ScrnInfoPtr pScrn)
{
    int fdDev = fbdevHWGetFD(pScrn);
    VivPtr vPtr = GET_VIV_PTR(pScrn);
    ImxPtr imxPtr = vPtr;

    /* Query the FB variable screen info */
    struct fb_var_screeninfo fbVarScreenInfo;
    if (0 != ioctl(fdDev, FBIOGET_VSCREENINFO, &fbVarScreenInfo)) {
        return FALSE;
    }

    if(fbVarScreenInfo.bits_per_pixel == pScrn->bitsPerPixel)
        return TRUE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Change bpp from %d to %d\n",
        fbVarScreenInfo.bits_per_pixel,
        pScrn->bitsPerPixel);

    fbVarScreenInfo.bits_per_pixel = pScrn->bitsPerPixel;

    /* Make the adjustments to the variable screen info. */
    if (0 != ioctl(fdDev, FBIOPUT_VSCREENINFO, &fbVarScreenInfo)) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Change bpp failed\n");
        return FALSE;
    }

    return TRUE;
}

