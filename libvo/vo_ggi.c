/* 
  vo_ggi.c - General Graphics Interface (GGI) Renderer for MPlayer

  (C) Alex Beregszaszi <alex@naxine.org>
  
  Uses libGGI - http://www.ggi-project.org/

  Many thanks to Atmosfear, he hacked this driver to working with Planar
  formats, and he fixed the RGB handling.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "config.h"
#include "video_out.h"
#include "video_out_internal.h"

#include "yuv2rgb.h"
#include "fastmemcpy.h"

#include <ggi/ggi.h>

#undef GGI_OST
#define GII_BUGGY_KEYCODES
#define GGI_OSD

LIBVO_EXTERN (ggi)

static vo_info_t vo_info = 
{
	"General Graphics Interface (GGI) output",
	"ggi",
	"Alex Beregszaszi <alex@naxine.org>",
	"under developement"
};

extern int verbose;

static char *ggi_output_name = NULL;
static ggi_visual_t ggi_vis;
static ggi_directbuffer *ggi_buffer;
static int ggi_format = 0;
static int ggi_bpp = 0;
static int ggi_bppmul = 0;
static uint32_t virt_width;
static uint32_t virt_height;
#ifdef GGI_OST
static ggi_pixel white;
static ggi_pixel black;
#endif

static int ggi_setmode(uint32_t d_width, uint32_t d_height, int d_depth)
{
    ggi_mode mode =
    {
	1,			/* frames */
	{ GGI_AUTO, GGI_AUTO },	/* visible */
	{ GGI_AUTO, GGI_AUTO },	/* virt */
	{ GGI_AUTO, GGI_AUTO },	/* size */
	GT_AUTO,		/* graphtype */
	{ GGI_AUTO, GGI_AUTO }	/* dots per pixel */
    };
    ggi_color pal[256];

    mode.visible.x = mode.virt.x = d_width;
    mode.visible.y = mode.virt.y = d_height;
    
    switch(d_depth)
    {
	case 1:
	    mode.graphtype = GT_1BIT;
	    break;
	case 2:
	    mode.graphtype = GT_2BIT;
	    break;
	case 4:
	    mode.graphtype = GT_4BIT;
	    break;
	case 8:
	    mode.graphtype = GT_8BIT;
	    break;
	case 15:
	    mode.graphtype = GT_15BIT;
	    break;
	case 16:
	    mode.graphtype = GT_16BIT;
	    break;
	case 24:
	    mode.graphtype = GT_24BIT;
	    break;
	case 32:
	    mode.graphtype = GT_32BIT;
	    break;
	default:
	    printf("ggi-setmode: unknown bit depth - using auto\n");
	    mode.graphtype = GT_AUTO;
    }
    
    ggiCheckMode(ggi_vis, &mode);
    
    if (ggiSetMode(ggi_vis, &mode) != 0)
    {
	printf("ggi-setmode: unable to set mode\n");
	ggiClose(ggi_vis);
	ggiExit();
	return(-1);
    }
    
    if (ggiGetMode(ggi_vis, &mode) != 0)
    {
	printf("ggi-setmode: unable to get mode\n");
	ggiClose(ggi_vis);
	ggiExit();
	return(-1);
    }
    
    virt_width = mode.virt.x;
    virt_height = mode.virt.y;
    vo_screenwidth = mode.visible.x;
    vo_screenheight = mode.visible.y;
    vo_depthonscreen = d_depth;
//    vo_depthonscreen = GT_DEPTH(mode.graphtype);
//    ggi_bpp = GT_SIZE(mode.graphtype);
    ggi_bpp = vo_depthonscreen;

#ifdef get_db_info
    {
	const ggi_directbuffer *db = ggiDBGetBuffer(ggi_vis, 0);
	
	if (db)
	{
	    vo_depthonscreen = db->buffer.plb.pixelformat->depth;
	    ggi_bpp = db->buffer.plb.pixelformat->size / 8;
	}
    }
#endif

    if (GT_SCHEME(mode.graphtype) == GT_PALETTE)
    {
	ggiSetColorfulPalette(ggi_vis);
	ggiGetPalette(ggi_vis, 0, 1 << ggi_bpp, pal);
    }

    if (verbose)
	printf("ggi-setmode: %dx%d (virt: %dx%d) screen depth: %d, bpp: %d\n",
	    vo_screenwidth, vo_screenheight, virt_width, virt_height,
	    vo_depthonscreen, ggi_bpp);

    ggi_bppmul = ggi_bpp/8;

    return(0);
}


static uint32_t init(uint32_t width, uint32_t height, uint32_t d_width,
    uint32_t d_height, uint32_t fullscreen, char *title, uint32_t format)
{
    vo_depthonscreen = 32;
    printf("ggi-init: This driver has got bugs, if you can, fix them.\n");

    if (ggiInit() != 0)
    {
	printf("ggi-init: unable to initialize GGI\n");
	return(-1);
    }
    
    if ((ggi_vis = ggiOpen(ggi_output_name)) == NULL)
    {
	printf("ggi-init: unable to open GGI for %s output\n",
	    (ggi_output_name == NULL) ? "default" : ggi_output_name);
	ggiExit();
	return(-1);
    }
    
    printf("ggi-init: using %s GGI output\n",
	(ggi_output_name == NULL) ? "default" : ggi_output_name);

    switch(format)
    {
	case IMGFMT_RGB8:
	    ggi_bpp = 8;
	    break;
	case IMGFMT_RGB15:
	    ggi_bpp = 15;
	    break;
	case IMGFMT_RGB16:
	    ggi_bpp = 16;
	    break;
	case IMGFMT_RGB24:
	    ggi_bpp = 24;
	    break;
	case IMGFMT_RGB32:
	    ggi_bpp = 32;
	    break;
	case IMGFMT_BGR8:
	    ggi_bpp = 8;
	    break;
	case IMGFMT_BGR15:
	    ggi_bpp = 15;
	    break;
	case IMGFMT_BGR16:
	    ggi_bpp = 16;
	    break;
	case IMGFMT_BGR24:
	    ggi_bpp = 24;
	    break;
	case IMGFMT_BGR32:
	    ggi_bpp = 32;
	    break;
	case IMGFMT_YV12: /* rgb, 24bit */
	    ggi_bpp = 16;
	    yuv2rgb_init(32/*vo_depthonscreen*/, MODE_RGB);
	    break;
	default:
	    printf("ggi-init: no suitable image format found (requested: %s)\n",
		vo_format_name(format));
	    return(-1);
    }
    ggi_format = format;

    if (ggi_setmode(d_width, d_height, vo_depthonscreen) != 0)
    {
	printf("ggi-init: setmode returned with error\n");
	return(-1);
    }    

    printf("ggi-init: input: %d bpp %s - screen depth: %d\n", ggi_bpp,
	vo_format_name(ggi_format), vo_depthonscreen);

    ggiSetFlags(ggi_vis, GGIFLAG_ASYNC);

    ggi_buffer = (ggi_directbuffer *)ggiDBGetBuffer(ggi_vis, 0);

    if (ggi_buffer == NULL)
    {
	printf("ggi-init: double buffering is not available\n");
	ggiClose(ggi_vis);
	ggiExit();
	return(-1);
    }
	
    if (!(ggi_buffer->type & GGI_DB_SIMPLE_PLB) ||
	(ggi_buffer->page_size != 0) ||
	(ggi_buffer->write == NULL) ||
	(ggi_buffer->noaccess != 0) ||
	(ggi_buffer->align != 0) ||
	(ggi_buffer->layout != blPixelLinearBuffer))
    {
	printf("ggi-init: incorrect video memory type\n");
	ggiClose(ggi_vis);
	ggiExit();
	return(-1);
    }

#ifdef GGI_OST
    /* just for fun */
    {
	ggi_color col;

	/* set black */
	col.r = col.g = col.b = 0x0000;
	black = ggiMapColor(ggi_vis, &col);

	/* set white */
	col.r = col.g = col.b = 0xffff;
	white = ggiMapColor(ggi_vis, &col);

	ggiSetGCForeground(ggi_vis, white);
	ggiSetGCBackground(ggi_vis, black);
    }
#endif

    return(0);
}

static const vo_info_t* get_info(void)
{
    return &vo_info;
}

static uint32_t draw_frame(uint8_t *src[])
{
    ggiResourceAcquire(ggi_buffer->resource, GGI_ACTYPE_WRITE);
    ggiSetDisplayFrame(ggi_vis, ggi_buffer->frame);
    ggiSetWriteFrame(ggi_vis, ggi_buffer->frame);
    
    memcpy(ggi_buffer->write, src[0], virt_width*virt_height*ggi_bppmul);

#ifdef GGI_OST
    ggiPuts(ggi_vis, virt_width/2, virt_height-10, "MPlayer GGI");
#endif

    ggiResourceRelease(ggi_buffer->resource);
    return(0);
}

#ifdef GGI_OSD
static void draw_alpha(int x0, int y0, int w, int h, unsigned char *src,
    unsigned char *srca, int stride)
{
    switch(ggi_format)
    {
	case IMGFMT_YV12:
	case IMGFMT_I420:
	case IMGFMT_IYUV:
	    switch (vo_depthonscreen)
	    {
		case 32:
        	    vo_draw_alpha_rgb32(w, h, src, srca, stride, 
			ggi_buffer->write+4*(virt_width*y0+x0), 4*virt_width);
		    break;
		case 24:
        	    vo_draw_alpha_rgb24(w, h, src, srca, stride, 
	    		ggi_buffer->write+3*(virt_width*y0+x0), 3*virt_width);
		    break;
		case 16:
        	    vo_draw_alpha_rgb16(w, h, src, srca, stride, 
			ggi_buffer->write+2*(virt_width*y0+x0), 2*virt_width);
		    break;
		case 15:
        	    vo_draw_alpha_rgb15(w, h, src, srca, stride, 
			ggi_buffer->write+2*(virt_width*y0+x0), 2*virt_width);
		    break;
	    }
//	    vo_draw_alpha_yv12(w, h, src, srca, stride,
//		ggi_buffer->write+(virt_width*y0+x0),
//		virt_width);
	    break;
	case IMGFMT_YUY2:
	case IMGFMT_YVYU:
	    vo_draw_alpha_yuy2(w, h, src, srca, stride,
		ggi_buffer->write+2*(virt_width*y0+x0),
		2*virt_width);
	    break;
	case IMGFMT_UYVY:
	    vo_draw_alpha_yuy2(w, h, src, srca, stride,
		ggi_buffer->write+2*(virt_width*y0+x0)+1,
		2*virt_width);
	    break;
	case IMGFMT_RGB15:
        case IMGFMT_BGR15:
            vo_draw_alpha_rgb15(w, h, src, srca, stride, 
		ggi_buffer->write+2*(virt_width*y0+x0), 2*virt_width);
            break;
        case IMGFMT_RGB16:
        case IMGFMT_BGR16:
            vo_draw_alpha_rgb16(w, h, src, srca, stride, 
		ggi_buffer->write+2*(virt_width*y0+x0), 2*virt_width);
            break;
        case IMGFMT_RGB24:
        case IMGFMT_BGR24:
            vo_draw_alpha_rgb24(w, h, src, srca, stride, 
		ggi_buffer->write+3*(virt_width*y0+x0), 3*virt_width);
            break;
        case IMGFMT_RGB32:
        case IMGFMT_BGR32:
            vo_draw_alpha_rgb32(w, h, src, srca, stride, 
		ggi_buffer->write+4*(virt_width*y0+x0), 4*virt_width);
	    break;
    }
}
#endif

static void flip_page(void)
{
    check_events();
#ifdef GGI_OSD
    vo_draw_text(virt_width, virt_height, draw_alpha);
#endif
    ggiFlush(ggi_vis);
}

static uint32_t draw_slice(uint8_t *src[], int stride[], int w, int h,
    int x, int y)
{
    yuv2rgb(((uint8_t *) ggi_buffer->write)+(virt_width*y+x)*ggi_bppmul,
	src[0], src[1], src[2], w, h, virt_width*ggi_bppmul, stride[0],
	stride[1]);

    return(0);
}

static uint32_t query_format(uint32_t format)
{
    switch(format)
    {
	case IMGFMT_YV12:
/*	case IMGFMT_I420:
	case IMGFMT_IYUV:
	case IMGFMT_YUY2:
	case IMGFMT_YVYU:
	case IMGFMT_UYVY:*/
	    return(1);
	case IMGFMT_RGB8:
	case IMGFMT_RGB15:
	case IMGFMT_RGB16:
        case IMGFMT_RGB24:
	case IMGFMT_RGB32:
	case IMGFMT_BGR8:
	case IMGFMT_BGR15:
	case IMGFMT_BGR16:
	case IMGFMT_BGR24:
	case IMGFMT_BGR32:
	    return(1);
    }
    return(0);
}

static void uninit(void)
{
    ggiResourceRelease(ggi_buffer->resource);
    ggiClose(ggi_vis);
    ggiExit();
}

#include "../linux/keycodes.h"
static void check_events(void)
{
    struct timeval tv = {0, 0};
    ggi_event event;
    ggi_event_mask mask;

    if ((mask = ggiEventPoll(ggi_vis, emAll, &tv)))
    if (ggiEventRead(ggi_vis, &event, emAll) != 0)
    {
//	printf("type: %4x, origin: %4x, sym: %4x, label: %4x, button=%4x\n",
//	    event.any.origin, event.any.type, event.key.sym, event.key.label, event.key.button);

	if ((event.any.type == evKeyPress) ||
	    (event.any.type == evKeyRepeat) ||
	    (event.any.type == evKeyRelease))
	{
#ifdef GII_BUGGY_KEYCODES
	    switch(event.key.button)
	    {
		case 0x37:
		    mplayer_put_key('*');
		    break;
		case 0x68:
		    mplayer_put_key('/');
		    break;
		case 0x4e:
		    mplayer_put_key('+');
		    break;
		case 0x4a:
		    mplayer_put_key('-');
		    break;
		case 0x18:
		    mplayer_put_key('o');
		    break;
		case 0x22:
		    mplayer_put_key('g');
		    break;
		case 0x15:
		    mplayer_put_key('z');
		    break;
		case 0x2d:
		    mplayer_put_key('x');
		    break;
		case 0x32:
		    mplayer_put_key('m');
		    break;
		case 0x20:
		    mplayer_put_key('d');
		    break;
		case 0x10:
		    mplayer_put_key('q');
		    break;
		case 0x39:
		    mplayer_put_key('p');
		    break;
		case 0x5a:
		    mplayer_put_key(KEY_UP);
		    break;
		case 0x60:
		    mplayer_put_key(KEY_DOWN);
		    break;
		case 0x5c:
		    mplayer_put_key(KEY_LEFT);
		    break;
		case 0x5e:
		    mplayer_put_key(KEY_RIGHT);
		    break;
		case 0x5b:
		    mplayer_put_key(KEY_PAGE_UP);
		    break;
		case 0x61:
		    mplayer_put_key(KEY_PAGE_DOWN);
		    break;
		default:
		    break;
	    }
#else
	    switch(event.key.button)
	    {
		case GIIK_PAsterisk: /* PStar */
		case GIIUC_Asterisk:
		    mplayer_put_key('*');
		    break;
		case GIIK_PSlash:
		case GIIUC_Slash:
		    mplayer_put_key('/');
		    break;
		case GIIK_PPlus:
		case GIIUC_Plus:
		    mplayer_put_key('+');
		    break;
		case GIIK_PMinus:
		case GIIUC_Minus:
		    mplayer_put_key('-');
		    break;
		case GIIUC_o:
		case GIIUC_O:
		    mplayer_put_key('o');
		    break;
		case GIIUC_g:
		case GIIUC_G:
		    mplayer_put_key('g');
		    break;
		case GIIUC_z:
		case GIIUC_Z:
		    mplayer_put_key('z');
		    break;
		case GIIUC_x:
		case GIIUC_X:
		    mplayer_put_key('x');
		    break;
		case GIIUC_m:
		case GIIUC_M:
		    mplayer_put_key('m');
		    break;
		case GIIUC_d:
		case GIIUC_D:
		    mplayer_put_key('d');
		    break;
		case GIIUC_q:
		case GIIUC_Q:
		    mplayer_put_key('q');
		    break;
		case GIIUC_Space:
		case GIIUC_p:
		case GIIUC_P:
		    mplayer_put_key('p');
		    break;
		case GIIK_Up:
		    mplayer_put_key(KEY_UP);
		    break;
		case GIIK_Down:
		    mplayer_put_key(KEY_DOWN);
		    break;
		case GIIK_Left:
		    mplayer_put_key(KEY_LEFT);
		    break;
		case GIIK_Right:
		    mplayer_put_key(KEY_RIGHT);
		    break;
		case GIIK_PageUp:
		    mplayer_put_key(KEY_PAGE_UP);
		    break;
		case GIIK_PageDown:
		    mplayer_put_key(KEY_PAGE_DOWN);
		    break;
		default:
		    break;
	    }
#endif
	}
    }
    return;
}
