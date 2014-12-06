/****************************************************************************
*
*    Copyright (C) 2013 by Freescale Semiconductor, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "vivante_debug.h"

static const char *logFile = "/home/linaro/share/exa_log.txt";

static FILE *fpLog = NULL;

void OpenLog()
{
    if(fpLog == NULL)
        fpLog = fopen(logFile, "w");
}

void CloseLog()
{
    if(fpLog)
        fclose(fpLog);
    fpLog = NULL;
}

void LogString(const char *str)
{
    if(fpLog == NULL)
        return;

    if(str == NULL || str[0] == 0)
        return;

    if(strlen(str) > 1024*10)
        return;

    fwrite(str, 1, strlen(str), fpLog);

    fflush(fpLog);
}

static char tmp[1024*10];
void LogText(const char *fmt, ...)
{
    va_list args;

    if(fpLog == NULL)
        return;

    va_start(args, fmt);
    vsprintf(tmp, fmt, args);
    va_end (args);

    LogString(tmp);
}

typedef struct tagPixmapQueue
{
    struct tagPixmapQueue *next;
    Viv2DPixmap * pixmap;
}PixmapQueue;

static PixmapQueue queue_of_pixmaps_held_by_gpu;

void initPixmapQueue()
{
    LOGD("---- initPixmapQueue\n");
    queue_of_pixmaps_held_by_gpu.next = NULL;
    queue_of_pixmaps_held_by_gpu.pixmap = NULL;
}

void freePixmapQueue()
{
    PixmapQueue *pnext = queue_of_pixmaps_held_by_gpu.next;
    while(pnext)
    {
        PixmapQueue *p = pnext->next;
        pnext->pixmap->mGpuBusy = FALSE;
        free(pnext);
        pnext = p;
    }

    queue_of_pixmaps_held_by_gpu.next = NULL;
}

void queuePixmapToGpu(Viv2DPixmapPtr vpixmap)
{
    PixmapQueue *p = NULL;

    if(vpixmap == NULL || vpixmap->mGpuBusy) // already in the queue
        return;

    p = (PixmapQueue *)malloc(sizeof(PixmapQueue));
    if(p != NULL)
    {
        // set flag
        vpixmap->mGpuBusy = TRUE;

        // store
        p->pixmap = vpixmap;

        // put in the front
        p->next   = queue_of_pixmaps_held_by_gpu.next;
        queue_of_pixmaps_held_by_gpu.next = p;
    }
}

extern Bool vivEnableCacheMemory;

enum PixmapCachePolicy getPixmapCachePolicy()
{
    if(vivEnableCacheMemory)
        return WRITEALLOC;
    else
        return NONCACHEABLE;
}

extern Bool vivEnableSyncDraw;

int isGpuSyncMode()
{
    return vivEnableSyncDraw;
}

void preGpuDraw(VivPtr pViv, Viv2DPixmapPtr vpixmap, int bSrc)
{
    enum PixmapCachePolicy ePolicy = getPixmapCachePolicy();

    if(vpixmap == NULL || !vpixmap->mCpuBusy)
        return;

    // if this pixmap is noncacheable, then do nothing
    if(vpixmap->mFlags & VIVPIXMAP_FLAG_NONCACHEABLE)
    {
        vpixmap->mCpuBusy = FALSE;
        return;
    }

    if(bSrc)
    {
        // this is a source pixmap
        if(ePolicy == WRITETHROUGH)
        {
            // no need to clean cache
        }
        else if(ePolicy == WRITEALLOC)
        {
            VIV2DCacheOperation(&pViv->mGrCtx, vpixmap, FLUSH);
            vpixmap->mCpuBusy = FALSE;
        }
        else if(ePolicy == NONCACHEABLE)
        {
            vpixmap->mCpuBusy = FALSE;
        }
    }
    else
    {
        // this is a dest pixmap
        if(ePolicy == WRITETHROUGH)
        {
            VIV2DCacheOperation(&pViv->mGrCtx, vpixmap, INVALIDATE);
            vpixmap->mCpuBusy = FALSE;
        }
        else if(ePolicy == WRITEALLOC)
        {
            VIV2DCacheOperation(&pViv->mGrCtx, vpixmap, FLUSH);
            vpixmap->mCpuBusy = FALSE;
        }
        else if(ePolicy == NONCACHEABLE)
        {
            vpixmap->mCpuBusy = FALSE;
        }
    }
}

extern void OnSurfaceDamaged(ScreenPtr pScreen);

void postGpuDraw(VivPtr pViv)
{
    VIV2DGPUFlushGraphicsPipe(&pViv->mGrCtx); // need flush?

    if(isGpuSyncMode())
    {
        // wait until gpu done
        VIV2DGPUBlitComplete(&pViv->mGrCtx, TRUE);
        freePixmapQueue();
    }
    else
    {
        // fire but not wait
        VIV2DGPUBlitComplete(&pViv->mGrCtx, FALSE);
    }

    OnSurfaceDamaged(pViv->pScreen);
}

void preCpuDraw(VivPtr pViv, Viv2DPixmapPtr vivpixmap)
{
    if(vivpixmap)
    {
        // wait until gpu done
        if(vivpixmap->mGpuBusy)
        {
            VIV2DGPUBlitComplete(&pViv->mGrCtx, TRUE);
            freePixmapQueue();
            // is it possible this pixmap not in the queue?
            vivpixmap->mGpuBusy = FALSE;
        }

        // set flag
        if((vivpixmap->mFlags & VIVPIXMAP_FLAG_NONCACHEABLE) == 0)
            vivpixmap->mCpuBusy = TRUE;
    }
}

void postCpuDraw(VivPtr pViv, Viv2DPixmapPtr vivpixmap)
{
    if(pViv == NULL || vivpixmap == NULL)
        return;

    if(vivpixmap->mFlags & VIVPIXMAP_FLAG_NONCACHEABLE)
    {
        // this pixmap is noncacheable
        vivpixmap->mCpuBusy = FALSE;
    }

    OnSurfaceDamaged(pViv->pScreen);
}

#if defined(DRAWING_STATISTICS)
#include <sys/time.h>

typedef enum tagDrawOp
{
    DRAW_SOLID = 1,
    DRAW_COPY,
    DRAW_UPLOAD,
    DRAW_COMPOSITE,
    DRAW_SWACCESS,
    DRAW_UNKNOWN = 100
}DRAWOP;

typedef struct tagDrawing
{
    DRAWOP op;
    // time
    struct timeval time_start;
    struct timeval time_end;
    // parameters for solid
    int solid_width;
    int solid_height;
    // parameters for copy
    int copy_width;
    int copy_height;
    int rop;
    // parameters for upload
    int upload_width;
    int upload_height;
    // parameters for composite
    int comp_width;
    int comp_height;
}DRAWING;

static DRAWING curDrawing;

void initDrawingStatistics()
{
    curDrawing.op = DRAW_UNKNOWN;
}

void freeDrawingStatistics()
{
}

void startDrawingSolid(int width, int height)
{
    struct timeval time;
    gettimeofday(&time, NULL);

    curDrawing.op           = DRAW_SOLID;
    curDrawing.time_start   = time;
    curDrawing.solid_width  = width;
    curDrawing.solid_height = height;
}

void endDrawingSolid()
{
    struct timeval time;
    gettimeofday(&time, NULL);

    FSLASSERT(curDrawing.op == DRAW_SOLID);
    if(curDrawing.op != DRAW_SOLID)
        return;

    curDrawing.time_end     = time;

    // save to file
    LOGD("STATISTICS: [%d:%d - %d:%d] solid  %d x %d  use %d us\n",
        curDrawing.time_start.tv_sec, curDrawing.time_start.tv_usec,
        curDrawing.time_end.tv_sec, curDrawing.time_end.tv_usec,
        curDrawing.solid_width, curDrawing.solid_height,
        (curDrawing.time_end.tv_sec-curDrawing.time_start.tv_sec)*1000*1000+(curDrawing.time_end.tv_usec-curDrawing.time_start.tv_usec));
}

void startDrawingCopy(int width, int height, int rop)
{
    struct timeval time;
    gettimeofday(&time, NULL);

    curDrawing.op           = DRAW_COPY;
    curDrawing.time_start   = time;
    curDrawing.rop          = rop;
    curDrawing.copy_width   = width;
    curDrawing.copy_height  = height;
}

void endDrawingCopy()
{
    struct timeval time;
    gettimeofday(&time, NULL);

    FSLASSERT(curDrawing.op == DRAW_COPY);
    if(curDrawing.op != DRAW_COPY)
        return;

    curDrawing.time_end     = time;

    // save to file
    LOGD("STATISTICS: [%d:%d - %d:%d] copy  %d x %d rop %d  use %d us\n",
        curDrawing.time_start.tv_sec, curDrawing.time_start.tv_usec,
        curDrawing.time_end.tv_sec, curDrawing.time_end.tv_usec,
        curDrawing.copy_width, curDrawing.copy_height,
        curDrawing.rop,
        (curDrawing.time_end.tv_sec-curDrawing.time_start.tv_sec)*1000*1000+(curDrawing.time_end.tv_usec-curDrawing.time_start.tv_usec));
}

void startDrawingUpload(int width, int height)
{
    struct timeval time;
    gettimeofday(&time, NULL);

    curDrawing.op           = DRAW_UPLOAD;
    curDrawing.time_start   = time;
    curDrawing.upload_width = width;
    curDrawing.upload_height= height;
}

void endDrawingUpload()
{
    struct timeval time;
    gettimeofday(&time, NULL);

    FSLASSERT(curDrawing.op == DRAW_UPLOAD);
    if(curDrawing.op != DRAW_UPLOAD)
        return;

    curDrawing.time_end     = time;

    // save to file
    LOGD("STATISTICS: [%d:%d - %d:%d] upload  %d x %d use %d us\n",
        curDrawing.time_start.tv_sec, curDrawing.time_start.tv_usec,
        curDrawing.time_end.tv_sec, curDrawing.time_end.tv_usec,
        curDrawing.upload_width, curDrawing.upload_height,
        (curDrawing.time_end.tv_sec-curDrawing.time_start.tv_sec)*1000*1000+(curDrawing.time_end.tv_usec-curDrawing.time_start.tv_usec));
}

void startDrawingCompose(int width, int height)
{
    struct timeval time;
    gettimeofday(&time, NULL);

    curDrawing.op           = DRAW_COMPOSITE;
    curDrawing.time_start   = time;
    curDrawing.comp_width   = width;
    curDrawing.comp_height  = height;
}

void endDrawingCompose()
{
    struct timeval time;
    gettimeofday(&time, NULL);

    FSLASSERT(curDrawing.op == DRAW_COMPOSITE);
    if(curDrawing.op != DRAW_COMPOSITE)
        return;

    curDrawing.time_end     = time;

    // save to file
    LOGD("STATISTICS: [%d:%d - %d:%d] composite  %d x %d use %d us\n",
        curDrawing.time_start.tv_sec, curDrawing.time_start.tv_usec,
        curDrawing.time_end.tv_sec, curDrawing.time_end.tv_usec,
        curDrawing.comp_width, curDrawing.comp_height,
        (curDrawing.time_end.tv_sec-curDrawing.time_start.tv_sec)*1000*1000+(curDrawing.time_end.tv_usec-curDrawing.time_start.tv_usec));
}

void startDrawingSW()
{
    struct timeval time;
    gettimeofday(&time, NULL);

    curDrawing.op           = DRAW_SWACCESS;
    curDrawing.time_start   = time;
}

void endDrawingSW()
{
    struct timeval time;
    gettimeofday(&time, NULL);

    FSLASSERT(curDrawing.op == DRAW_SWACCESS);
    if(curDrawing.op != DRAW_SWACCESS)
        return;

    curDrawing.time_end     = time;

    // save to file
    LOGD("STATISTICS: [%d:%d - %d:%d] sw  use %d us\n",
        curDrawing.time_start.tv_sec, curDrawing.time_start.tv_usec,
        curDrawing.time_end.tv_sec, curDrawing.time_end.tv_usec,
        (curDrawing.time_end.tv_sec-curDrawing.time_start.tv_sec)*1000*1000+(curDrawing.time_end.tv_usec-curDrawing.time_start.tv_usec));
}

#endif

