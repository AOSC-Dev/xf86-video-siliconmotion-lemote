/* Header:   //Mercury/Projects/archives/XFree86/4.0/smi_accel.c-arc   1.16   03 Jan 2001 13:29:06   Frido  $ */

/*
Copyright (C) 1994-1999 The XFree86 Project, Inc.  All Rights Reserved.
Copyright (C) 2000 Silicon Motion, Inc.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the names of the XFree86 Project and
Silicon Motion shall not be used in advertising or otherwise to promote the
sale, use or other dealings in this Software without prior written
authorization from the XFree86 Project and silicon Motion.
*/
/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/siliconmotion/smi_accel.c,v 1.8 2003/04/23 21:51:44 tsi Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "smi.h"

#include "miline.h"
#include "xaalocal.h"
#include "xaarop.h"
#include "servermd.h"


// extern int gSMIEntityIndex;
extern int	entity_index[MAX_ENTITIES];

static void SMI_SetupForScreenToScreenCopy (ScrnInfoPtr, int, int, int,
					    unsigned int, int);
static void SMI_SubsequentScreenToScreenCopy (ScrnInfoPtr, int, int, int, int,
					      int, int);
static void SMI_SetupForSolidFill (ScrnInfoPtr, int, int, unsigned);
static void SMI_SubsequentSolidFillRect (ScrnInfoPtr, int, int, int, int);
static void SMI_SubsequentSolidHorVertLine (ScrnInfoPtr, int, int, int, int);
static void SMI_SetupForCPUToScreenColorExpandFill (ScrnInfoPtr, int, int,
						    int, unsigned int);
static void SMI_SubsequentCPUToScreenColorExpandFill (ScrnInfoPtr, int, int,
						      int, int, int);
static void SMI_SetupForMono8x8PatternFill (ScrnInfoPtr, int, int, int, int,
					    int, unsigned int);
static void SMI_SubsequentMono8x8PatternFillRect (ScrnInfoPtr, int, int, int,
						  int, int, int);
static void SMI_SetupForColor8x8PatternFill (ScrnInfoPtr, int, int, int,
					     unsigned int, int);
static void SMI_SubsequentColor8x8PatternFillRect (ScrnInfoPtr, int, int, int,
						   int, int, int);
#if SMI_USE_IMAGE_WRITES
static void SMI_SetupForImageWrite (ScrnInfoPtr, int, unsigned int, int, int,
				    int);
static void SMI_SubsequentImageWriteRect (ScrnInfoPtr, int, int, int, int,
					  int);
#endif
static void SMI_SetClippingRectangle (ScrnInfoPtr, int, int, int, int);
static void SMI_DisableClipping (ScrnInfoPtr);
/* #671 */
static void SMI_ValidatePolylines (GCPtr, unsigned long, DrawablePtr);
static void SMI_Polylines (DrawablePtr, GCPtr, int, int, DDXPointPtr);
void WaitForNotBusy(SMIPtr pSmi);

static void
SMI501_RestoreAccelState (ScrnInfoPtr pScrn)
{
  SMI_EngineReset (pScrn);
  return;
}


Bool
SMI_AccelInit (ScreenPtr pScreen)
{
  XAAInfoRecPtr infoPtr;
  ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
  SMIPtr pSmi = SMIPTR (pScrn);
  /*BoxRec AvailFBArea; */
  Bool ret;
  /*int numLines, maxLines; */

  pSmi->AccelInfoRec = infoPtr = XAACreateInfoRec ();
  if (infoPtr == NULL)
    {
      LEAVE_PROC ("SMI_AccelInit");
      return FALSE;
    }

  infoPtr->Flags = PIXMAP_CACHE | LINEAR_FRAMEBUFFER | OFFSCREEN_PIXMAPS;

  infoPtr->Sync = SMI_AccelSync;

  /*boyod */

  if (xf86IsEntityShared (pScrn->entityList[0]))
    {
      DevUnion *pPriv;
      SMIRegPtr pEnt;
//      pPriv = xf86GetEntityPrivate (pScrn->entityList[0], gSMIEntityIndex);
      pPriv = xf86GetEntityPrivate (pScrn->entityList[0], entity_index[pScrn->entityList[0]]);
      pEnt = pPriv->ptr;

      /*if there are more than one devices sharing this entity, we
         have to assign this call back, otherwise the XAA will be
         disabled */
      if (pEnt->DualHead)
	infoPtr->RestoreAccelState = SMI501_RestoreAccelState;
    }
  /*boyod*/


  /* Screen to screen copies */

  infoPtr->ScreenToScreenCopyFlags = NO_PLANEMASK
    | ONLY_TWO_BITBLT_DIRECTIONS;
  infoPtr->SetupForScreenToScreenCopy = SMI_SetupForScreenToScreenCopy;
  infoPtr->SubsequentScreenToScreenCopy = SMI_SubsequentScreenToScreenCopy;
  if (pScrn->bitsPerPixel == 24)
    {
      infoPtr->ScreenToScreenCopyFlags |= NO_TRANSPARENCY;
    }
  if ((pSmi->Chipset == SMI_LYNX3D) && (pScrn->bitsPerPixel == 8))
    {
      infoPtr->ScreenToScreenCopyFlags |= GXCOPY_ONLY;
    }

  /* Solid Fills */
  infoPtr->SolidFillFlags = NO_PLANEMASK;
  infoPtr->SetupForSolidFill = SMI_SetupForSolidFill;
  infoPtr->SubsequentSolidFillRect = SMI_SubsequentSolidFillRect;

  /* Solid Lines */
  infoPtr->SolidLineFlags = NO_PLANEMASK;
  infoPtr->SetupForSolidLine = SMI_SetupForSolidFill;
  infoPtr->SubsequentSolidHorVertLine = SMI_SubsequentSolidHorVertLine;


#if USE_HOSTBLT
  /* Color Expansion Fills */
  infoPtr->CPUToScreenColorExpandFillFlags = ROP_NEEDS_SOURCE
    | NO_PLANEMASK
    | BIT_ORDER_IN_BYTE_MSBFIRST
    | LEFT_EDGE_CLIPPING | CPU_TRANSFER_PAD_DWORD | SCANLINE_PAD_DWORD;
  infoPtr->ColorExpandBase = pSmi->DataPortBase;
  infoPtr->ColorExpandRange = pSmi->DataPortSize;

  infoPtr->SetupForCPUToScreenColorExpandFill =
    SMI_SetupForCPUToScreenColorExpandFill;
  infoPtr->SubsequentCPUToScreenColorExpandFill =
    SMI_SubsequentCPUToScreenColorExpandFill;
#endif

  /* 8x8 Mono Pattern Fills */
  infoPtr->Mono8x8PatternFillFlags = NO_PLANEMASK
    | HARDWARE_PATTERN_PROGRAMMED_BITS
    | HARDWARE_PATTERN_SCREEN_ORIGIN | BIT_ORDER_IN_BYTE_MSBFIRST;
  infoPtr->SetupForMono8x8PatternFill = SMI_SetupForMono8x8PatternFill;
  infoPtr->SubsequentMono8x8PatternFillRect =
    SMI_SubsequentMono8x8PatternFillRect;

  /* 8x8 Color Pattern Fills */
  if (!SMI_LYNX3D_SERIES (pSmi->Chipset) || (pScrn->bitsPerPixel != 24))
    {
      infoPtr->Color8x8PatternFillFlags = NO_PLANEMASK
	| HARDWARE_PATTERN_SCREEN_ORIGIN;
      infoPtr->SetupForColor8x8PatternFill = SMI_SetupForColor8x8PatternFill;
      infoPtr->SubsequentColor8x8PatternFillRect =
	SMI_SubsequentColor8x8PatternFillRect;
    }


#if SMI_USE_IMAGE_WRITES
  /* Image Writes */
  infoPtr->ImageWriteFlags = ROP_NEEDS_SOURCE
    | NO_PLANEMASK | CPU_TRANSFER_PAD_DWORD | SCANLINE_PAD_DWORD;
  infoPtr->ImageWriteBase = pSmi->DataPortBase;
  infoPtr->ImageWriteRange = pSmi->DataPortSize;
  infoPtr->SetupForImageWrite = SMI_SetupForImageWrite;
  infoPtr->SubsequentImageWriteRect = SMI_SubsequentImageWriteRect;
#endif

  /* Clipping */
  infoPtr->ClippingFlags = HARDWARE_CLIP_SCREEN_TO_SCREEN_COPY
    | HARDWARE_CLIP_MONO_8x8_FILL
    | HARDWARE_CLIP_COLOR_8x8_FILL
    | HARDWARE_CLIP_SOLID_FILL
    | HARDWARE_CLIP_SOLID_LINE | HARDWARE_CLIP_DASHED_LINE;
  infoPtr->SetClippingRectangle = SMI_SetClippingRectangle;
  infoPtr->DisableClipping = SMI_DisableClipping;

  /* Pixmap Cache */
  if (pScrn->bitsPerPixel >= 24)
    {
      infoPtr->CachePixelGranularity = 16;
    }
  else
    {
      infoPtr->CachePixelGranularity = 128 / pScrn->bitsPerPixel;
    }

  /* Offscreen Pixmaps */
  infoPtr->maxOffPixWidth = 4096;
  infoPtr->maxOffPixHeight = 4096;
  if (pScrn->bitsPerPixel == 24)
    {
      infoPtr->maxOffPixWidth = 4096 / 3;

      if (pSmi->Chipset == SMI_LYNX)
	{
	  infoPtr->maxOffPixHeight = 4096 / 3;
	}
    }

  SMI_EngineReset (pScrn);


  /* CZ 18.06.2001: moved to smi_driver.c before the NoAccel question
     to have offscreen framebuffer in NoAccel mode */
#if 0
  maxLines = pSmi->FBReserved / (pSmi->width * pSmi->Bpp);
  if (pSmi->rotate)
    {
      numLines = maxLines;
    }
  else
    {
#if SMI_USE_VIDEO
      numLines = ((pSmi->FBReserved - pSmi->width * pSmi->Bpp * pSmi->height)
		  * 25 / 100 + pSmi->width * pSmi->Bpp - 1)
	/ (pSmi->width * pSmi->Bpp);
      numLines += pSmi->height;
#else
      numLines = maxLines;
#endif
    }

  AvailFBArea.x1 = 0;
  AvailFBArea.y1 = 0;
  AvailFBArea.x2 = pSmi->width;
  AvailFBArea.y2 = numLines;
  xf86DrvMsg (pScrn->scrnIndex, X_INFO, "FrameBuffer Box: %d,%d - %d,%d\n",
	      AvailFBArea.x1, AvailFBArea.y1, AvailFBArea.x2, AvailFBArea.y2);
  xf86InitFBManager (pScreen, &AvailFBArea);
#endif

  ret = XAAInit (pScreen, infoPtr);
  if (ret && pSmi->shadowFB)	/* #671 */
    {
      pSmi->ValidatePolylines = infoPtr->ValidatePolylines;
      infoPtr->ValidatePolylines = SMI_ValidatePolylines;
    }

  return (ret);
}

void
SMI_GEReset (ScrnInfoPtr pScrn, int from_timeout, int line, char *file)
{
  SMIPtr pSmi = SMIPTR (pScrn);
  CARD8 tmp;
  unsigned int iTempVal;

  if (from_timeout)
    {
      	if (pSmi->GEResetCnt++ < 10 || xf86GetVerbosity () > 1)
//	if (pSmi->GEResetCnt++ < 10 )
	{
	  xf86DrvMsg (pScrn->scrnIndex, X_INFO,
		      "\tSMI_GEReset called from %s line %d\n", file, line);
	}
    }
  else
    {
      WaitIdleEmpty ();
    }


  /* 501 changes!!! */
  if (pSmi->Chipset == SMI_MSOC)
    {
      ErrorF ("Resetting Graphics Engine!!!\n");
      iTempVal = READ_SCR (pSmi, SCR00) & ~0x00003000;
      WRITE_SCR (pSmi, SCR00, (iTempVal | 0x00003000));
      WRITE_SCR (pSmi, SCR00, iTempVal);
    }
  else
    {
      tmp = VGAIN8_INDEX (pSmi, VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x15);
      VGAOUT8_INDEX (pSmi, VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x15, tmp | 0x30);
    }

  WaitIdleEmpty ();

  if (pSmi->Chipset != SMI_MSOC)
    {
      VGAOUT8_INDEX (pSmi, VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x15, tmp);
    }

  SMI_EngineReset (pScrn);

}

/* The sync function for the GE */
void
SMI_AccelSync (ScrnInfoPtr pScrn)
{
  SMIPtr pSmi = SMIPTR (pScrn);

  WaitIdleEmpty ();		/* #161 */
  WaitForNotBusy(pSmi);


}

void
SMI_EngineReset (ScrnInfoPtr pScrn)
{
  SMIPtr pSmi = SMIPTR (pScrn);
  CARD32 DEDataFormat = 0;
  int i;
  int xyAddress[] = { 320, 400, 512, 640, 800, 1024, 1280, 1600, 2048 };

  if(pSmi->rotate)
  {
  	pSmi->Stride = ((pSmi->width + 15) & ~15) * pSmi->Bpp;
  }
  else
  {
  	pSmi->Stride = (pSmi->lcdWidth * pSmi->Bpp + 15) & ~15;
  }  
	//xf86DrvMsg(pScrn->scrnIndex, X_INFO, " caesar pSmi->width = %d pSmi->lcdWidth = %d\n",	pSmi->width, pSmi->lcdWidth);	
	//xf86DrvMsg(pScrn->scrnIndex, X_INFO, " caesar pSmi->Stride = %d  %d %s\n", pSmi->Stride, __LINE__, __FUNCTION__);	

  switch (pScrn->bitsPerPixel)
    {
    case 8:
      DEDataFormat = 0x00000000;
      break;

    case 16:
      pSmi->Stride >>= 1;
      DEDataFormat = 0x00100000;
      break;

    case 24:
      DEDataFormat = 0x00300000;
      break;

    case 32:
      pSmi->Stride >>= 2;
      DEDataFormat = 0x00200000;
      break;
    }
/*
 * Remakrd by Belcon. Why need we do this?
*/
	for (i = 0; i < sizeof(xyAddress) / sizeof(xyAddress[0]); i++)
	{
		if (pSmi->rotate)
		{
			if (xyAddress[i] == pSmi->height)
			{
				DEDataFormat |= i << 16;
				break;
			}
		}
		else
		{
			if (xyAddress[i] == pSmi->width)
			{
				DEDataFormat |= i << 16;
				break;
			}
		}
	}

  WaitIdleEmpty ();
  WRITE_DPR (pSmi, 0x10, (pSmi->Stride << 16) | pSmi->Stride);
  WRITE_DPR (pSmi, 0x1C, DEDataFormat);
  WRITE_DPR (pSmi, 0x24, 0xFFFFFFFF);
  WRITE_DPR (pSmi, 0x28, 0xFFFFFFFF);
  WRITE_DPR (pSmi, 0x3C, (pSmi->Stride << 16) | pSmi->Stride);

  if (!pSmi->IsSecondary)
    {
      WRITE_DPR (pSmi, 0x40, 0);
      WRITE_DPR (pSmi, 0x44, 0);
    }
  else
    {
/* Changed by Belcon
      WRITE_DPR (pSmi, 0x40, (pSmi->videoRAMBytes / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pSmi->videoRAMBytes / 16 << 4));
*/
      WRITE_DPR (pSmi, 0x40, (pScrn->fbOffset / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pScrn->fbOffset / 16 << 4));
    }

  SMI_DisableClipping (pScrn);
}

/****************************
 * Screen to Screen Copies
 ****************************/

static void
SMI_SetupForScreenToScreenCopy (ScrnInfoPtr pScrn, int xdir, int ydir,
				int rop, unsigned int planemask, int trans)
{
  SMIPtr pSmi = SMIPTR (pScrn);


// #if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(6,9,0,0,0)
// #ifdef XORG_VERSION_CURRENT
#if XORG_VERSION_CURRENT >= XF86_VERSION_NUMERIC(6,9,0,0,0)
  pSmi->AccelCmd = XAAGetCopyROP (rop) | SMI_BITBLT | SMI_START_ENGINE;
#elif XORG_VERSION_CURRENT == XORG_VERSION_NUMERIC(1,3,0,0,0)
  pSmi->AccelCmd = XAAGetCopyROP (rop) | SMI_BITBLT | SMI_START_ENGINE;
#else
  pSmi->AccelCmd = XAACopyROP[rop] | SMI_BITBLT | SMI_START_ENGINE;
#endif

  if ((xdir == -1) || (ydir == -1))
    {
      pSmi->AccelCmd |= SMI_RIGHT_TO_LEFT;
    }


  if (trans != -1)
    {
      pSmi->AccelCmd |= SMI_TRANSPARENT_SRC | SMI_TRANSPARENT_PXL;
      WaitQueue (1);
      WRITE_DPR (pSmi, 0x20, trans);

    }

  if (pSmi->ClipTurnedOn)
    {
      WaitQueue (1);
      WRITE_DPR (pSmi, 0x2C, pSmi->ScissorsLeft);
      pSmi->ClipTurnedOn = FALSE;
    }

}

static void
SMI_SubsequentScreenToScreenCopy (ScrnInfoPtr pScrn, int x1, int y1, int x2,
				  int y2, int w, int h)
{
  SMIPtr pSmi = SMIPTR (pScrn);


  if (pSmi->AccelCmd & SMI_RIGHT_TO_LEFT)
    {
      x1 += w - 1;
      y1 += h - 1;
      x2 += w - 1;
      y2 += h - 1;
    }

  if (pScrn->bitsPerPixel == 24)
    {
      x1 *= 3;
      x2 *= 3;
      w *= 3;

      if (pSmi->Chipset == SMI_LYNX)
	{
	  y1 *= 3;
	  y2 *= 3;
	}

      if (pSmi->AccelCmd & SMI_RIGHT_TO_LEFT)
	{
	  x1 += 2;
	  x2 += 2;
	}
    }
	 WaitIdle();//Teddy sovled the sound issue
 	 WaitQueue ();
  if (!pSmi->IsSecondary)
    {
      WRITE_DPR (pSmi, 0x40, 0);
      WRITE_DPR (pSmi, 0x44, 0);
    }
  else
    {
/* Changed By Belcon
      WRITE_DPR (pSmi, 0x40, (pSmi->videoRAMBytes / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pSmi->videoRAMBytes / 16 << 4));
*/
      WRITE_DPR (pSmi, 0x40, (pScrn->fbOffset / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pScrn->fbOffset / 16 << 4));
    }
/*
  WRITE_DPR (pSmi, 0x00, (x1 << 16) + (y1 & 0xFFFF));
  WRITE_DPR (pSmi, 0x04, (x2 << 16) + (y2 & 0xFFFF));
  WRITE_DPR (pSmi, 0x08, (w << 16) + (h & 0xFFFF));
*/
  WRITE_DPR (pSmi, 0x00, (x1 << 16) | (y1 & 0xFFFF));
  WRITE_DPR (pSmi, 0x04, (x2 << 16) | (y2 & 0xFFFF));
  WRITE_DPR (pSmi, 0x08, (w << 16) | (h & 0xFFFF));
  WRITE_DPR (pSmi, 0x0C, pSmi->AccelCmd);

}

/***************/
/* Solid Fills */
/***************/

static void
SMI_SetupForSolidFill (ScrnInfoPtr pScrn, int color, int rop,
		       unsigned int planemask)
{
  SMIPtr pSmi = SMIPTR (pScrn);

// #if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(6,9,0,0,0)
// #ifdef XORG_VERSION_CURRENT
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(6,9,0,0,0)
  pSmi->AccelCmd = XAAGetPatternROP (rop) | SMI_BITBLT | SMI_START_ENGINE;
#elif XORG_VERSION_CURRENT == XORG_VERSION_NUMERIC(1,3,0,0,0)
  	pSmi->AccelCmd = XAAGetPatternROP (rop) | SMI_BITBLT | SMI_START_ENGINE;
#else
  pSmi->AccelCmd = XAAPatternROP[rop] | SMI_BITBLT | SMI_START_ENGINE;
#endif

  if (pSmi->ClipTurnedOn)
    {
      WaitQueue (4);
      WRITE_DPR (pSmi, 0x2C, pSmi->ScissorsLeft);

      pSmi->ClipTurnedOn = FALSE;
    }
  else
    {
      WaitQueue (3);
    }

  WRITE_DPR (pSmi, 0x14, color);
  WRITE_DPR (pSmi, 0x34, 0xFFFFFFFF);
  WRITE_DPR (pSmi, 0x38, 0xFFFFFFFF);

}

void
SMI_SubsequentSolidFillRect (ScrnInfoPtr pScrn, int x, int y, int w, int h)
{
  SMIPtr pSmi = SMIPTR (pScrn);

  if (pScrn->bitsPerPixel == 24)
    {
      x *= 3;
      w *= 3;

      if (pSmi->Chipset == SMI_LYNX)
	{
	  y *= 3;
	}
    }

  if (SMI_MSOC == pSmi->Chipset)
    {
      /* Clip to prevent negative screen coordinates */
      if (x < 0)
	x = 0;

      if (y < 0)
	y = 0;
    }

  WaitQueue (5);
  if (!pSmi->IsSecondary)
    {
      WRITE_DPR (pSmi, 0x40, 0);
      WRITE_DPR (pSmi, 0x44, 0);
    }
  else
    {
/* changed by Belcon
      WRITE_DPR (pSmi, 0x40, (pSmi->videoRAMBytes / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pSmi->videoRAMBytes / 16 << 4));
*/
      WRITE_DPR (pSmi, 0x40, (pScrn->fbOffset / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pScrn->fbOffset / 16 << 4));
    }
  WRITE_DPR (pSmi, 0x04, (x << 16) | (y & 0xFFFF));
  WRITE_DPR (pSmi, 0x08, (w << 16) | (h & 0xFFFF));
  WRITE_DPR (pSmi, 0x0C, pSmi->AccelCmd);
}

/***************/
/* Solid Lines */
/***************/

static void
SMI_SubsequentSolidHorVertLine (ScrnInfoPtr pScrn, int x, int y, int len,
				int dir)
{
  SMIPtr pSmi = SMIPTR (pScrn);
  int w, h;

  if (dir == DEGREES_0)
    {
      w = len;
      h = 1;
    }
  else
    {
      w = 1;
      h = len;
    }

  if (pScrn->bitsPerPixel == 24)
    {
      x *= 3;
      w *= 3;

      if (pSmi->Chipset == SMI_LYNX)
	{
	  y *= 3;
	}
    }

  WaitQueue (5);
  if (!pSmi->IsSecondary)
    {
      WRITE_DPR (pSmi, 0x40, 0);
      WRITE_DPR (pSmi, 0x44, 0);
    }
  else
    {
/* Changed By Belcon
      WRITE_DPR (pSmi, 0x40, (pSmi->videoRAMBytes / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pSmi->videoRAMBytes / 16 << 4));
*/
      WRITE_DPR (pSmi, 0x40, (pScrn->fbOffset / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pScrn->fbOffset / 16 << 4));
    }
  WRITE_DPR (pSmi, 0x04, (x << 16) | (y & 0xFFFF));
  WRITE_DPR (pSmi, 0x08, (w << 16) | (h & 0xFFFF));
  WRITE_DPR (pSmi, 0x0C, pSmi->AccelCmd);
}

/******************************************************************************/
/*							  Color Expansion Fills							  */
/******************************************************************************/

static void
SMI_SetupForCPUToScreenColorExpandFill (ScrnInfoPtr pScrn, int fg, int bg,
					int rop, unsigned int planemask)
{
  SMIPtr pSmi = SMIPTR (pScrn);

// #if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(6,9,0,0,0)
// #ifdef XORG_VERSION_CURRENT
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(6,9,0,0,0)
  pSmi->AccelCmd = XAAGetCopyROP (rop)
#elif XORG_VERSION_CURRENT == XORG_VERSION_NUMERIC(1,3,0,0,0)
  	pSmi->AccelCmd = XAAGetCopyROP (rop)
#else
  pSmi->AccelCmd = XAACopyROP[rop]
#endif
    | SMI_HOSTBLT_WRITE | SMI_SRC_MONOCHROME | SMI_START_ENGINE;
  if (bg == -1)
    {
      pSmi->AccelCmd |= SMI_TRANSPARENT_SRC;

      WaitQueue (3);
      WRITE_DPR (pSmi, 0x14, fg);
      WRITE_DPR (pSmi, 0x18, ~fg);
      WRITE_DPR (pSmi, 0x20, fg);
    }
  else
    {
      WaitQueue (2);
      WRITE_DPR (pSmi, 0x14, fg);
      WRITE_DPR (pSmi, 0x18, bg);
    }
}

void
SMI_SubsequentCPUToScreenColorExpandFill (ScrnInfoPtr pScrn, int x, int y,
					  int w, int h, int skipleft)
{
  SMIPtr pSmi = SMIPTR (pScrn);

  if (pScrn->bitsPerPixel == 24)
    {
      x *= 3;
      w *= 3;
      skipleft *= 3;

      if (pSmi->Chipset == SMI_LYNX)
	{
	  y *= 3;
	}
    }
  if (skipleft)
    {
      WaitQueue (5);
      WRITE_DPR (pSmi, 0x2C, (pSmi->ScissorsLeft & 0xFFFF0000)
		 | (x + skipleft) | 0x2000);
      pSmi->ClipTurnedOn = TRUE;
    }
  else
    {
      if (pSmi->ClipTurnedOn)
	{
	  WaitQueue ();
	  WRITE_DPR (pSmi, 0x2C, pSmi->ScissorsLeft);
	  pSmi->ClipTurnedOn = FALSE;
	}
      else
	{
	  WaitQueue ();
	}
    }

  if (!pSmi->IsSecondary)
    {
      WRITE_DPR (pSmi, 0x40, 0);
      WRITE_DPR (pSmi, 0x44, 0);
    }
  else
    {
/* Changed by Belcon
      WRITE_DPR (pSmi, 0x40, (pSmi->videoRAMBytes / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pSmi->videoRAMBytes / 16 << 4));
*/
      WRITE_DPR (pSmi, 0x40, (pScrn->fbOffset / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pScrn->fbOffset / 16 << 4));
    }
  WRITE_DPR (pSmi, 0x00, 0);
  WRITE_DPR (pSmi, 0x04, (x << 16) | (y & 0xFFFF));
  WRITE_DPR (pSmi, 0x08, (w << 16) | (h & 0xFFFF));
  WRITE_DPR (pSmi, 0x0C, pSmi->AccelCmd);
}

/******************************************************************************/
/*							 8x8 Mono Pattern Fills							  */
/******************************************************************************/

static void
SMI_SetupForMono8x8PatternFill (ScrnInfoPtr pScrn, int patx, int paty, int fg,
				int bg, int rop, unsigned int planemask)
{
  SMIPtr pSmi = SMIPTR (pScrn);

// #if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(6,9,0,0,0)
// #ifdef XORG_VERSION_CURRENT
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(6,9,0,0,0)
  pSmi->AccelCmd = XAAGetPatternROP (rop) | SMI_BITBLT | SMI_START_ENGINE;
#elif XORG_VERSION_CURRENT == XORG_VERSION_NUMERIC(1,3,0,0,0)
  	pSmi->AccelCmd = XAAGetPatternROP (rop) | SMI_BITBLT | SMI_START_ENGINE;
#else
  pSmi->AccelCmd = XAAPatternROP[rop] | SMI_BITBLT | SMI_START_ENGINE;
#endif

  if (pSmi->ClipTurnedOn)
    {
      WaitQueue (1);
      WRITE_DPR (pSmi, 0x2C, pSmi->ScissorsLeft);
      pSmi->ClipTurnedOn = FALSE;
    }

  WaitQueue (2);
  if (!pSmi->IsSecondary)
    {
      WRITE_DPR (pSmi, 0x40, 0);
      WRITE_DPR (pSmi, 0x44, 0);
    }
  else
    {
/* Changed by Belcon
      WRITE_DPR (pSmi, 0x40, (pSmi->videoRAMBytes / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pSmi->videoRAMBytes / 16 << 4));
*/
      WRITE_DPR (pSmi, 0x40, (pScrn->fbOffset / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pScrn->fbOffset / 16 << 4));
    }


  if (bg == -1)
    {
      WaitQueue (5);
      WRITE_DPR (pSmi, 0x14, fg);
      WRITE_DPR (pSmi, 0x18, ~fg);
      WRITE_DPR (pSmi, 0x20, fg);
      WRITE_DPR (pSmi, 0x34, patx);
      WRITE_DPR (pSmi, 0x38, paty);
    }
  else
    {
      WaitQueue (4);
      WRITE_DPR (pSmi, 0x14, fg);
      WRITE_DPR (pSmi, 0x18, bg);
      WRITE_DPR (pSmi, 0x34, patx);
      WRITE_DPR (pSmi, 0x38, paty);
    }

}

static void
SMI_SubsequentMono8x8PatternFillRect (ScrnInfoPtr pScrn, int patx, int paty,
				      int x, int y, int w, int h)
{
  SMIPtr pSmi = SMIPTR (pScrn);

  if (pScrn->bitsPerPixel == 24)
    {
      x *= 3;
      w *= 3;
      if (pSmi->Chipset == SMI_LYNX)
	{
	  y *= 3;
	}
    }

  WaitQueue (5);

  if (!pSmi->IsSecondary)
    {
      WRITE_DPR (pSmi, 0x40, 0);
      WRITE_DPR (pSmi, 0x44, 0);
    }
  else
    {
/* Changed by Belcon
      WRITE_DPR (pSmi, 0x40, (pSmi->videoRAMBytes / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pSmi->videoRAMBytes / 16 << 4));
*/
      WRITE_DPR (pSmi, 0x40, (pScrn->fbOffset / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pScrn->fbOffset / 16 << 4));
    }
  WRITE_DPR (pSmi, 0x04, (x << 16) | (y & 0xFFFF));
  WRITE_DPR (pSmi, 0x08, (w << 16) | (h & 0xFFFF));
  WRITE_DPR (pSmi, 0x0C, pSmi->AccelCmd);

}

/******************************************************************************/
/*							 8x8 Color Pattern Fills						  */
/******************************************************************************/

static void
SMI_SetupForColor8x8PatternFill (ScrnInfoPtr pScrn, int patx, int paty,
				 int rop, unsigned int planemask,
				 int trans_color)
{
  SMIPtr pSmi = SMIPTR (pScrn);


// #if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(6,9,0,0,0)
// #ifdef XORG_VERSION_CURRENT
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(6,9,0,0,0)
  pSmi->AccelCmd = XAAGetPatternROP (rop)
 #elif XORG_VERSION_CURRENT == XORG_VERSION_NUMERIC(1,3,0,0,0)
  	 pSmi->AccelCmd = XAAGetPatternROP (rop) 
#else
  pSmi->AccelCmd = XAAPatternROP[rop]
#endif
    | SMI_BITBLT | SMI_COLOR_PATTERN | SMI_START_ENGINE;

  if (pScrn->bitsPerPixel <= 16)
    {
      /* PDR#950 */
      CARD8 *pattern = pSmi->FBBase
	+ (patx + paty * pSmi->Stride) * pSmi->Bpp;

      WaitIdleEmpty ();
      WRITE_DPR (pSmi, 0x0C, SMI_BITBLT | SMI_COLOR_PATTERN);
      memcpy (pSmi->DataPortBase, pattern, 8 * pSmi->Bpp * 8);
    }
  else
    {
      if (pScrn->bitsPerPixel == 24)
	{
	  patx *= 3;

	  if (pSmi->Chipset == SMI_LYNX)
	    {
	      paty *= 3;
	    }
	}

      WaitQueue (1);
      WRITE_DPR (pSmi, 0x00, (patx << 16) | (paty & 0xFFFF));
    }

  WaitQueue (2);
  if (!pSmi->IsSecondary)
    {
      WRITE_DPR (pSmi, 0x40, 0);
      WRITE_DPR (pSmi, 0x44, 0);
    }
  else
    {
/* changed by Belcon
      WRITE_DPR (pSmi, 0x40, (pSmi->videoRAMBytes / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pSmi->videoRAMBytes / 16 << 4));
*/
      WRITE_DPR (pSmi, 0x40, (pScrn->fbOffset / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pScrn->fbOffset / 16 << 4));
    }

  if (trans_color == -1)
    {
      pSmi->AccelCmd |= SMI_TRANSPARENT_SRC | SMI_TRANSPARENT_PXL;

      WaitQueue (1);
      WRITE_DPR (pSmi, 0x20, trans_color);
    }

  if (pSmi->ClipTurnedOn)
    {
      WaitQueue (1);
      WRITE_DPR (pSmi, 0x2C, pSmi->ScissorsLeft);
      pSmi->ClipTurnedOn = FALSE;
    }

}

static void
SMI_SubsequentColor8x8PatternFillRect (ScrnInfoPtr pScrn, int patx, int paty,
				       int x, int y, int w, int h)
{
  SMIPtr pSmi = SMIPTR (pScrn);

  if (pScrn->bitsPerPixel == 24)
    {
      x *= 3;
      w *= 3;

      if (pSmi->Chipset == SMI_LYNX)
	{
	  y *= 3;
	}
    }

  WaitQueue ();
  if (!pSmi->IsSecondary)
    {
      WRITE_DPR (pSmi, 0x40, 0);
      WRITE_DPR (pSmi, 0x44, 0);
    }
  else
    {
/* changed by Belcon
      WRITE_DPR (pSmi, 0x40, (pSmi->videoRAMBytes / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pSmi->videoRAMBytes / 16 << 4));
*/
      WRITE_DPR (pSmi, 0x40, (pScrn->fbOffset / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pScrn->fbOffset / 16 << 4));
    }
  WRITE_DPR (pSmi, 0x04, (x << 16) | (y & 0xFFFF));
  WRITE_DPR (pSmi, 0x08, (w << 16) | (h & 0xFFFF));	/* PDR#950 */
  WRITE_DPR (pSmi, 0x0C, pSmi->AccelCmd);

}

#if SMI_USE_IMAGE_WRITES
/******************************************************************************/
/*								  Image Writes								  */
/******************************************************************************/

static void
SMI_SetupForImageWrite (ScrnInfoPtr pScrn, int rop, unsigned int planemask,
			int trans_color, int bpp, int depth)
{
  SMIPtr pSmi = SMIPTR (pScrn);


// #if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(6,9,0,0,0)
// #ifdef XORG_VERSION_CURRENT
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(6,9,0,0,0)
  pSmi->AccelCmd = XAAGetCopyROP (rop) | SMI_HOSTBLT_WRITE | SMI_START_ENGINE;
#elif XORG_VERSION_CURRENT == XORG_VERSION_NUMERIC(1,3,0,0,0)
  	 pSmi->AccelCmd = XAAGetCopyROP (rop) | SMI_HOSTBLT_WRITE | SMI_START_ENGINE;
#else
  pSmi->AccelCmd = XAACopyROP[rop] | SMI_HOSTBLT_WRITE | SMI_START_ENGINE;
#endif

  if (trans_color != -1)
    {
      pSmi->AccelCmd |= SMI_TRANSPARENT_SRC | SMI_TRANSPARENT_PXL;

      WaitQueue (1);
      WRITE_DPR (pSmi, 0x20, trans_color);
    }

}

static void
SMI_SubsequentImageWriteRect (ScrnInfoPtr pScrn, int x, int y, int w, int h,
			      int skipleft)
{
  SMIPtr pSmi = SMIPTR (pScrn);

  if (pScrn->bitsPerPixel == 24)
    {
      x *= 3;
      w *= 3;
      skipleft *= 3;

      if (pSmi->Chipset == SMI_LYNX)
	{
	  y *= 3;
	}
    }

  if (skipleft)
    {
      WaitQueue (5);
      WRITE_DPR (pSmi, 0x2C, (pSmi->ScissorsLeft & 0xFFFF0000) |
		 (x + skipleft) | 0x2000);
      pSmi->ClipTurnedOn = TRUE;
    }
  else
    {
      if (pSmi->ClipTurnedOn)
	{
	  WaitQueue (7);
	  WRITE_DPR (pSmi, 0x2C, pSmi->ScissorsLeft);
	  pSmi->ClipTurnedOn = FALSE;
	}
      else
	{
	  WaitQueue (6);
	}
    }

  if (!pSmi->IsSecondary)
    {
      WRITE_DPR (pSmi, 0x40, 0);
      WRITE_DPR (pSmi, 0x44, 0);
    }
  else
    {
/* changed by Belcon
      WRITE_DPR (pSmi, 0x40, (pSmi->videoRAMBytes / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pSmi->videoRAMBytes / 16 << 4));
*/
      WRITE_DPR (pSmi, 0x40, (pScrn->fbOffset / 16 << 4));
      WRITE_DPR (pSmi, 0x44, (pScrn->fbOffset / 16 << 4));
    }

  WRITE_DPR (pSmi, 0x00, 0);
  WRITE_DPR (pSmi, 0x04, (x << 16) | (y * 0xFFFF));
  WRITE_DPR (pSmi, 0x08, (w << 16) | (h & 0xFFFF));
  WRITE_DPR (pSmi, 0x0C, pSmi->AccelCmd);

}
#endif

/******************************************************************************/
/*									Clipping								  */
/******************************************************************************/

static void
SMI_SetClippingRectangle (ScrnInfoPtr pScrn, int left, int top, int right,
			  int bottom)
{
  SMIPtr pSmi = SMIPTR (pScrn);

  /* CZ 26.10.2001: this code prevents offscreen pixmaps being drawn ???
     left   = max(left, 0);
     top    = max(top, 0);
     right  = min(right, pSmi->width);
     bottom = min(bottom, pSmi->height);
   */

  if (pScrn->bitsPerPixel == 24)
    {
      left *= 3;
      right *= 3;

      if (pSmi->Chipset == SMI_LYNX)
	{
	  top *= 3;
	  bottom *= 3;
	}
    }


  pSmi->ScissorsLeft = (top << 16) | (left & 0xFFFF) | 0x2000;
  pSmi->ScissorsRight = (bottom << 16) | (right & 0xFFFF);

  pSmi->ClipTurnedOn = FALSE;

  WaitQueue (2);
  WRITE_DPR (pSmi, 0x2C, pSmi->ScissorsLeft);

  if (SMI_MSOC != pSmi->Chipset)
    {
      WRITE_DPR (pSmi, 0x30, pSmi->ScissorsRight);
    }
  else
    {
      WRITE_DPR (pSmi, 0x30, (((bottom + 1) << 16) | ((right + 1) & 0xFFFF)));
    }

}

static void
SMI_DisableClipping (ScrnInfoPtr pScrn)
{
  SMIPtr pSmi = SMIPTR (pScrn);


  pSmi->ScissorsLeft = 0;
  if (pScrn->bitsPerPixel == 24)
    {
      if (pSmi->Chipset == SMI_LYNX)
	{
	  pSmi->ScissorsRight = ((pSmi->height * 3) << 16)
	    | (pSmi->width * 3);
	}
      else
	{
	  pSmi->ScissorsRight = (pSmi->height << 16) | (pSmi->width * 3);
	}
    }
  else
    {
      pSmi->ScissorsRight = (pSmi->height << 16) | pSmi->width;
    }

  pSmi->ClipTurnedOn = FALSE;

  WaitQueue (2);
  WRITE_DPR (pSmi, 0x2C, pSmi->ScissorsLeft);
  WRITE_DPR (pSmi, 0x30, pSmi->ScissorsRight);

}

/******************************************************************************/
/*									Polylines							 #671 */
/******************************************************************************/

/*

In order to speed up the "logout" screen in rotated modes, we need to intercept
the Polylines function. Normally, the polylines are drawn and the shadowFB is
then sending a request of the bounding rectangle of those poylines. This should
be okay, if it weren't for the fact that the Gnome logout screen is drawing
polylines in rectangles and this asks for a rotation of the entire rectangle.
This is very slow.

To circumvent this slowness, we intercept the ValidatePolylines function and
override the default "Fallback" Polylines with our own Polylines function. Our
Polylines function first draws the polylines through the original Fallback
function and then rotates the lines, line by line. We then set a flag and
return control to the shadowFB which will try to rotate the bounding rectangle.
However, the flag has been set and the RefreshArea function does nothing but
clear the flag so the next Refresh that comes in shoiuld be handled correctly.

All this code improves the speed quite a bit.

*/

#define IS_VISIBLE(pWin) \
( \
	   pScrn->vtSema \
	&& (((WindowPtr) pWin)->visibility != VisibilityFullyObscured) \
)

#define TRIM_BOX(box, pGC) \
{ \
	BoxPtr extents = &pGC->pCompositeClip->extents; \
	if (box.x1 < extents->x1) box.x1 = extents->x1; \
	if (box.y1 < extents->y1) box.y1 = extents->y1; \
	if (box.x2 > extents->x2) box.x2 = extents->x2; \
	if (box.y2 > extents->y2) box.y2 = extents->y2; \
}

#define TRANSLATE_BOX(box, pDraw) \
{ \
	box.x1 += pDraw->x; \
	box.y1 += pDraw->y; \
	box.x2 += pDraw->x; \
	box.y2 += pDraw->y; \
}

#define BOX_NOT_EMPTY(box) \
	((box.x2 > box.x1) && (box.y2 > box.y1))

static void
SMI_ValidatePolylines (GCPtr pGC, unsigned long changes, DrawablePtr pDraw)
{
  XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC (pGC);
  SMIPtr pSmi = SMIPTR (infoRec->pScrn);

  pSmi->ValidatePolylines (pGC, changes, pDraw);
// #if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(6,9,0,0,0)
// #ifdef XORG_VERSION_CURRENT
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(6,9,0,0,0)
  	if (pGC->ops->Polylines == XAAGetFallbackOps ()->Polylines)
#elif XORG_VERSION_CURRENT == XORG_VERSION_NUMERIC(1,3,0,0,0)
  if (pGC->ops->Polylines == XAAGetFallbackOps ()->Polylines)
#else
  if (pGC->ops->Polylines == XAAFallbackOps.Polylines)
#endif
    {
      /* Override the Polylines function with our own Polylines function. */
      pGC->ops->Polylines = SMI_Polylines;
    }

}

static void
SMI_Polylines (DrawablePtr pDraw, GCPtr pGC, int mode, int npt,
	       DDXPointPtr pptInit)
{
  XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC (pGC);
  ScrnInfoPtr pScrn = infoRec->pScrn;
  SMIPtr pSmi = SMIPTR (pScrn);

  /* Call the original Polylines function. */
// #if XF86_VERSION_CURRENT >= XF86_VERSION_NUMERIC(6,9,0,0,0)
// #ifdef XORG_VERSION_CURRENT
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(6,9,0,0,0)
  pGC->ops->Polylines = XAAGetFallbackOps ()->Polylines;
#elif XORG_VERSION_CURRENT == XORG_VERSION_NUMERIC(1,3,0,0,0)
  pGC->ops->Polylines = XAAGetFallbackOps ()->Polylines;
#else
  pGC->ops->Polylines = XAAFallbackOps.Polylines;
#endif
  (*pGC->ops->Polylines) (pDraw, pGC, mode, npt, pptInit);
  pGC->ops->Polylines = SMI_Polylines;

  if (IS_VISIBLE (pDraw) && npt)
    {
      /* Allocate a temporary buffer for all segments of the polyline. */
      BoxPtr pBox = xnfcalloc (sizeof (BoxRec), npt);
      int extra = pGC->lineWidth >> 1, box;

      if (npt > 1)
	{
	  /* Adjust the extra space required per polyline segment. */
	  if (pGC->joinStyle == JoinMiter)
	    {
	      extra = 6 * pGC->lineWidth;
	    }
	  else if (pGC->capStyle == CapProjecting)
	    {
	      extra = pGC->lineWidth;
	    }
	}

      for (box = 0; --npt;)
	{
	  /* Setup the bounding box for one polyline segment. */
	  pBox[box].x1 = pptInit->x;
	  pBox[box].y1 = pptInit->y;
	  pptInit++;
	  pBox[box].x2 = pptInit->x;
	  pBox[box].y2 = pptInit->y;
	  if (mode == CoordModePrevious)
	    {
	      pBox[box].x2 += pBox[box].x1;
	      pBox[box].y2 += pBox[box].y1;
	    }

	  /* Sort coordinates. */
	  if (pBox[box].x1 > pBox[box].x2)
	    {
	      int tmp = pBox[box].x1;
	      pBox[box].x1 = pBox[box].x2;
	      pBox[box].x2 = tmp;
	    }
	  if (pBox[box].y1 > pBox[box].y2)
	    {
	      int tmp = pBox[box].y1;
	      pBox[box].y1 = pBox[box].y2;
	      pBox[box].y2 = tmp;
	    }

	  /* Add extra space required for each polyline segment. */
	  pBox[box].x1 -= extra;
	  pBox[box].y1 -= extra;
	  pBox[box].x2 += extra + 1;
	  pBox[box].y2 += extra + 1;

	  /* See if we need to draw this polyline segment. */
	  TRANSLATE_BOX (pBox[box], pDraw);
	  TRIM_BOX (pBox[box], pGC);
	  if (BOX_NOT_EMPTY (pBox[box]))
	    {
	      box++;
	    }
	}

      if (box)
	{
	  /* Refresh all polyline segments now. */
	  if (pSmi->Chipset == SMI_COUGAR3DR)
	    {
	      SMI_RefreshArea730 (pScrn, box, pBox);
	    }
	  else
	    {
	      SMI_RefreshArea (pScrn, box, pBox);
	    }

	}

      /* Free the temporary buffer. */
      xfree (pBox);
    }

  pSmi->polyLines = TRUE;
}


