/*
 *    Copyright (C) 2014 by Freescale Semiconductor, Inc.
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <errno.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>

#define FSLPRINTF printf

static int x_err_handler(Display *dpy, XErrorEvent *e)
{
    FSLPRINTF("An x11 error is recovered\n");
    return 0;
}

#define X_VIVEXTDisplayFlip                    16

/* Fix tearing: update back surface */
typedef struct _VIVEXTDisplayFlip {
       CARD8   reqType;                /* always vivEXTReqCode */
       CARD8   vivEXTReqType;          /* always X_VIVEXTDisplayFlip */
       CARD16  length B16;
       CARD32  screen B32;
       CARD32  restore B32;
} xVIVEXTDisplayFlipReq;
#define sz_xVIVEXTDisplayFlipReq   12


#define VIVEXTNAME "vivext"

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

static int flip_display(Display* dpy, int screen, int restore)
{
    XExtDisplayInfo *info = find_display (dpy);
    xVIVEXTDisplayFlipReq *req;
    int rc = -1;

    VIVEXTCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(VIVEXTDisplayFlip, req);
    req->reqType = info->codes->major_opcode;
    req->vivEXTReqType = X_VIVEXTDisplayFlip;
    req->screen = screen;
    req->restore = restore;

    UnlockDisplay(dpy);
    SyncHandle();

    return 0;
}

#define UPDATE_RATE 30

int main(int argc, const char **argv)
{
    struct timeval t1, t2;
    t1.tv_sec = 0;
    t1.tv_usec = 0;

    /* connect to xserver */
    Display *dpy = XOpenDisplay(NULL);
    if(dpy == NULL) {
        FSLPRINTF("XServer not running\n");
        return -1;
    }

    /* set error handler */
    int (*oldXErrorHandler)(Display *, XErrorEvent *);
    oldXErrorHandler = XSetErrorHandler(x_err_handler);


    while(1) {
        // check duration
        gettimeofday(&t2, NULL);
        unsigned long duration = (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_usec - t1.tv_usec) / 1000;
        if(duration < 1000/UPDATE_RATE - 3) {
            usleep(1000);
            continue;
        }

        // flip
        t1 = t2;
        flip_display(dpy, XDefaultScreen(dpy), 0);
        XSync(dpy, False);
    }

    usleep(1000 * 1000 * 5);

    FSLPRINTF("Quit and reset display ...\n");
    flip_display(dpy, XDefaultScreen(dpy), 1);
    XSync(dpy, False);
    FSLPRINTF("Done\n");

    /* close connection to xserver */
    XSetErrorHandler(oldXErrorHandler);
    XCloseDisplay(dpy);
}

