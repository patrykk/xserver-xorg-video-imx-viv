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
#include <sys/socket.h>
#include <linux/netlink.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>

#define UEVENT_BUFFER_SIZE 2048
#define UEVENT_PARAMS_MAX 32

enum uevent_action { action_add, action_remove, action_change };
enum uevent_source { unknown=0, from_6qdlsolo, from_6slsx };

struct uevent {
    char *path;
    enum uevent_action action;
    char *subsystem;
    char *param[UEVENT_PARAMS_MAX];
    unsigned int seqnum;
    enum uevent_source source;
};

#define FSLPRINTF printf

static int is_hdmi_event(struct uevent *e)
{
    if(strcmp(e->subsystem, "i2c") == 0) {
        // SL
        int i;
        for(i=0; e->param[i]!=0; i++) {
            if(strcmp(e->param[i], "DRIVER=sii902x") == 0) {
                e->source = from_6slsx;
                return 1;
            }
        }
    }
    else {
        int i;
        for(i=0; e->param[i]!=0; i++) {
            if(strcmp(e->param[i], "DRIVER=mxc_hdmi") == 0) {
                e->source = from_6qdlsolo;
                return 1;
            }
        }
    }

    return 0;
}

static int init_hotplug_sock(void)
{
    struct sockaddr_nl snl;
    const int buffersize = 16 * 1024 * 1024;
    int retval;

    memset(&snl, 0, sizeof(struct sockaddr_nl));
    snl.nl_family = AF_NETLINK;
    snl.nl_pid = getpid();
    snl.nl_groups = 1;

    int hotplug_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (hotplug_sock == -1) {
        FSLPRINTF("Error getting socket: %s", strerror(errno));
        return -1;
    }

    /* set receive buffersize */
    setsockopt(hotplug_sock, SOL_SOCKET, SO_RCVBUFFORCE, &buffersize, sizeof(buffersize));
    retval = bind(hotplug_sock, (struct sockaddr *) &snl, sizeof(struct sockaddr_nl));
    if (retval < 0) {
        FSLPRINTF("Error binding socket: %s", strerror(errno));
        close(hotplug_sock);
        hotplug_sock = -1;
    }

    return hotplug_sock;
}

static void free_hotplug_sock(int sock)
{
    close(sock);
}

static const char *get_uevent_param(struct uevent *event, const char *param_name)
{
    int i;
    for (i = 0; i < UEVENT_PARAMS_MAX; i++) {
        if (!event->param[i])
            break;

        if (!strncmp(event->param[i], param_name, strlen(param_name)))
            return &event->param[i][strlen(param_name) + 1];
    }

    FSLPRINTF("get_uevent_param(): No parameter '%s' found", param_name);
    return NULL;
}

static void dump_uevent(struct uevent *event)
{
    int i;

    FSLPRINTF("[UEVENT] Sq: %u S: %s A: %d P: %s\n",
              event->seqnum, event->subsystem, event->action, event->path);
    for (i = 0; i < UEVENT_PARAMS_MAX; i++) {
        if (!event->param[i])
            break;
        FSLPRINTF("%s\n", event->param[i]);
    }
}

static void
imxRemoveTrailingNewLines(char* str)
{
	int len = strlen(str);

	while ((len > 0) && ('\n' == str[len-1])) {

		str[--len] = '\0';
	}
}

static int find_hdmi_fb(struct uevent *event)
{
    int index;
    char sysnodeName[80];
    char content[64];
    int rc = -1;

    for(index = 0; index < 6; index+=2) {
        sprintf(sysnodeName, "/sys/class/graphics/fb%d/fsl_disp_dev_property", index);

        FILE *fp = fopen(sysnodeName, "r");
        if (NULL == fp)
            continue;

        content[0] = 0;
        if(NULL != fgets(content, sizeof(content), fp)) {
            imxRemoveTrailingNewLines(content);
            if(strcmp(content, "hdmi") == 0) {
                rc = index;
            }
        }

        fclose(fp);

        if(rc != -1)
            break;
    }

    if(rc == -1) {
        if(event->source == from_6slsx) {
            rc = 0;
        }
    }

    return rc;
}

static char *get_output_name(int fbIndex)
{
    FILE *fpOutput;
    char sOutputName[128];
    char sysnodeName[80];

    sOutputName[0] = 0;

    sprintf(sysnodeName, "/sys/class/graphics/fb%d/name", fbIndex);

    fpOutput = fopen(sysnodeName, "r");

    if (NULL == fpOutput)
    {
        FSLPRINTF("Error: unable to open sysnode '%s' (%s)\n",
            sysnodeName, strerror(errno));
    }
    else
    {
        if(NULL != fgets(sOutputName, sizeof(sOutputName), fpOutput))
        {
            imxRemoveTrailingNewLines(sOutputName);
        }
        fclose(fpOutput);
    }

    if(sOutputName[0] != 0)
        return strdup(sOutputName);
    else
        return NULL;
}

static int x_err_handler(Display *dpy, XErrorEvent *e)
{
    FSLPRINTF("An x11 error is recovered\n");
    return 0;
}

#define X_VIVEXTRefreshVideoModes         15

typedef struct _VIVEXTRefreshVideoModes {
    CARD8   reqType;                /* always vivEXTReqCode */
    CARD8   vivEXTReqType;          /* always X_VIVEXTRefreshVideoModes */
    CARD16  length B16;
    CARD32  screen B32;
    CARD32  fb B32;
} xVIVEXTRefreshVideoModesReq;
#define sz_xVIVEXTRefreshVideoModesReq   12

typedef struct {
	BYTE	type;			/* X_Reply */
	BYTE	pad1;
	CARD16	sequenceNumber B16;
	CARD32	length B32;
	CARD32	preferModeLen B32;
	CARD32	pad3 B32;
	CARD32	pad4 B32;
	CARD32	pad5 B32;
	CARD32	pad6 B32;
	CARD32	pad7 B32;
} xVIVEXTRefreshVideoModesReply;
#define	sz_xVIVEXTRefreshVideoModesReply 32

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

static int refresh_video_modes(Display* dpy, int screen, int fbIndex, char *suggestMode)
{
    XExtDisplayInfo *info = find_display (dpy);
    xVIVEXTRefreshVideoModesReq *req;
    xVIVEXTRefreshVideoModesReply rep;
    int rc = -1;

    VIVEXTCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(VIVEXTRefreshVideoModes, req);
    req->reqType = info->codes->major_opcode;
    req->vivEXTReqType = X_VIVEXTRefreshVideoModes;
    req->screen = screen;
    req->fb = fbIndex;

    if (!_XReply(dpy, (xReply *)&rep, 0 , xFalse)) {
        UnlockDisplay(dpy);
        SyncHandle();
        return -1;
    }

    int nbytes = rep.length << 2;
    int nbytesRead =  rep.preferModeLen;
    if(nbytes > 0) {
        _XRead(dpy, suggestMode, nbytes);
        rc = 0;
    }

    UnlockDisplay(dpy);
    SyncHandle();

    return 0;
}

static int handle_hdmi_plugin(struct uevent *event)
{
    char mode[64];
    mode[0] = 0;

    int fbIndex = find_hdmi_fb(event);
    if(fbIndex == -1) {
        FSLPRINTF("No fb uses hdmi\n");
        return -1;
    }

    /* connect to xserver */
    Display *dpy = XOpenDisplay(NULL);
    if(dpy == NULL) {
        FSLPRINTF("XServer not running\n");
        return -1;
    }

    /* set error handler */
    int (*oldXErrorHandler)(Display *, XErrorEvent *);
    oldXErrorHandler = XSetErrorHandler(x_err_handler);

    /* send special request to xserver/exa to refresh video modes and return a good mode (
     which resolution is near to current one */
    refresh_video_modes(dpy, XDefaultScreen(dpy), fbIndex, mode);

    /* close connection to xserver */
    XSetErrorHandler(oldXErrorHandler);
    XCloseDisplay(dpy);

    if(mode[0] == 0) {
        FSLPRINTF("Failed to refresh video modes\n");
        return -1;
    }

    /* system call to xrandr to set a mode */
    // get output name
    char *output = get_output_name(fbIndex);
    if(output == NULL) {
        FSLPRINTF("Cannot get output name for fb%d\n", fbIndex);
        return -1;
    }

    // construct command
    char setvideomode[256];
    sprintf(setvideomode, "xrandr --output \'%s\' --mode \'%s\'", output, mode);

    // execute command
    int rc = system(setvideomode);
    return rc;
}

int main(int argc, const char **argv)
{
    struct uevent *event;
	int hotplug_sock = init_hotplug_sock();

	if (!(event = (struct uevent *)malloc(sizeof(struct uevent)))) {
		FSLPRINTF("Error allocating memory (%s)", strerror(errno));
		return -errno;
	}


    while (1) {
		/* Netlink message buffer */
		char buf[UEVENT_BUFFER_SIZE * 2] = {0};
		int count = recv(hotplug_sock, &buf, sizeof(buf), 0);
		if (count < 0)
			continue;

        /* Parse the text to construct uevent object */
        char *s = buf;
        char *end = s + count;
	    int param_idx = 0;
		memset(event, 0, sizeof(struct uevent));
        while (s < end) {
            if (s == buf) {
                // get path
                char *p = s;
                while (*p != '@')
                    p++;
                event->path = strdup(p+1);
            }
            else {
                // get action/seqnum/subsystem/etc.
                if (!strncmp(s, "ACTION=", strlen("ACTION="))) {
                    char *a = s + strlen("ACTION=");
					if (!strcmp(a, "add"))
						event->action = action_add;
					else if (!strcmp(a, "change"))
						event->action = action_change;
					else if (!strcmp(a, "remove"))
						event->action = action_remove;
                }
                else if (!strncmp(s, "SEQNUM=", strlen("SEQNUM="))) {
					event->seqnum = atoi(s + strlen("SEQNUM="));
                }
                else if (!strncmp(s, "SUBSYSTEM=", strlen("SUBSYSTEM="))) {
					event->subsystem = strdup(s + strlen("SUBSYSTEM="));
                }
                else {
					event->param[param_idx++] = strdup(s);
                }
            }

			s+= strlen(s) + 1;
        }

        /* debug dump */
//        dump_uevent(event);

        /* check uevent */
        if(is_hdmi_event(event)) {
			const char *state = get_uevent_param(event, "EVENT");
			if (!strcmp(state, "hdcpint")) {
                FSLPRINTF("EVENT hdcpint\n");
			}else if (!strcmp(state, "plugin")) {
                FSLPRINTF("EVENT plugin\n");
                handle_hdmi_plugin(event);
			} else if (!strcmp(state, "plugout")){
                FSLPRINTF("EVENT plugout\n");
			} else if (!strcmp(state, "hdcpenable")){
                FSLPRINTF("EVENT hdcpenable\n");
			} else if (!strcmp(state, "hdcpdisable")){
                FSLPRINTF("EVENT hdcpdisable\n");
			}
		}

        /* free event */
        if(event->path != NULL)
            free(event->path);
        if(event->subsystem != NULL)
            free(event->subsystem);
        while(--param_idx >= 0)
            free(event->param[param_idx]);
    }

    free_hotplug_sock(hotplug_sock);
    free(event);

    return 0;
}

