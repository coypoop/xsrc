/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/tdfx/tdfx_video.c,v 1.15 2001/08/01 00:44:54 tsi Exp $ */

#include "xf86.h"
#include "tdfx.h"
#include "dixstruct.h"

#include "Xv.h"
#include "fourcc.h"

static Atom xvColorKey, xvFilterQuality;

/* These should move into tdfxdefs.h with better names */
#define YUV_Y_BASE              0xC00000
#define YUV_U_BASE              0xD00000
#define YUV_V_BASE              0xE00000

#define SST_2D_FORMAT_YUYV      0x8
#define SST_2D_FORMAT_UYVY      0x9

#define YUVBASEADDR             0x80100
#define YUVSTRIDE               0x80104
#define VIDPROCCFGMASK          0xa2e3eb6c

#define OFF_DELAY               250  /* milliseconds */
#define FREE_DELAY              15000

#define OFF_TIMER               0x01
#define FREE_TIMER              0x02
#define CLIENT_VIDEO_ON         0x04
#define TIMER_MASK              (OFF_TIMER | FREE_TIMER)

#define TDFX_MAX_OVERLAY_PORTS  1
#define TDFX_MAX_TEXTURE_PORTS  32

#define GET_PORT_PRIVATE(pScrn) \
   (TDFXPortPrivPtr)((TDFXPTR(pScrn))->overlayAdaptor->pPortPrivates[0].ptr)

/* Doesn't matter what screen we use */
#define DummyScreen             screenInfo.screens[0]

/* Needed for attribute atoms */
#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

/*
 * PROTOTYPES
 */

static FBAreaPtr TDFXAllocateMemoryArea (ScrnInfoPtr pScrn, FBAreaPtr area, int width, int height);
static FBLinearPtr TDFXAllocateMemoryLinear (ScrnInfoPtr pScrn, FBLinearPtr linear, int size);
static void TDFXVideoTimerCallback(ScrnInfoPtr pScrn, Time time);

static XF86VideoAdaptorPtr TDFXSetupImageVideoTexture(ScreenPtr);
static int  TDFXSetPortAttributeTexture(ScrnInfoPtr, Atom, INT32, pointer);
static int  TDFXGetPortAttributeTexture(ScrnInfoPtr, Atom ,INT32 *, pointer);
static int  TDFXPutImageTexture(ScrnInfoPtr, short, short, short, short, short, short, short, short, int, unsigned char*, short, short, Bool, RegionPtr, pointer);
static void TDFXStopVideoTexture(ScrnInfoPtr, pointer, Bool);

static XF86VideoAdaptorPtr TDFXSetupImageVideoOverlay(ScreenPtr);
static int  TDFXSetPortAttributeOverlay(ScrnInfoPtr, Atom, INT32, pointer);
static int  TDFXGetPortAttributeOverlay(ScrnInfoPtr, Atom ,INT32 *, pointer);
static int  TDFXPutImageOverlay(ScrnInfoPtr, short, short, short, short, short, short, short, short, int, unsigned char*, short, short, Bool, RegionPtr, pointer);
static void TDFXStopVideoOverlay(ScrnInfoPtr, pointer, Bool);
static void TDFXResetVideoOverlay(ScrnInfoPtr);

static void TDFXQueryBestSize(ScrnInfoPtr, Bool, short, short, short, short, unsigned int *, unsigned int *, pointer);
static int  TDFXQueryImageAttributes(ScrnInfoPtr, int, unsigned short *, unsigned short *,  int *, int *);

static void TDFXInitOffscreenImages(ScreenPtr);

/*
 * ADAPTOR INFORMATION
 */

static XF86VideoEncodingRec OverlayEncoding[] =
{
   { 0, "XV_IMAGE", 2048, 2048, {1, 1} }
};

static XF86VideoEncodingRec TextureEncoding[] =
{
   { 0, "XV_IMAGE", 1024, 1024, {1, 1} }
};

static XF86VideoFormatRec OverlayFormats[] = 
{
   {8, TrueColor}, {8, DirectColor}, {8, PseudoColor},
   {8, GrayScale}, {8, StaticGray}, {8, StaticColor},
   {15, TrueColor}, {16, TrueColor}, {24, TrueColor},
   {15, DirectColor}, {16, DirectColor}, {24, DirectColor}
};

static XF86VideoFormatRec TextureFormats[] = 
{
   {15, TrueColor}, {16, TrueColor}, {24, TrueColor}
};

static XF86AttributeRec OverlayAttributes[] =
{
   {XvSettable | XvGettable, 0, (1 << 24) - 1, "XV_COLORKEY"},
   {XvSettable | XvGettable, 0, 1, "XV_FILTER_QUALITY"}
};

static XF86AttributeRec TextureAttributes[] =
{
   {XvSettable | XvGettable, 0, (1 << 24) - 1, "XV_COLORKEY"},
   {XvSettable | XvGettable, 0, 1, "XV_FILTER_QUALITY"}
};

static XF86ImageRec OverlayImages[] =
{
  XVIMAGE_YUY2, XVIMAGE_UYVY, XVIMAGE_YV12, XVIMAGE_I420
};

static XF86ImageRec TextureImages[] =
{
  XVIMAGE_YV12, XVIMAGE_I420
};

/*
 * COMMON SETUP FUNCTIONS
 */

void TDFXInitVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
    XF86VideoAdaptorPtr newAdaptor = NULL;
    TDFXPtr pTDFX = TDFXPTR(pScrn);
    int num_adaptors;

    /* The hardware can't convert YUV->8 bit color */
    if(pTDFX->cpp == 1)
      return;
    
    if(!pTDFX->AccelInfoRec || !pTDFX->AccelInfoRec->FillSolidRects)
	return;

    if (!pTDFX->TextureXvideo) {
	/* Offscreen support for Overlay only */
    	TDFXInitOffscreenImages(pScreen);

    	/* Overlay adaptor */
        newAdaptor = TDFXSetupImageVideoOverlay(pScreen);
    } else {
    	/* Texture adaptor */
        newAdaptor = TDFXSetupImageVideoTexture(pScreen);
    }

    num_adaptors = xf86XVListGenericAdaptors(pScrn, &adaptors);

    if(newAdaptor) {
	if (!num_adaptors) {
	    num_adaptors = 1;
	    adaptors = &newAdaptor;
	} else {
            newAdaptors = 
		xalloc((num_adaptors + 1) * sizeof(XF86VideoAdaptorPtr*));
            if(newAdaptors) {
                memcpy(newAdaptors, adaptors, num_adaptors * 
						sizeof(XF86VideoAdaptorPtr));
                newAdaptors[num_adaptors] = newAdaptor;
                adaptors = newAdaptors;
                num_adaptors++;
            }
	}
    }

    if(num_adaptors)
        xf86XVScreenInit(pScreen, adaptors, num_adaptors);

    if(newAdaptors)
        xfree(newAdaptors);
}


void TDFXCloseVideo (ScreenPtr pScreen)
{
}


static XF86VideoAdaptorPtr
TDFXAllocAdaptor(ScrnInfoPtr pScrn, int numberPorts)
{
    XF86VideoAdaptorPtr adapt;
    TDFXPtr pTDFX = TDFXPTR(pScrn);
    TDFXPortPrivPtr pPriv;

    if(!(adapt = xf86XVAllocateVideoAdaptorRec(pScrn)))
        return NULL;

    if(!(pPriv = xcalloc(1, sizeof(TDFXPortPrivRec) + (numberPorts * sizeof(DevUnion)))))
    {
        xfree(adapt);
        return NULL;
    }

    adapt->pPortPrivates = (DevUnion*)(&pPriv[1]);
    adapt->pPortPrivates[0].ptr = (pointer)pPriv;

    xvColorKey = MAKE_ATOM("XV_COLORKEY");
    xvFilterQuality = MAKE_ATOM("XV_FILTER_QUALITY");

    pPriv->colorKey = pTDFX->videoKey;
    pPriv->videoStatus = 0;
    pPriv->filterQuality = 1;
  
    return adapt;
}


static XF86VideoAdaptorPtr
TDFXSetupImageVideoOverlay(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    TDFXPtr pTDFX = TDFXPTR(pScrn);
    TDFXPortPrivPtr pPriv;
    XF86VideoAdaptorPtr adapt;

    if(!(adapt = TDFXAllocAdaptor(pScrn, TDFX_MAX_OVERLAY_PORTS)))
        return NULL;

    adapt->type = XvWindowMask | XvInputMask | XvImageMask;
    adapt->flags = VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT;
    adapt->name = "3dfx Video Overlay";
    adapt->nPorts = TDFX_MAX_OVERLAY_PORTS;
    adapt->nEncodings = sizeof(OverlayEncoding) / sizeof(XF86VideoEncodingRec);
    adapt->pEncodings = OverlayEncoding;
    adapt->nFormats = sizeof(OverlayFormats) / sizeof(XF86VideoFormatRec);
    adapt->pFormats = OverlayFormats;
    adapt->nAttributes = sizeof(OverlayAttributes) / sizeof(XF86AttributeRec);
    adapt->pAttributes = OverlayAttributes;
    adapt->nImages = sizeof(OverlayImages) / sizeof(XF86ImageRec);
    adapt->pImages = OverlayImages;
    adapt->PutVideo = NULL;
    adapt->PutStill = NULL;
    adapt->GetVideo = NULL;
    adapt->GetStill = NULL;
    adapt->StopVideo = TDFXStopVideoOverlay;
    adapt->SetPortAttribute = TDFXSetPortAttributeOverlay;
    adapt->GetPortAttribute = TDFXGetPortAttributeOverlay;
    adapt->QueryBestSize = TDFXQueryBestSize;
    adapt->PutImage = TDFXPutImageOverlay;
    adapt->QueryImageAttributes = TDFXQueryImageAttributes;

    pTDFX->overlayAdaptor = adapt;

    pPriv = (TDFXPortPrivPtr)(adapt->pPortPrivates[0].ptr);
    REGION_INIT(pScreen, &(pPriv->clip), NullBox, 0);

    TDFXResetVideoOverlay(pScrn);

    return adapt;
}

static XF86VideoAdaptorPtr
TDFXSetupImageVideoTexture(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    TDFXPtr pTDFX = TDFXPTR(pScrn);
    XF86VideoAdaptorPtr adapt;
    int i;

    if(!(adapt = TDFXAllocAdaptor(pScrn, TDFX_MAX_TEXTURE_PORTS)))
        return NULL;

    adapt->type = XvWindowMask | XvInputMask | XvImageMask;
    adapt->flags = 0;
    adapt->name = "3dfx Video Texture";
    adapt->nPorts = TDFX_MAX_TEXTURE_PORTS;
    adapt->nEncodings = sizeof(TextureEncoding) / sizeof(XF86VideoEncodingRec);
    adapt->pEncodings = TextureEncoding;
    adapt->nFormats = sizeof(TextureFormats) / sizeof(XF86VideoFormatRec);
    adapt->pFormats = TextureFormats;
    adapt->nAttributes = sizeof(TextureAttributes) / sizeof(XF86AttributeRec);
    adapt->pAttributes = TextureAttributes;
    adapt->nImages = sizeof(TextureImages) / sizeof(XF86ImageRec);
    adapt->pImages = TextureImages;
    adapt->PutVideo = NULL;
    adapt->PutStill = NULL;
    adapt->GetVideo = NULL;
    adapt->GetStill = NULL;
    adapt->StopVideo = TDFXStopVideoTexture;
    adapt->SetPortAttribute = TDFXSetPortAttributeTexture;
    adapt->GetPortAttribute = TDFXGetPortAttributeTexture;
    adapt->QueryBestSize = TDFXQueryBestSize;
    adapt->PutImage = TDFXPutImageTexture;
    adapt->QueryImageAttributes = TDFXQueryImageAttributes;

    for(i = 0; i < TDFX_MAX_TEXTURE_PORTS; i++)
        adapt->pPortPrivates[i].val = i;

    pTDFX->textureAdaptor = adapt;

    return adapt;
}


/*
 * MISCELLANEOUS ROUTINES
 */

static Bool
TDFXClipVideo(
  BoxPtr dst,
  INT32 *xa,
  INT32 *xb,
  INT32 *ya,
  INT32 *yb,
  RegionPtr reg,
  INT32 width,
  INT32 height
){
    INT32 vscale, hscale, delta;
    BoxPtr extents = REGION_EXTENTS(DummyScreen, reg);
    int diff;

    hscale = ((*xb - *xa) << 16) / (dst->x2 - dst->x1);
    vscale = ((*yb - *ya) << 16) / (dst->y2 - dst->y1);

    *xa <<= 16; *xb <<= 16;
    *ya <<= 16; *yb <<= 16;

    diff = extents->x1 - dst->x1;
    if(diff > 0) {
        dst->x1 = extents->x1;
        *xa += diff * hscale;
    }
    diff = dst->x2 - extents->x2;
    if(diff > 0) {
        dst->x2 = extents->x2;
        *xb -= diff * hscale;
    }
    diff = extents->y1 - dst->y1;
    if(diff > 0) {
        dst->y1 = extents->y1;
        *ya += diff * vscale;
    }
    diff = dst->y2 - extents->y2;
    if(diff > 0) {
        dst->y2 = extents->y2;
        *yb -= diff * vscale;
    }

    if(*xa < 0) {
        diff =  (- *xa + hscale - 1)/ hscale;
        dst->x1 += diff;
        *xa += diff * hscale;
    }
    delta = *xb - (width << 16);
    if(delta > 0) {
        diff = (delta + hscale - 1)/ hscale;
        dst->x2 -= diff;
        *xb -= diff * hscale;
    }
    if(*xa >= *xb) return FALSE;

    if(*ya < 0) {
        diff =  (- *ya + vscale - 1)/ vscale;
        dst->y1 += diff;
        *ya += diff * vscale;
    }
    delta = *yb - (height << 16);
    if(delta > 0) {
        diff = (delta + vscale - 1)/ vscale;
        dst->y2 -= diff;
        *yb -= diff * vscale;
    }
    if(*ya >= *yb) return FALSE;

    if((dst->x1 != extents->x1) || (dst->x2 != extents->x2) ||
       (dst->y1 != extents->y1) || (dst->y2 != extents->y2))
    {
        RegionRec clipReg;
        REGION_INIT(DummyScreen, &clipReg, dst, 1);
        REGION_INTERSECT(DummyScreen, reg, reg, &clipReg);
        REGION_UNINIT(DummyScreen, &clipReg);
    }
    return TRUE;
}

static int
TDFXQueryImageAttributes(
    ScrnInfoPtr pScrn,
    int id,
    unsigned short *w, unsigned short *h,
    int *pitches, int *offsets
){
    int size, tmp;

    if(*w > 1024) *w = 1024;
    if(*h > 1024) *h = 1024;

    *w = (*w + 1) & ~1;
    if(offsets) offsets[0] = 0;

    switch(id) {
    case FOURCC_YV12:
    case FOURCC_I420:
        *h = (*h + 1) & ~1;
        size = (*w + 3) & ~3;
        if(pitches) pitches[0] = size;
        size *= *h;
        if(offsets) offsets[1] = size;
        tmp = ((*w >> 1) + 3) & ~3;
        if(pitches) pitches[1] = pitches[2] = tmp;
        tmp *= (*h >> 1);
        size += tmp;
        if(offsets) offsets[2] = size;
        size += tmp;
        break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
    default:
        size = *w << 1;
        if(pitches) pitches[0] = size;
        size *= *h;
        break;
    }

    return size;
}


static int
TDFXSetPortAttributeOverlay(
  ScrnInfoPtr pScrn,
  Atom attribute,
  INT32 value,
  pointer data
){

  TDFXPortPrivPtr pPriv = (TDFXPortPrivPtr)data;
  TDFXPtr pTDFX = TDFXPTR(pScrn);

  if(attribute == xvColorKey) {
        pPriv->colorKey = value;
        pTDFX->writeLong(pTDFX, VIDCHROMAMIN, pPriv->colorKey);
        pTDFX->writeLong(pTDFX, VIDCHROMAMAX, pPriv->colorKey);
        REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
  } else if(attribute == xvFilterQuality) {
        if((value < 0) || (value > 1))
           return BadValue;
        pPriv->filterQuality = value;
  } else return BadMatch;

  return Success;
}

static int
TDFXGetPortAttributeOverlay(
  ScrnInfoPtr pScrn,
  Atom attribute,
  INT32 *value,
  pointer data
){
  TDFXPortPrivPtr pPriv = (TDFXPortPrivPtr)data;

  if(attribute == xvColorKey) {
        *value = pPriv->colorKey;
  } else if(attribute == xvFilterQuality) {
        *value = pPriv->filterQuality;
  } else return BadMatch;

  return Success;
}


static int 
TDFXSetPortAttributeTexture(
  ScrnInfoPtr pScrn, 
  Atom attribute,
  INT32 value, 
  pointer data
) {
  return Success;
}


static int 
TDFXGetPortAttributeTexture(
  ScrnInfoPtr pScrn, 
  Atom attribute,
  INT32 *value, 
  pointer data
){
  return Success;
}


static void
TDFXQueryBestSize(
  ScrnInfoPtr pScrn,
  Bool motion,
  short vid_w, short vid_h,
  short drw_w, short drw_h,
  unsigned int *p_w, unsigned int *p_h,
  pointer data
){
   if(vid_w > drw_w) drw_w = vid_w;
   if(vid_h > drw_h) drw_h = vid_h;
   
  *p_w = drw_w;
  *p_h = drw_h;
}


static void
TDFXCopyData(
  unsigned char *src,
  unsigned char *dst,
  int srcPitch,
  int dstPitch,
  int h,
  int w
){
#if X_BYTE_ORDER == X_BIG_ENDIAN
    w >>= 1;
    while(h--) {
      int i;
      for (i=0; i<w; i++)
       ((unsigned long *)dst)[i]=BE_WSWAP32(((unsigned long *)src)[i]);
       src += srcPitch;
       dst += dstPitch;
    }
#else
     w <<= 1;
     while(h--) {
 	memcpy(dst, src, w);
 	src += srcPitch;
 	dst += dstPitch;
     }
#endif
}

static void
TDFXCopyMungedData(
   unsigned char *src1,
   unsigned char *src2,
   unsigned char *src3,
   unsigned char *dst1,
   int srcPitch,
   int srcPitch2,
   int dstPitch,
   int h,
   int w
){
   CARD32 *dst;
   CARD8 *s1, *s2, *s3;
   int i, j;

   w >>= 1;

   for(j = 0; j < h; j++) {
        dst = (CARD32*)dst1;
        s1 = src1;  s2 = src2;  s3 = src3;
        i = w;
        while(i > 4) {
           dst[0] = BE_WSWAP32(s1[0] | (s1[1] << 16) | (s3[0] << 8) |
			(s2[0] << 24));
           dst[1] = BE_WSWAP32(s1[2] | (s1[3] << 16) | (s3[1] << 8) |
			(s2[1] << 24));
           dst[2] = BE_WSWAP32(s1[4] | (s1[5] << 16) | (s3[2] << 8) |
			(s2[2] << 24));
           dst[3] = BE_WSWAP32(s1[6] | (s1[7] << 16) | (s3[3] << 8) |
			(s2[3] << 24));
 	   dst += 4; s2 += 4; s3 += 4; s1 += 8;
 	   i -= 4;
 	}
 	while(i--) {
	   dst[0] = BE_WSWAP32(s1[0] | (s1[1] << 16) | (s3[0] << 8) |
				(s2[0] << 24));
 	   dst++; s2++; s3++;
 	   s1 += 2;
 	}

        dst1 += dstPitch;
        src1 += srcPitch;
        if(j & 1) {
            src2 += srcPitch2;
            src3 += srcPitch2;
        }
   }
}


/*
 * TEXTURE DRAWING FUNCTIONS
 */


static void
TDFXStopVideoTexture(ScrnInfoPtr pScrn, pointer data, Bool cleanup)
{
  TDFXPtr pTDFX = TDFXPTR(pScrn);
  TDFXPortPrivPtr pPriv = (TDFXPortPrivPtr)data;

  REGION_EMPTY(pScrn->pScreen, &pPriv->clip);

  if (cleanup) {
     if(pTDFX->textureBuffer) {
        xf86FreeOffscreenArea(pTDFX->textureBuffer);
        pTDFX->textureBuffer = NULL;
     }
  }
}


static void
TDFXScreenToScreenYUVStretchBlit (ScrnInfoPtr pScrn,
                                  short src_x1, short src_y1,
                                  short src_x2, short src_y2,
                                  short dst_x1, short dst_y1,
                                  short dst_x2, short dst_y2)
{
   TDFXPtr pTDFX = TDFXPTR(pScrn);
   /* reformulate the paramaters the way the hardware wants them */
   INT32 src_x = src_x1 & 0x1FFF;
   INT32 src_y = src_y1 & 0x1FFF;
   INT32 dst_x = dst_x1 & 0x1FFF;
   INT32 dst_y = dst_y1 & 0x1FFF;
   INT32 src_w = (src_x2 - src_x1) & 0x1FFF;
   INT32 src_h = (src_y2 - src_y1) & 0x1FFF;
   INT32 dst_w = (dst_x2 - dst_x1) & 0x1FFF;
   INT32 dst_h = (dst_y2 - dst_y1) & 0x1FFF;
   /* Setup for blit src and dest */
   TDFXMakeRoom(pTDFX, 4);
   DECLARE(SSTCP_DSTSIZE|SSTCP_SRCSIZE|SSTCP_DSTXY|SSTCP_COMMAND/*|SSTCP_COMMANDEXTRA*/);
   /* TDFXWriteLong(pTDFX, SST_2D_COMMANDEXTRA, SST_COMMANDEXTRA_VSYNC);*/
   TDFXWriteLong(pTDFX, SST_2D_SRCSIZE, src_w | (src_h<<16));
   TDFXWriteLong(pTDFX, SST_2D_DSTSIZE, dst_w | (dst_h<<16));
   TDFXWriteLong(pTDFX, SST_2D_DSTXY,   dst_x | (dst_y<<16));
   TDFXWriteLong(pTDFX, SST_2D_COMMAND, SST_2D_SCRNTOSCRNSTRETCH | 0xCC000000);
   /* Write to the launch area to start the blit */
   TDFXMakeRoom(pTDFX, 1);
   DECLARE_LAUNCH(1, 0);
   TDFXWriteLong(pTDFX, SST_2D_LAUNCH, (src_x<<1) | (src_y<<16));
   /* Wait for it to happen */
   TDFXSendNOPFifo2D(pScrn);
}


static void
YUVPlanarToPacked (ScrnInfoPtr pScrn,
                   short src_x, short src_y,
                   short src_h, short src_w,
                   int id, unsigned char *buf,
                   short width, short height,
                   FBAreaPtr fbarea)
{
   TDFXPtr pTDFX = TDFXPTR(pScrn);
   unsigned char *psrc, *pdst;
   int count;
   int baseaddr;
   INT32 yuvBaseAddr, yuvStride;

   /* Save these registers so I can restore them when we are done. */
   yuvBaseAddr = TDFXReadLongMMIO(pTDFX, YUVBASEADDR);
   yuvStride =   TDFXReadLongMMIO(pTDFX, YUVSTRIDE);

   /* Set yuvBaseAddress and yuvStride. */
   baseaddr = pTDFX->fbOffset + pTDFX->cpp * fbarea->box.x1 + pTDFX->stride * fbarea->box.y1;
   TDFXWriteLongMMIO(pTDFX, YUVSTRIDE, pTDFX->stride);
   TDFXWriteLongMMIO(pTDFX, YUVBASEADDR, baseaddr);

   /* Copy Y plane (twice as much Y as U or V) */
   psrc = buf;
   psrc += (src_x & ~0x1) + src_y * width;
   pdst = pTDFX->MMIOBase[0] + YUV_Y_BASE;
   TDFXCopyData(psrc, pdst, width, 1024, src_h, src_w + (src_x & 0x1));

   /* Copy V plane */
   psrc = buf + width * height;
   psrc += (src_x >> 1) + (src_y >> 1) * (width >> 1);
   pdst = pTDFX->MMIOBase[0] + YUV_V_BASE;
   TDFXCopyData(psrc, pdst, width >> 1, 1024, src_h >> 1, src_w >> 1);

   /* Copy U plane */
   psrc = buf + width * height + (width >> 1) * (height >> 1);
   psrc += (src_x >> 1) + (src_y >> 1) * (width >> 1);
   pdst = pTDFX->MMIOBase[0] + YUV_U_BASE;
   TDFXCopyData(psrc, pdst, width >> 1, 1024, src_h >> 1, src_w >> 1);

   /* IDLE until the copy finished, timeout for safety */
   for (count = 0; count < 1000; count++) 
     if (!((TDFXReadLongMMIO(pTDFX, STATUS) & SST_BUSY)))
       break;

   /* Restore trashed registers */
   TDFXWriteLongMMIO(pTDFX, YUVBASEADDR, yuvBaseAddr);
   TDFXWriteLongMMIO(pTDFX, YUVSTRIDE, yuvStride);  

   /* Wait for it to happen */
   TDFXSendNOPFifo2D(pScrn);
}


static int 
TDFXPutImageTexture( 
             ScrnInfoPtr pScrn, 
             short src_x, short src_y, 
             short drw_x, short drw_y,
             short src_w, short src_h, 
             short drw_w, short drw_h,
             int id, unsigned char* buf, 
             short width, short height, 
             Bool sync,
             RegionPtr clipBoxes, pointer data
             )
{
   TDFXPtr pTDFX = TDFXPTR(pScrn);
   BoxPtr pbox;
   int nbox;
   int format;

   /* Check the source format */
   if (id == FOURCC_YV12)      format = SST_2D_FORMAT_YUYV;
   else if (id == FOURCC_UYVY) format = SST_2D_FORMAT_UYVY;
   else                        return BadAlloc;

   /* Get a buffer to store the packed YUV data */
   if (!(pTDFX->textureBuffer = TDFXAllocateMemoryArea(pScrn, pTDFX->textureBuffer, src_w, src_h)))
        return BadAlloc;

   /* Pack the YUV data in offscreen memory using YUV framebuffer (0x[CDE]0000) */
   YUVPlanarToPacked (pScrn, src_x, src_y, src_h, src_w,
                      id, buf, width, height,
                      pTDFX->textureBuffer);

   /* Setup source and destination pixel formats (yuv -> rgb) */
   TDFXMakeRoom(pTDFX, 2);
   DECLARE(SSTCP_SRCFORMAT|SSTCP_DSTFORMAT);
   TDFXWriteLong(pTDFX, SST_2D_DSTFORMAT, pTDFX->stride|((pTDFX->cpp+1)<<16));
   TDFXWriteLong(pTDFX, SST_2D_SRCFORMAT, pTDFX->stride|((format)<<16));

   /* Blit packed YUV data from offscreen memory, respecting clips */
#define SRC_X1 (pTDFX->textureBuffer->box.x1)
#define SRC_Y1 (pTDFX->textureBuffer->box.y1)
#define SCALEX(dx) ((int)(((dx) * src_w) / drw_w))
#define SCALEY(dy) ((int)(((dy) * src_h) / drw_h))
   for (nbox = REGION_NUM_RECTS(clipBoxes),
        pbox = REGION_RECTS(clipBoxes); nbox > 0; nbox--, pbox++)
   {
     TDFXScreenToScreenYUVStretchBlit (pScrn,
        SRC_X1 + SCALEX(pbox->x1 - drw_x), 
        SRC_Y1 + SCALEY(pbox->y1 - drw_y),
        SRC_X1 + SCALEX(pbox->x2 - drw_x), 
        SRC_Y1 + SCALEY(pbox->y2 - drw_y),
        pbox->x1, pbox->y1,
        pbox->x2, pbox->y2);
   }

   /* Restore the WAX registers we trashed */
   TDFXMakeRoom(pTDFX, 2);
   DECLARE(SSTCP_SRCFORMAT|SSTCP_DSTFORMAT);
   TDFXWriteLong(pTDFX, SST_2D_DSTFORMAT, pTDFX->sst2DDstFmtShadow);
   TDFXWriteLong(pTDFX, SST_2D_SRCFORMAT, pTDFX->sst2DSrcFmtShadow);

   /* Wait for it to happen */
   TDFXSendNOPFifo2D(pScrn);

   return Success;
}


/*
 * COMMON DRAWING FUNCTIONS
 */

static Bool
RegionsEqual(RegionPtr A, RegionPtr B)
{
    int *dataA, *dataB;
    int num;

    num = REGION_NUM_RECTS(A);
    if(num != REGION_NUM_RECTS(B))
        return FALSE;

    if((A->extents.x1 != B->extents.x1) ||
       (A->extents.x2 != B->extents.x2) ||
       (A->extents.y1 != B->extents.y1) ||
       (A->extents.y2 != B->extents.y2))
        return FALSE;

    dataA = (int*)REGION_RECTS(A);
    dataB = (int*)REGION_RECTS(B);

    while(num--) {
        if((dataA[0] != dataB[0]) || (dataA[1] != dataB[1]))
           return FALSE;
        dataA += 2;
        dataB += 2;
    }

    return TRUE;
}

/*
 * OVERLAY DRAWING FUNCTIONS
 */


static void
TDFXResetVideoOverlay(ScrnInfoPtr pScrn)
{
    TDFXPtr pTDFX = TDFXPTR(pScrn);
    TDFXPortPrivPtr pPriv = pTDFX->overlayAdaptor->pPortPrivates[0].ptr;

    /* reset the video */
    pTDFX->ModeReg.vidcfg &= ~VIDPROCCFGMASK;
    pTDFX->writeLong(pTDFX, VIDPROCCFG, pTDFX->ModeReg.vidcfg);
    pTDFX->writeLong(pTDFX, RGBMAXDELTA, 0x0080808);
    pTDFX->writeLong(pTDFX, VIDCHROMAMIN, pPriv->colorKey);
    pTDFX->writeLong(pTDFX, VIDCHROMAMAX, pPriv->colorKey);
}


static void
TDFXStopVideoOverlay(ScrnInfoPtr pScrn, pointer data, Bool cleanup)
{
  TDFXPtr pTDFX = TDFXPTR(pScrn);
  TDFXPortPrivPtr pPriv = (TDFXPortPrivPtr)data;

  REGION_EMPTY(pScrn->pScreen, &pPriv->clip);

  if(cleanup) {
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
        pTDFX->ModeReg.vidcfg &= ~VIDPROCCFGMASK;
        pTDFX->writeLong(pTDFX, VIDPROCCFG, pTDFX->ModeReg.vidcfg);
     }
     if(pTDFX->overlayBuffer) {
        xf86FreeOffscreenLinear(pTDFX->overlayBuffer);
        pTDFX->overlayBuffer = NULL;
     }
     pPriv->videoStatus = 0;
  } else {
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
        pPriv->videoStatus |= OFF_TIMER;
        pPriv->offTime = currentTime.milliseconds + OFF_DELAY;
     }
  }
}


static void
TDFXDisplayVideoOverlay(
    ScrnInfoPtr pScrn,
    int id,
    int offset,
    short width, short height,
    int pitch,
    int left, int right, int top,
    BoxPtr dstBox,
    short src_w, short src_h,
    short drw_w, short drw_h
){
    TDFXPtr pTDFX = TDFXPTR(pScrn);
    TDFXPortPrivPtr pPriv = pTDFX->overlayAdaptor->pPortPrivates[0].ptr;
    int dudx, dvdy;

    dudx = (src_w << 20) / drw_w;
    dvdy = (src_h << 20) / drw_h;

    offset += ((left >> 16) & ~1) << 1;
    left = (left & 0x0001ffff) << 3;

    pTDFX->ModeReg.vidcfg &= ~VIDPROCCFGMASK;
    pTDFX->ModeReg.vidcfg |= 0x00000320;

    if(drw_w != src_w)       pTDFX->ModeReg.vidcfg |= (1 << 14);
    if(drw_h != src_h)       pTDFX->ModeReg.vidcfg |= (1 << 15);
    if(id == FOURCC_UYVY)    pTDFX->ModeReg.vidcfg |= (6 << 21);
    else                     pTDFX->ModeReg.vidcfg |= (5 << 21);
    if(pScrn->depth == 8)    pTDFX->ModeReg.vidcfg |= (1 << 11);
    /* can't do bilinear filtering when in 2X mode */
    if(pPriv->filterQuality && !(pTDFX->ModeReg.vidcfg & SST_VIDEO_2X_MODE_EN))
	pTDFX->ModeReg.vidcfg |= (3 << 16);
    pTDFX->writeLong(pTDFX, VIDPROCCFG, pTDFX->ModeReg.vidcfg);

    pTDFX->writeLong(pTDFX, VIDOVERLAYSTARTCOORDS, dstBox->x1 | (dstBox->y1 << 12));
    pTDFX->writeLong(pTDFX, VIDOVERLAYENDSCREENCOORDS, (dstBox->x2 - 1) | ((dstBox->y2 - 1) << 12));
    pTDFX->writeLong(pTDFX, VIDOVERLAYDUDX, dudx);
    pTDFX->writeLong(pTDFX, VIDOVERLAYDUDXOFFSETSRCWIDTH, left | (src_w << 20));
    pTDFX->writeLong(pTDFX, VIDOVERLAYDVDY, dvdy);
    pTDFX->writeLong(pTDFX, VIDOVERLAYDVDYOFFSET, (top & 0x0000ffff) << 3);

    pTDFX->ModeReg.stride &= 0x0000ffff;
    pTDFX->ModeReg.stride |= pitch << 16;
    pTDFX->writeLong(pTDFX, VIDDESKTOPOVERLAYSTRIDE, pTDFX->ModeReg.stride);
    pTDFX->writeLong(pTDFX, SST_3D_LEFTOVERLAYBUF, offset & ~3);
    pTDFX->writeLong(pTDFX, VIDINADDR0, offset & ~3);
}


static int
TDFXPutImageOverlay(
  ScrnInfoPtr pScrn,
  short src_x, short src_y,
  short drw_x, short drw_y,
  short src_w, short src_h,
  short drw_w, short drw_h,
  int id, unsigned char* buf,
  short width, short height,
  Bool Sync,
  RegionPtr clipBoxes, pointer data
){
   TDFXPtr pTDFX = TDFXPTR(pScrn);
   TDFXPortPrivPtr pPriv = (TDFXPortPrivPtr)data;
   INT32 xa, xb, ya, yb;
   unsigned char *dst_start;
   int pitch, new_size, offset;
   int s2offset = 0, s3offset = 0;
   int srcPitch = 0, srcPitch2 = 0;
   int dstPitch;
   int top, left, npixels, nlines, bpp;
   BoxRec dstBox;
   CARD32 tmp;

   /*
    * s2offset, s3offset - byte offsets into U and V plane of the
    *                      source where copying starts.  Y plane is
    *                      done by editing "buf".
    *
    * offset - byte offset to the first line of the destination.
    *
    * dst_start - byte address to the first displayed pel.
    *
    */

   if(src_w > drw_w) drw_w = src_w;
   if(src_h > drw_h) drw_h = src_h;

   /* Clip */
   xa = src_x;
   xb = src_x + src_w;
   ya = src_y;
   yb = src_y + src_h;

   dstBox.x1 = drw_x;
   dstBox.x2 = drw_x + drw_w;
   dstBox.y1 = drw_y;
   dstBox.y2 = drw_y + drw_h;

   if(!TDFXClipVideo(&dstBox, &xa, &xb, &ya, &yb, clipBoxes, width, height))
        return Success;

   dstBox.x1 -= pScrn->frameX0;
   dstBox.x2 -= pScrn->frameX0;
   dstBox.y1 -= pScrn->frameY0;
   dstBox.y2 -= pScrn->frameY0;

   bpp = pScrn->bitsPerPixel >> 3;
   pitch = bpp * pScrn->displayWidth;

   switch(id) {
   case FOURCC_YV12:
   case FOURCC_I420:
        dstPitch = ((width << 1) + 3) & ~3;
        new_size = ((dstPitch * height) + bpp - 1) / bpp;
        srcPitch = (width + 3) & ~3;
        s2offset = srcPitch * height;
        srcPitch2 = ((width >> 1) + 3) & ~3;
        s3offset = (srcPitch2 * (height >> 1)) + s2offset;
        break;
   case FOURCC_UYVY:
   case FOURCC_YUY2:
   default:
        dstPitch = ((width << 1) + 3) & ~3;
        new_size = ((dstPitch * height) + bpp - 1) / bpp;
        srcPitch = (width << 1);
        break;
   }

   if(!(pTDFX->overlayBuffer = TDFXAllocateMemoryLinear(pScrn, pTDFX->overlayBuffer, new_size)))
        return BadAlloc;

   /* copy data */
   top = ya >> 16;
   left = (xa >> 16) & ~1;
   npixels = ((((xb + 0xffff) >> 16) + 1) & ~1) - left;

   offset = (pTDFX->overlayBuffer->offset * bpp) + (top * dstPitch) + pTDFX->fbOffset;
   dst_start = pTDFX->FbBase + offset;

   switch(id) {
    case FOURCC_YV12:
    case FOURCC_I420:
        top &= ~1;
        dst_start += left << 1;
        tmp = ((top >> 1) * srcPitch2) + (left >> 1);
        s2offset += tmp;
        s3offset += tmp;
        if(id == FOURCC_I420) {
           tmp = s2offset;
           s2offset = s3offset;
           s3offset = tmp;
        }
        nlines = ((((yb + 0xffff) >> 16) + 1) & ~1) - top;
        TDFXCopyMungedData(buf + (top * srcPitch) + left, buf + s2offset,
                           buf + s3offset, dst_start, srcPitch, srcPitch2,
                           dstPitch, nlines, npixels);
        break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
    default:
        left <<= 1;
        buf += (top * srcPitch) + left;
        nlines = ((yb + 0xffff) >> 16) - top;
        dst_start += left;
        TDFXCopyData(buf, dst_start, srcPitch, dstPitch, nlines, npixels);
        break;
    }

    if(!RegionsEqual(&pPriv->clip, clipBoxes)) {
        REGION_COPY(pScreen, &pPriv->clip, clipBoxes);
        (*pTDFX->AccelInfoRec->FillSolidRects)(pScrn, pPriv->colorKey, 
                                               GXcopy, ~0,
                                               REGION_NUM_RECTS(clipBoxes),
                                               REGION_RECTS(clipBoxes));
    }

    TDFXDisplayVideoOverlay(pScrn, id, offset, width, height, dstPitch, xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h);

    pPriv->videoStatus = CLIENT_VIDEO_ON;

    pTDFX->VideoTimerCallback = TDFXVideoTimerCallback;

    return Success;
}


static void
TDFXVideoTimerCallback(ScrnInfoPtr pScrn, Time time)
{
    TDFXPtr pTDFX = TDFXPTR(pScrn);
    TDFXPortPrivPtr pPriv = pTDFX->overlayAdaptor->pPortPrivates[0].ptr;

    if(pPriv->videoStatus & TIMER_MASK) {
        if(pPriv->videoStatus & OFF_TIMER) {
            if(pPriv->offTime < time) {
                pTDFX->ModeReg.vidcfg &= ~VIDPROCCFGMASK;
                pTDFX->writeLong(pTDFX, VIDPROCCFG, pTDFX->ModeReg.vidcfg);
                pPriv->videoStatus = FREE_TIMER;
                pPriv->freeTime = time + FREE_DELAY;
            }
        } else
        if(pPriv->videoStatus & FREE_TIMER) {
            if(pPriv->freeTime < time) {
                if(pTDFX->overlayBuffer) {
                   xf86FreeOffscreenLinear(pTDFX->overlayBuffer);
                   pTDFX->overlayBuffer = NULL;
                }
                pPriv->videoStatus = 0;
                pTDFX->VideoTimerCallback = NULL;
            }
        }
    } else  /* shouldn't get here */
        pTDFX->VideoTimerCallback = NULL;
}


/*
 * MEMORY MANAGEMENT
 */


static FBAreaPtr
TDFXAllocateMemoryArea (ScrnInfoPtr pScrn, FBAreaPtr area, int width, int height)
{
  TDFXPtr pTDFX = TDFXPTR(pScrn);
  ScreenPtr pScreen;
  FBAreaPtr new_area;

  if (area) {
    if ((area->box.x2 - area->box.x1 >= width) &&
        (area->box.y2 - area->box.y1 >= height))
      return area;

    if (xf86ResizeOffscreenArea(area, width, height))
      return area;

    xf86FreeOffscreenArea(area);
  }

  pScreen = screenInfo.screens[pScrn->scrnIndex];

  new_area = xf86AllocateOffscreenArea(pScreen, width, height, pTDFX->cpp, NULL, NULL, NULL);

  if (!new_area) {
    int max_width, max_height;

    xf86QueryLargestOffscreenArea(pScreen, &max_width, &max_height, pTDFX->cpp, 0, PRIORITY_EXTREME);

    if (max_width < width || max_height < height)
      return NULL;

    xf86PurgeUnlockedOffscreenAreas(pScreen);
    new_area = xf86AllocateOffscreenArea(pScreen, width, height, pTDFX->cpp, NULL, NULL, NULL);
  }

  return new_area;
}


static FBLinearPtr
TDFXAllocateMemoryLinear (ScrnInfoPtr pScrn, FBLinearPtr linear, int size)
{
   ScreenPtr pScreen;
   FBLinearPtr new_linear;

   if(linear) {
        if(linear->size >= size)
           return linear;

        if(xf86ResizeOffscreenLinear(linear, size))
           return linear;

        xf86FreeOffscreenLinear(linear);
   }

   pScreen = screenInfo.screens[pScrn->scrnIndex];

   new_linear = xf86AllocateOffscreenLinear(pScreen, size, 4, NULL, NULL, NULL);

   if(!new_linear) {
        int max_size;

        xf86QueryLargestOffscreenLinear(pScreen, &max_size, 4, PRIORITY_EXTREME);

        if(max_size < size)
           return NULL;

        xf86PurgeUnlockedOffscreenAreas(pScreen);
        new_linear = xf86AllocateOffscreenLinear(pScreen, size, 4, NULL, NULL, NULL);
   }

   return new_linear;
}

/****************** Offscreen stuff ***************/

typedef struct {
  FBLinearPtr linear;
  Bool isOn;
} OffscreenPrivRec, * OffscreenPrivPtr;

static int 
TDFXAllocateSurface(
    ScrnInfoPtr pScrn,
    int id,
    unsigned short w, 	
    unsigned short h,
    XF86SurfacePtr surface
){
    TDFXPtr pTDFX = TDFXPTR(pScrn);
    FBLinearPtr linear;
    int pitch, fbpitch, size, bpp;
    OffscreenPrivPtr pPriv;

    if((w > 2048) || (h > 2048))
	return BadAlloc;

    w = (w + 1) & ~1;
    pitch = ((w << 1) + 15) & ~15;
    bpp = pScrn->bitsPerPixel >> 3;
    fbpitch = bpp * pScrn->displayWidth;
    size = ((pitch * h) + bpp - 1) / bpp;

    if(!(linear = TDFXAllocateMemoryLinear(pScrn, NULL, size)))
	return BadAlloc;

    surface->width = w;
    surface->height = h;

    if(!(surface->pitches = xalloc(sizeof(int)))) {
	xf86FreeOffscreenLinear(linear);
	return BadAlloc;
    }
    if(!(surface->offsets = xalloc(sizeof(int)))) {
	xfree(surface->pitches);
	xf86FreeOffscreenLinear(linear);
	return BadAlloc;
    }
    if(!(pPriv = xalloc(sizeof(OffscreenPrivRec)))) {
	xfree(surface->pitches);
	xfree(surface->offsets);
	xf86FreeOffscreenLinear(linear);
	return BadAlloc;
    }

    pPriv->linear = linear;
    pPriv->isOn = FALSE;

    surface->pScrn = pScrn;
    surface->id = id;   
    surface->pitches[0] = pitch;
    surface->offsets[0] = pTDFX->fbOffset + (linear->offset * bpp);
    surface->devPrivate.ptr = (pointer)pPriv;

    return Success;
}

static int 
TDFXStopSurface(
    XF86SurfacePtr surface
){
    OffscreenPrivPtr pPriv = (OffscreenPrivPtr)surface->devPrivate.ptr;

    if(pPriv->isOn) {
	TDFXPtr pTDFX = TDFXPTR(surface->pScrn);
        pTDFX->ModeReg.vidcfg &= ~VIDPROCCFGMASK;
        pTDFX->writeLong(pTDFX, VIDPROCCFG, pTDFX->ModeReg.vidcfg);
	pPriv->isOn = FALSE;
    }

    return Success;
}


static int 
TDFXFreeSurface(
    XF86SurfacePtr surface
){
    OffscreenPrivPtr pPriv = (OffscreenPrivPtr)surface->devPrivate.ptr;

    if(pPriv->isOn)
	TDFXStopSurface(surface);
    xf86FreeOffscreenLinear(pPriv->linear);
    xfree(surface->pitches);
    xfree(surface->offsets);
    xfree(surface->devPrivate.ptr);

    return Success;
}

static int
TDFXGetSurfaceAttribute(
    ScrnInfoPtr pScrn,
    Atom attribute,
    INT32 *value
){
    return TDFXGetPortAttributeOverlay(pScrn, attribute, value, 
			(pointer)(GET_PORT_PRIVATE(pScrn)));
}

static int
TDFXSetSurfaceAttribute(
    ScrnInfoPtr pScrn,
    Atom attribute,
    INT32 value
){
    return TDFXSetPortAttributeOverlay(pScrn, attribute, value, 
			(pointer)(GET_PORT_PRIVATE(pScrn)));
}

static int 
TDFXDisplaySurface(
    XF86SurfacePtr surface,
    short src_x, short src_y, 
    short drw_x, short drw_y,
    short src_w, short src_h, 
    short drw_w, short drw_h,
    RegionPtr clipBoxes
){
    OffscreenPrivPtr pPriv = (OffscreenPrivPtr)surface->devPrivate.ptr;
    ScrnInfoPtr pScrn = surface->pScrn;
    TDFXPtr pTDFX = TDFXPTR(pScrn);
    TDFXPortPrivPtr portPriv = pTDFX->overlayAdaptor->pPortPrivates[0].ptr;
    INT32 x1, y1, x2, y2;
    BoxRec dstBox;

    x1 = src_x;
    x2 = src_x + src_w;
    y1 = src_y;
    y2 = src_y + src_h;

    dstBox.x1 = drw_x;
    dstBox.x2 = drw_x + drw_w;
    dstBox.y1 = drw_y;
    dstBox.y2 = drw_y + drw_h;

    if(!TDFXClipVideo(&dstBox, &x1, &x2, &y1, &y2, clipBoxes, 
			surface->width, surface->height))
    {
	return Success;
    }

    dstBox.x1 -= pScrn->frameX0;
    dstBox.x2 -= pScrn->frameX0;
    dstBox.y1 -= pScrn->frameY0;
    dstBox.y2 -= pScrn->frameY0;

#if 0
    TDFXResetVideoOverlay(pScrn);
#endif

    TDFXDisplayVideoOverlay(pScrn, surface->id, surface->offsets[0], 
	     surface->width, surface->height, surface->pitches[0],
	     x1, y1, x2, &dstBox, src_w, src_h, drw_w, drw_h);

    (*pTDFX->AccelInfoRec->FillSolidRects)(pScrn, portPriv->colorKey,
					   GXcopy, ~0, 
					   REGION_NUM_RECTS(clipBoxes),
					   REGION_RECTS(clipBoxes));

    pPriv->isOn = TRUE;
    /* we've prempted the XvImage stream so set its free timer */
    if(portPriv->videoStatus & CLIENT_VIDEO_ON) {
	REGION_EMPTY(pScrn->pScreen, &portPriv->clip);   
	UpdateCurrentTime();
	portPriv->videoStatus = FREE_TIMER;
	portPriv->freeTime = currentTime.milliseconds + FREE_DELAY;
	pTDFX->VideoTimerCallback = TDFXVideoTimerCallback;
    }

    return Success;
}

static void 
TDFXInitOffscreenImages(ScreenPtr pScreen)
{
    XF86OffscreenImagePtr offscreenImages;

    /* need to free this someplace */
    if(!(offscreenImages = xalloc(sizeof(XF86OffscreenImageRec))))
	return;

    offscreenImages[0].image = &OverlayImages[0];
    offscreenImages[0].flags = VIDEO_OVERLAID_IMAGES | 
			       VIDEO_CLIP_TO_VIEWPORT;
    offscreenImages[0].alloc_surface = TDFXAllocateSurface;
    offscreenImages[0].free_surface = TDFXFreeSurface;
    offscreenImages[0].display = TDFXDisplaySurface;
    offscreenImages[0].stop = TDFXStopSurface;
    offscreenImages[0].setAttribute = TDFXSetSurfaceAttribute;
    offscreenImages[0].getAttribute = TDFXGetSurfaceAttribute;
    offscreenImages[0].max_width = 2048;
    offscreenImages[0].max_height = 2048;
    offscreenImages[0].num_attributes = 2;
    offscreenImages[0].attributes = OverlayAttributes;
    
    xf86XVRegisterOffscreenImages(pScreen, offscreenImages, 1);
}
