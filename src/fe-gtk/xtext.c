/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 * =========================================================================
 *
 * xtext, the text widget used by X-Chat.
 * By Peter Zelezny <zed@xchat.org>.
 *
 */

#define XCHAT							/* using xchat */
#define TINT_VALUE 195				/* 195/255 of the brightness. */
#define MOTION_MONITOR				/* URL hilights. */
#define SMOOTH_SCROLL				/* line-by-line or pixel scroll? */
#define SCROLL_HACK					/* use XCopyArea scroll, or full redraw? */
#undef COLOR_HILIGHT				/* Color instead of underline? */
/* Italic is buggy because it assumes drawing an italic string will have
   identical extents to the normal font. This is only true some of the
   time, so we can't use this hack yet. */
#undef ITALIC							/* support Italic? */
#define GDK_MULTIHEAD_SAFE
#define USE_DB							/* double buffer */

#define MARGIN 2						/* dont touch. */
#define REFRESH_TIMEOUT 20
#define WORDWRAP_LIMIT 24

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkselection.h>
#include <gtk/gtkclipboard.h>
#include <gtk/gtkversion.h>
#include <gtk/gtkwindow.h>
#include "webcontroller.h"

#ifdef XCHAT
#include "../../config.h"			/* can define USE_XLIB here */
#else
#define USE_XLIB
#endif

#ifdef USE_XLIB
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

#ifdef USE_MMX
#include "mmx_cmod.h"
#endif

#include "xtext.h"

#define charlen(str) g_utf8_skip[*(guchar *)(str)]

#ifdef WIN32
#include <windows.h>
#include <gdk/gdkwin32.h>
#endif

/* is delimiter */
#define is_del(c) \
	(c == ' ' || c == '\n' || c == ')' || c == '(' || \
	 c == '>' || c == '<' || c == ATTR_RESET || c == ATTR_BOLD || c == 0)

#ifdef SCROLL_HACK
/* force scrolling off */
#define dontscroll(buf) (buf)->last_pixel_pos = 0x7fffffff
#else
#define dontscroll(buf)
#endif

struct textentry
{
	struct textentry *next;
	struct textentry *prev;
	unsigned char *str;
	time_t stamp;
	gint16 str_width;
	gint16 str_len;
	gint16 mark_start;
	gint16 mark_end;
	gint16 indent;
	gint16 left_len;
	gint16 lines_taken;
#define RECORD_WRAPS 4
	guint16 wrap_offset[RECORD_WRAPS];
	guchar mb;		/* boolean: is multibyte? */
	guchar tag;
	guchar pad1;
	guchar pad2;	/* 32-bit align : 44 bytes total */
};

enum
{
	WORD_CLICK,
	LAST_SIGNAL
};

/* values for selection info */
enum
{
	TARGET_UTF8_STRING,
	TARGET_STRING,
	TARGET_TEXT,
	TARGET_COMPOUND_TEXT
};


#ifdef XCHAT
char *nocasestrstr (const char *text, const char *tofind);	/* util.c */
int xtext_get_stamp_str (time_t, char **);
#endif
static void gtk_xtext_render_page (GtkXText * xtext);
static void gtk_xtext_adjustment_changed (GtkAdjustment * adj,GtkXText * xtext);



/* some utility functions first */

#ifndef XCHAT	/* xchat has this in util.c */

static char *
nocasestrstr (const char *s, const char *tofind)
{
   register const size_t len = strlen (tofind);

   if (len == 0)
     return (char *)s;
   while (toupper(*s) != toupper(*tofind) || strncasecmp (s, tofind, len))
     if (*s++ == '\0')
       return (char *)NULL;
   return (char *)s;
}

#endif

#ifdef WIN32

static void
win32_draw_bg (GtkXText *xtext, int x, int y, int width, int height)
{
	HDC hdc;
	HWND hwnd;
	HRGN rgn;

	if (xtext->shaded)
	{
		/* xtext->pixmap is really a GdkImage, created in win32_tint() */
		gdk_draw_image (xtext->draw_buf, xtext->bgc, (GdkImage*)xtext->pixmap,
							 x, y, x, y, width, height);
	} else
	{
		hwnd = GDK_WINDOW_HWND (xtext->draw_buf);
		hdc = GetDC (hwnd);

		rgn = CreateRectRgn (x, y, x + width, y + height);
		SelectClipRgn (hdc, rgn);

		PaintDesktop (hdc);

		ReleaseDC (hwnd, hdc);
		DeleteObject (rgn);
	}
}

static void
xtext_draw_bg (GtkXText *xtext, int x, int y, int width, int height)
{
	if (xtext->transparent)
		win32_draw_bg (xtext, x, y, width, height);
	else
		gdk_draw_rectangle (xtext->draw_buf, xtext->bgc, 1, x, y, width, height);
}

#else

#define xtext_draw_bg(xt,x,y,w,h) gdk_draw_rectangle(xt->draw_buf, xt->bgc, \
																	  1,x,y,w,h);

#endif

/* ========================================= */
/* ========== XFT 1 and 2 BACKEND ========== */
/* ========================================= */

#ifdef USE_XFT

static void
backend_font_close (GtkXText *xtext)
{
	XftFontClose (GDK_WINDOW_XDISPLAY (xtext->draw_buf), xtext->font);
#ifdef ITALIC
	XftFontClose (GDK_WINDOW_XDISPLAY (xtext->draw_buf), xtext->ifont);
#endif
}

static void
backend_init (GtkXText *xtext)
{
	if (xtext->xftdraw == NULL)
	{
		xtext->xftdraw = XftDrawCreate (
			GDK_WINDOW_XDISPLAY (xtext->draw_buf),
			GDK_WINDOW_XWINDOW (xtext->draw_buf),
			GDK_VISUAL_XVISUAL (gdk_drawable_get_visual (xtext->draw_buf)),
			GDK_COLORMAP_XCOLORMAP (gdk_drawable_get_colormap (xtext->draw_buf)));
		XftDrawSetSubwindowMode (xtext->xftdraw, IncludeInferiors);
	}
}

static void
backend_deinit (GtkXText *xtext)
{
	if (xtext->xftdraw)
	{
		XftDrawDestroy (xtext->xftdraw);
		xtext->xftdraw = NULL;
	}
}

static XftFont *
backend_font_open_real (Display *xdisplay, char *name, gboolean italics)
{
	XftFont *font = NULL;
	PangoFontDescription *fontd;
	int weight, slant, screen = DefaultScreen (xdisplay);

	fontd = pango_font_description_from_string (name);

	if (pango_font_description_get_size (fontd) != 0)
	{
		weight = pango_font_description_get_weight (fontd);
		/* from pangoft2-fontmap.c */
		if (weight < (PANGO_WEIGHT_NORMAL + PANGO_WEIGHT_LIGHT) / 2)
			weight = XFT_WEIGHT_LIGHT;
		else if (weight < (PANGO_WEIGHT_NORMAL + 600) / 2)
			weight = XFT_WEIGHT_MEDIUM;
		else if (weight < (600 + PANGO_WEIGHT_BOLD) / 2)
			weight = XFT_WEIGHT_DEMIBOLD;
		else if (weight < (PANGO_WEIGHT_BOLD + PANGO_WEIGHT_ULTRABOLD) / 2)
			weight = XFT_WEIGHT_BOLD;
		else
			weight = XFT_WEIGHT_BLACK;

		slant = pango_font_description_get_style (fontd);
		if (slant == PANGO_STYLE_ITALIC)
			slant = XFT_SLANT_ITALIC;
		else if (slant == PANGO_STYLE_OBLIQUE)
			slant = XFT_SLANT_OBLIQUE;
		else
			slant = XFT_SLANT_ROMAN;

		font = XftFontOpen (xdisplay, screen,
						XFT_FAMILY, XftTypeString, pango_font_description_get_family (fontd),
						XFT_CORE, XftTypeBool, False,
						XFT_SIZE, XftTypeDouble, (double)pango_font_description_get_size (fontd)/PANGO_SCALE,
						XFT_WEIGHT, XftTypeInteger, weight,
						XFT_SLANT, XftTypeInteger, italics ? XFT_SLANT_ITALIC : slant,
						NULL);
	}
	pango_font_description_free (fontd);

	if (font == NULL)
	{
		font = XftFontOpenName (xdisplay, screen, name);
		if (font == NULL)
			font = XftFontOpenName (xdisplay, screen, "sans-11");
	}

	return font;
}

static void
backend_font_open (GtkXText *xtext, char *name)
{
	Display *dis = GDK_WINDOW_XDISPLAY (xtext->draw_buf);

	xtext->font = backend_font_open_real (dis, name, FALSE);
#ifdef ITALIC
	xtext->ifont = backend_font_open_real (dis, name, TRUE);
#endif
}

inline static int
backend_get_char_width (GtkXText *xtext, unsigned char *str, int *mbl_ret)
{
	XGlyphInfo ext;

	if (*str < 128)
	{
		*mbl_ret = 1;
		return xtext->fontwidth[*str];
	}

	*mbl_ret = charlen (str);
	XftTextExtentsUtf8 (GDK_WINDOW_XDISPLAY (xtext->draw_buf), xtext->font, str, *mbl_ret, &ext);

	return ext.xOff;
}

static int
backend_get_text_width (GtkXText *xtext, guchar *str, int len, int is_mb)
{
	XGlyphInfo ext;

	if (!is_mb)
		return gtk_xtext_text_width_8bit (xtext, str, len);

	XftTextExtentsUtf8 (GDK_WINDOW_XDISPLAY (xtext->draw_buf), xtext->font, str, len, &ext);
	return ext.xOff;
}

static void
backend_draw_text (GtkXText *xtext, int dofill, GdkGC *gc, int x, int y,
						 char *str, int len, int str_width, int is_mb)
{
	/*Display *xdisplay = GDK_WINDOW_XDISPLAY (xtext->draw_buf);*/
	void (*draw_func) (XftDraw *, XftColor *, XftFont *, int, int, XftChar8 *, int) = (void *)XftDrawString8;
	XftFont *font;

	/* if all ascii, use String8 to avoid the conversion penalty */
	if (is_mb)
		draw_func = (void *)XftDrawStringUtf8;

	if (dofill)
	{
/*		register GC xgc = GDK_GC_XGC (gc);
		XSetForeground (xdisplay, xgc, xtext->xft_bg->pixel);
		XFillRectangle (xdisplay, GDK_WINDOW_XWINDOW (xtext->draw_buf), xgc, x,
							 y - xtext->font->ascent, str_width, xtext->fontsize);*/
		XftDrawRect (xtext->xftdraw, xtext->xft_bg, x,
						 y - xtext->font->ascent, str_width, xtext->fontsize);
	}

	font = xtext->font;
#ifdef ITALIC
	if (xtext->italics)
		font = xtext->ifont;
#endif

	draw_func (xtext->xftdraw, xtext->xft_fg, font, x, y, str, len);

	if (xtext->overdraw)
		draw_func (xtext->xftdraw, xtext->xft_fg, font, x, y, str, len);

	if (xtext->bold)
		draw_func (xtext->xftdraw, xtext->xft_fg, font, x + 1, y, str, len);
}

/*static void
backend_set_clip (GtkXText *xtext, GdkRectangle *area)
{
	gdk_gc_set_clip_rectangle (xtext->fgc, area);
	gdk_gc_set_clip_rectangle (xtext->bgc, area);
}

static void
backend_clear_clip (GtkXText *xtext)
{
	gdk_gc_set_clip_rectangle (xtext->fgc, NULL);
	gdk_gc_set_clip_rectangle (xtext->bgc, NULL);
}*/

/*static void
backend_set_clip (GtkXText *xtext, GdkRectangle *area)
{
	Region reg;
	XRectangle rect;

	rect.x = area->x;
	rect.y = area->y;
	rect.width = area->width;
	rect.height = area->height;

	reg = XCreateRegion ();
	XUnionRectWithRegion (&rect, reg, reg);
	XftDrawSetClip (xtext->xftdraw, reg);
	XDestroyRegion (reg);

	gdk_gc_set_clip_rectangle (xtext->fgc, area);
}

static void
backend_clear_clip (GtkXText *xtext)
{
	XftDrawSetClip (xtext->xftdraw, NULL);
	gdk_gc_set_clip_rectangle (xtext->fgc, NULL);
}
*/
#else	/* !USE_XFT */

/* ======================================= */
/* ============ PANGO BACKEND ============ */
/* ======================================= */

inline static int
backend_get_char_width (GtkXText *xtext, unsigned char *str, int *mbl_ret)
{
	int width;

	if (*str < 128)
	{
		*mbl_ret = 1;
		return xtext->fontwidth[*str];
	}

	*mbl_ret = charlen (str);
	pango_layout_set_text (xtext->layout, (const char *)str, *mbl_ret);
	pango_layout_get_pixel_size (xtext->layout, &width, NULL);

	return width;
}

#endif /* !USE_PANGO */


static void
gtk_xtext_init (GtkXText * xtext)
{
	xtext->pixmap = NULL;
	xtext->io_tag = 0;
	xtext->add_io_tag = 0;
	xtext->scroll_tag = 0;
	xtext->max_lines = 0;
	xtext->col_back = XTEXT_BG;
	xtext->col_fore = XTEXT_FG;
	xtext->nc = 0;
	xtext->pixel_offset = 0;
	xtext->bold = FALSE;
	xtext->underline = FALSE;
	xtext->italics = FALSE;
	xtext->hidden = FALSE;
	xtext->font = NULL;
#ifdef USE_XFT
	xtext->xftdraw = NULL;
#else
	xtext->layout = NULL;
#endif
	xtext->jump_out_offset = 0;
	xtext->jump_in_offset = 0;
	xtext->ts_x = 0;
	xtext->ts_y = 0;
	xtext->clip_x = 0;
	xtext->clip_x2 = 1000000;
	xtext->clip_y = 0;
	xtext->clip_y2 = 1000000;
	xtext->error_function = NULL;
	xtext->urlcheck_function = NULL;
	xtext->color_paste = FALSE;
	xtext->skip_border_fills = FALSE;
	xtext->skip_stamp = FALSE;
	xtext->render_hilights_only = FALSE;
	xtext->un_hilight = FALSE;
	xtext->recycle = FALSE;
	xtext->dont_render = FALSE;
	xtext->dont_render2 = FALSE;
	xtext->overdraw = FALSE;
	xtext->tint_red = xtext->tint_green = xtext->tint_blue = TINT_VALUE;

	xtext->adj = (GtkAdjustment *) gtk_adjustment_new (0, 0, 1, 1, 1, 1);
	g_object_ref (G_OBJECT (xtext->adj));
	g_object_ref_sink (G_OBJECT (xtext->adj));
	g_object_unref (G_OBJECT (xtext->adj));

	xtext->vc_signal_tag = g_signal_connect (G_OBJECT (xtext->adj),
				"value_changed", G_CALLBACK (gtk_xtext_adjustment_changed), xtext);
	{
		static const GtkTargetEntry targets[] = {
			{ "UTF8_STRING", 0, TARGET_UTF8_STRING },
			{ "STRING", 0, TARGET_STRING },
			{ "TEXT",   0, TARGET_TEXT },
			{ "COMPOUND_TEXT", 0, TARGET_COMPOUND_TEXT }
		};
		static const gint n_targets = sizeof (targets) / sizeof (targets[0]);

		gtk_selection_add_targets (GTK_WIDGET (xtext), GDK_SELECTION_PRIMARY,
											targets, n_targets);
	}

	if (getenv ("XCHAT_OVERDRAW"))
		xtext->overdraw = TRUE;
}



static gint
gtk_xtext_adjustment_timeout (GtkXText * xtext)
{
	gtk_xtext_render_page (xtext);
	xtext->io_tag = 0;
	return 0;
}

static void
gtk_xtext_adjustment_changed (GtkAdjustment * adj, GtkXText * xtext)
{
#ifdef SMOOTH_SCROLL
	if (xtext->buffer->old_value != xtext->adj->value)
#else
	if ((int) xtext->buffer->old_value != (int) xtext->adj->value)
#endif
	{
		if (xtext->adj->value >= xtext->adj->upper - xtext->adj->page_size)
			xtext->buffer->scrollbar_down = TRUE;
		else
			xtext->buffer->scrollbar_down = FALSE;

		if (xtext->adj->value + 1 == xtext->buffer->old_value ||
			 xtext->adj->value - 1 == xtext->buffer->old_value)	/* clicked an arrow? */
		{
			if (xtext->io_tag)
			{
				g_source_remove (xtext->io_tag);
				xtext->io_tag = 0;
			}
			gtk_xtext_render_page (xtext);
		} else
		{
			if (!xtext->io_tag)
				xtext->io_tag = g_timeout_add (REFRESH_TIMEOUT,
															(GSourceFunc)
															gtk_xtext_adjustment_timeout,
															xtext);
		}
	}
	xtext->buffer->old_value = adj->value;
}

/*
	XXX: metto la webview al posto della fuffa sua
*/

struct User* user_tmp;

GtkWidget *
gtk_xtext_new (GdkColor palette[], int separator)
{
    create_wview();
	return get_wview_container();
}



#ifdef USE_SHM
static int
have_shm_pixmaps(Display *dpy)
{
	int major, minor;
	static int checked = 0;
	static int have = FALSE;

	if (!checked)
	{
		XShmQueryVersion (dpy, &major, &minor, &have);
		checked = 1;
	}

	return have;
}
#endif


static void
gtk_xtext_class_init (GtkXTextClass * class)
{
}

GType
gtk_xtext_get_type (void)
{
	static GType xtext_type = 0;

	if (!xtext_type)
	{
		static const GTypeInfo xtext_info =
		{
			sizeof (GtkXTextClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) gtk_xtext_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (GtkXText),
			0,		/* n_preallocs */
			(GInstanceInitFunc) gtk_xtext_init,
		};

		xtext_type = g_type_register_static (GTK_TYPE_WIDGET, "GtkXText",
														 &xtext_info, 0);
	}

	return xtext_type;
}

/* Fast shading routine. Based on code by Willem Monsuwe <willem@stack.nl> */

#define SHADE_IMAGE(bytes, type, rmask, gmask, bmask) \
	unsigned char *ptr; \
	int x, y; \
	int bgr = (256 - rm) * (bg & rmask); \
	int bgg = (256 - gm) * (bg & gmask); \
	int bgb = (256 - bm) * (bg & bmask); \
	ptr = (unsigned char *) data + (w * bytes); \
	for (y = h; --y >= 0;) \
	{ \
		for (x = -w; x < 0; x++) \
		{ \
			int r, g, b; \
			b = ((type *) ptr)[x]; \
			r = rm * (b & rmask) + bgr; \
			g = gm * (b & gmask) + bgg; \
			b = bm * (b & bmask) + bgb; \
			((type *) ptr)[x] = ((r >> 8) & rmask) \
										| ((g >> 8) & gmask) \
										| ((b >> 8) & bmask); \
		} \
		ptr += bpl; \
    }



#ifdef USE_SHM

static XImage *
get_shm_image (Display *xdisplay, XShmSegmentInfo *shminfo, int x, int y,
					int w, int h, int depth, Pixmap pix)
{
	XImage *ximg;

	shminfo->shmid = -1;
	shminfo->shmaddr = (char*) -1;
	ximg = XShmCreateImage (xdisplay, 0, depth, ZPixmap, 0, shminfo, w, h);
	if (!ximg)
		return NULL;

	shminfo->shmid = shmget (IPC_PRIVATE, ximg->bytes_per_line * ximg->height,
									 IPC_CREAT|0600);
	if (shminfo->shmid == -1)
	{
		XDestroyImage (ximg);
		return NULL;
	}

	shminfo->readOnly = False;
	ximg->data = shminfo->shmaddr = (char *)shmat (shminfo->shmid, 0, 0);
	if (shminfo->shmaddr == ((char *)-1))
	{
		shmctl (shminfo->shmid, IPC_RMID, 0);
		XDestroyImage (ximg);
		return NULL;
	}

	XShmAttach (xdisplay, shminfo);
	XSync (xdisplay, False);
	shmctl (shminfo->shmid, IPC_RMID, 0);
	XShmGetImage (xdisplay, pix, ximg, x, y, AllPlanes);

	return ximg;
}

static XImage *
get_image (GtkXText *xtext, Display *xdisplay, XShmSegmentInfo *shminfo,
			  int x, int y, int w, int h, int depth, Pixmap pix)
{
	XImage *ximg;

	xtext->shm = 1;
	ximg = get_shm_image (xdisplay, shminfo, x, y, w, h, depth, pix);
	if (!ximg)
	{
		xtext->shm = 0;
		ximg = XGetImage (xdisplay, pix, x, y, w, h, -1, ZPixmap);
	}

	return ximg;
}

#endif

#ifdef WIN32

static GdkPixmap *
win32_tint (GtkXText *xtext, GdkImage *img, int width, int height)
{
	guchar *pixelp;
	int x, y;
	GdkPixmap *pix;
	GdkVisual *visual = gdk_drawable_get_visual (GTK_WIDGET (xtext)->window);
	guint32 pixel;
	int r, g, b;

	if (img->depth <= 14)
	{
		/* slow generic routine */
		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				if (img->depth == 1)
				{
					pixel = (((guchar *) img->mem)[y * img->bpl + (x >> 3)] & (1 << (7 - (x & 0x7)))) != 0;
					goto here;
				}

				if (img->depth == 4)
				{
					pixelp = (guchar *) img->mem + y * img->bpl + (x >> 1);
					if (x&1)
					{
						pixel = (*pixelp) & 0x0F;
						goto here;
					}

					pixel = (*pixelp) >> 4;
					goto here;
				}

				pixelp = (guchar *) img->mem + y * img->bpl + x * img->bpp;

				switch (img->bpp)
				{
				case 1:
					pixel = *pixelp; break;

				/* Windows is always LSB, no need to check img->byte_order. */
				case 2:
					pixel = pixelp[0] | (pixelp[1] << 8); break;

				case 3:
					pixel = pixelp[0] | (pixelp[1] << 8) | (pixelp[2] << 16); break;

				case 4:
					pixel = pixelp[0] | (pixelp[1] << 8) | (pixelp[2] << 16); break;
				}

here:
				r = (pixel & visual->red_mask) >> visual->red_shift;
				g = (pixel & visual->green_mask) >> visual->green_shift;
				b = (pixel & visual->blue_mask) >> visual->blue_shift;

				/* actual tinting is only these 3 lines */
				pixel = ((r * xtext->tint_red) >> 8) << visual->red_shift |
							((g * xtext->tint_green) >> 8) << visual->green_shift |
							((b * xtext->tint_blue) >> 8) << visual->blue_shift;

				if (img->depth == 1)
					if (pixel & 1)
						((guchar *) img->mem)[y * img->bpl + (x >> 3)] |= (1 << (7 - (x & 0x7)));
					else
						((guchar *) img->mem)[y * img->bpl + (x >> 3)] &= ~(1 << (7 - (x & 0x7)));
				else if (img->depth == 4)
				{
					pixelp = (guchar *) img->mem + y * img->bpl + (x >> 1);

					if (x&1)
					{
						*pixelp &= 0xF0;
						*pixelp |= (pixel & 0x0F);
					} else
					{
						*pixelp &= 0x0F;
						*pixelp |= (pixel << 4);
					}
				} else
				{
					pixelp = (guchar *) img->mem + y * img->bpl + x * img->bpp;

					/* Windows is always LSB, no need to check img->byte_order. */
					switch (img->bpp)
					{
					case 4:
						pixelp[3] = 0;
					case 3:
						pixelp[2] = ((pixel >> 16) & 0xFF);
					case 2:
						pixelp[1] = ((pixel >> 8) & 0xFF);
					case 1:
						pixelp[0] = (pixel & 0xFF);
					}
				}
			}
		}
	} else
	{
		shade_image (visual, img->mem, img->bpl, img->bpp, width, height,
						 xtext->tint_red, xtext->tint_green, xtext->tint_blue,
						 xtext->palette[XTEXT_BG], visual->depth);
	}

	/* no need to dump it to a Pixmap, it's one and the same on win32 */
	pix = (GdkPixmap *)img;

	return pix;
}

#endif /* !WIN32 */


/* render a whole page/window, starting from 'startline' */

static void
gtk_xtext_render_page (GtkXText * xtext)
{
}


/* append a textentry to our linked list */


/* the main two public functions */

void gtk_xtext_append_indent (xtext_buffer *buf, unsigned char *left_text, int left_len, unsigned char *right_text, int right_len, time_t stamp){}
void gtk_xtext_append (xtext_buffer *buf, unsigned char *text, int len){}
int gtk_xtext_set_font (GtkXText *xtext, char *name){return 1;}
void gtk_xtext_set_background (GtkXText * xtext, GdkPixmap * pixmap, gboolean trans){}
void gtk_xtext_set_palette (GtkXText * xtext, GdkColor palette[]){}
void gtk_xtext_clear (xtext_buffer *buf, int lines){}
void gtk_xtext_save (GtkXText * xtext, int fh){}
void gtk_xtext_refresh (GtkXText * xtext, int do_trans){}
int gtk_xtext_lastlog (xtext_buffer *out, xtext_buffer *search_area, int (*cmp_func) (char *, void *userdata), void *userdata){return 0;}
textentry *gtk_xtext_search (GtkXText * xtext, const gchar *text, textentry *start, gboolean case_match, gboolean backward){return NULL;}
void gtk_xtext_reset_marker_pos (GtkXText *xtext){}
void gtk_xtext_check_marker_visibility(GtkXText *xtext){}
//typedef void (*GtkXTextForeach) (GtkXText *xtext, unsigned char *text, void *data);
void gtk_xtext_foreach (xtext_buffer *buf, GtkXTextForeach func, void *data){}

void gtk_xtext_set_error_function (GtkXText *xtext, void (*error_function) (int)){}
void gtk_xtext_set_indent (GtkXText *xtext, gboolean indent){}
void gtk_xtext_set_max_indent (GtkXText *xtext, int max_auto_indent){}
void gtk_xtext_set_max_lines (GtkXText *xtext, int max_lines){}
void gtk_xtext_set_show_marker (GtkXText *xtext, gboolean show_marker){}
void gtk_xtext_set_show_separator (GtkXText *xtext, gboolean show_separator){}
void gtk_xtext_set_thin_separator (GtkXText *xtext, gboolean thin_separator){}
void gtk_xtext_set_time_stamp (xtext_buffer *buf, gboolean timestamp){}
void gtk_xtext_set_tint (GtkXText *xtext, int tint_red, int tint_green, int tint_blue){}
void gtk_xtext_set_urlcheck_function (GtkXText *xtext, int (*urlcheck_function) (GtkWidget *, char *, int)){}
void gtk_xtext_set_wordwrap (GtkXText *xtext, gboolean word_wrap){};
void gtk_xtext_buffer_free (xtext_buffer *buf){}
void gtk_xtext_buffer_show (GtkXText *xtext, xtext_buffer *buf, int render){}
xtext_buffer * gtk_xtext_buffer_new (GtkXText *xtext){return NULL;}
