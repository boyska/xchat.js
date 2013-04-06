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

static GtkWidgetClass *parent_class = NULL;

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

static guint xtext_signals[LAST_SIGNAL];

#ifdef XCHAT
char *nocasestrstr (const char *text, const char *tofind);	/* util.c */
int xtext_get_stamp_str (time_t, char **);
#endif
static void gtk_xtext_render_page (GtkXText * xtext);
static void gtk_xtext_calc_lines (xtext_buffer *buf, int);
#if defined(USE_XLIB) || defined(WIN32)
static void gtk_xtext_load_trans (GtkXText * xtext);
static void gtk_xtext_free_trans (GtkXText * xtext);
#endif
static char *gtk_xtext_selection_get_text (GtkXText *xtext, int *len_ret);
static textentry *gtk_xtext_nth (GtkXText *xtext, int line, int *subline);
static void gtk_xtext_adjustment_changed (GtkAdjustment * adj,
														GtkXText * xtext);
static int gtk_xtext_render_ents (GtkXText * xtext, textentry *, textentry *);
static void gtk_xtext_recalc_widths (xtext_buffer *buf, int);
static void gtk_xtext_fix_indent (xtext_buffer *buf);
static int gtk_xtext_find_subline (GtkXText *xtext, textentry *ent, int line);
static unsigned char *
gtk_xtext_strip_color (unsigned char *text, int len, unsigned char *outbuf,
							  int *newlen, int *mb_ret, int strip_hidden);

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

/* gives width of a 8bit string - with no mIRC codes in it */

static int
gtk_xtext_text_width_8bit (GtkXText *xtext, unsigned char *str, int len)
{
	int width = 0;

	while (len)
	{
		width += xtext->fontwidth[*str];
		str++;
		len--;
	}

	return width;
}

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

static void
backend_font_close (GtkXText *xtext)
{
	pango_font_description_free (xtext->font->font);
#ifdef ITALIC
	pango_font_description_free (xtext->font->ifont);
#endif
}

static void
backend_init (GtkXText *xtext)
{
	if (xtext->layout == NULL)
	{
		xtext->layout = gtk_widget_create_pango_layout (GTK_WIDGET (xtext), 0);
		if (xtext->font)
			pango_layout_set_font_description (xtext->layout, xtext->font->font);
	}
}

static void
backend_deinit (GtkXText *xtext)
{
	if (xtext->layout)
	{
		g_object_unref (xtext->layout);
		xtext->layout = NULL;
	}
}

static PangoFontDescription *
backend_font_open_real (char *name)
{
	PangoFontDescription *font;

	font = pango_font_description_from_string (name);
	if (font && pango_font_description_get_size (font) == 0)
	{
		pango_font_description_free (font);
		font = pango_font_description_from_string ("sans 11");
	}
	if (!font)
		font = pango_font_description_from_string ("sans 11");

	return font;
}

static void
backend_font_open (GtkXText *xtext, char *name)
{
	PangoLanguage *lang;
	PangoContext *context;
	PangoFontMetrics *metrics;

	xtext->font = &xtext->pango_font;
	xtext->font->font = backend_font_open_real (name);
	if (!xtext->font->font)
	{
		xtext->font = NULL;
		return;
	}
#ifdef ITALIC
	xtext->font->ifont = backend_font_open_real (name);
	pango_font_description_set_style (xtext->font->ifont, PANGO_STYLE_ITALIC);
#endif

	backend_init (xtext);
	pango_layout_set_font_description (xtext->layout, xtext->font->font);

	/* vte does it this way */
	context = gtk_widget_get_pango_context (GTK_WIDGET (xtext));
	lang = pango_context_get_language (context);
	metrics = pango_context_get_metrics (context, xtext->font->font, lang);
	xtext->font->ascent = pango_font_metrics_get_ascent (metrics) / PANGO_SCALE;
	xtext->font->descent = pango_font_metrics_get_descent (metrics) / PANGO_SCALE;
	pango_font_metrics_unref (metrics);
}

static int
backend_get_text_width (GtkXText *xtext, guchar *str, int len, int is_mb)
{
	int width;

	if (!is_mb)
		return gtk_xtext_text_width_8bit (xtext, str, len);

	if (*str == 0)
		return 0;

	pango_layout_set_text (xtext->layout, str, len);
	pango_layout_get_pixel_size (xtext->layout, &width, NULL);

	return width;
}

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
	pango_layout_set_text (xtext->layout, str, *mbl_ret);
	pango_layout_get_pixel_size (xtext->layout, &width, NULL);

	return width;
}

/* simplified version of gdk_draw_layout_line_with_colors() */

static void
xtext_draw_layout_line (GdkDrawable      *drawable,
								GdkGC            *gc,
								gint              x,
								gint              y,
								PangoLayoutLine  *line)
{
	GSList *tmp_list = line->runs;
	PangoRectangle logical_rect;
	gint x_off = 0;

	while (tmp_list)
	{
		PangoLayoutRun *run = tmp_list->data;

		pango_glyph_string_extents (run->glyphs, run->item->analysis.font,
											 NULL, &logical_rect);

		gdk_draw_glyphs (drawable, gc, run->item->analysis.font,
							  x + x_off / PANGO_SCALE, y, run->glyphs);

		x_off += logical_rect.width;
		tmp_list = tmp_list->next;
	}
}

static void
backend_draw_text (GtkXText *xtext, int dofill, GdkGC *gc, int x, int y,
						 char *str, int len, int str_width, int is_mb)
{
	GdkGCValues val;
	GdkColor col;
	PangoLayoutLine *line;

#ifdef ITALIC
	if (xtext->italics)
		pango_layout_set_font_description (xtext->layout, xtext->font->ifont);
#endif

	pango_layout_set_text (xtext->layout, str, len);

	if (dofill)
	{
#ifdef WIN32
		if (xtext->transparent && !xtext->backcolor)
			win32_draw_bg (xtext, x, y - xtext->font->ascent, str_width,
								xtext->fontsize);
		else
#endif
		{
			gdk_gc_get_values (gc, &val);
			col.pixel = val.background.pixel;
			gdk_gc_set_foreground (gc, &col);
			gdk_draw_rectangle (xtext->draw_buf, gc, 1, x, y -
									  xtext->font->ascent, str_width, xtext->fontsize);
			col.pixel = val.foreground.pixel;
			gdk_gc_set_foreground (gc, &col);
		}
	}

	line = pango_layout_get_lines (xtext->layout)->data;

	xtext_draw_layout_line (xtext->draw_buf, gc, x, y, line);

	if (xtext->overdraw)
		xtext_draw_layout_line (xtext->draw_buf, gc, x, y, line);

	if (xtext->bold)
		xtext_draw_layout_line (xtext->draw_buf, gc, x + 1, y, line);

#ifdef ITALIC
	if (xtext->italics)
		pango_layout_set_font_description (xtext->layout, xtext->font->font);
#endif
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

#endif /* !USE_PANGO */

static void
xtext_set_fg (GtkXText *xtext, GdkGC *gc, int index)
{
	GdkColor col;

	col.pixel = xtext->palette[index];
	gdk_gc_set_foreground (gc, &col);

#ifdef USE_XFT
	if (gc == xtext->fgc)
		xtext->xft_fg = &xtext->color[index];
	else
		xtext->xft_bg = &xtext->color[index];
#endif
}

#ifdef USE_XFT

#define xtext_set_bg(xt,gc,index) xt->xft_bg = &xt->color[index]

#else

static void
xtext_set_bg (GtkXText *xtext, GdkGC *gc, int index)
{
	GdkColor col;

	col.pixel = xtext->palette[index];
	gdk_gc_set_background (gc, &col);
}

#endif

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

static void
gtk_xtext_adjustment_set (xtext_buffer *buf, int fire_signal)
{
	GtkAdjustment *adj = buf->xtext->adj;

	if (buf->xtext->buffer == buf)
	{
		adj->lower = 0;
		adj->upper = buf->num_lines;

		if (adj->upper == 0)
			adj->upper = 1;

		adj->page_size =
			(GTK_WIDGET (buf->xtext)->allocation.height -
			 buf->xtext->font->descent) / buf->xtext->fontsize;
		adj->page_increment = adj->page_size;

		if (adj->value > adj->upper - adj->page_size)
			adj->value = adj->upper - adj->page_size;

		if (adj->value < 0)
			adj->value = 0;

		if (fire_signal)
			gtk_adjustment_changed (adj);
	}
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

static void
gtk_xtext_destroy (GtkObject * object)
{
}

static void
gtk_xtext_unrealize (GtkWidget * widget)
{
}

static void
gtk_xtext_realize (GtkWidget * widget)
{
}

static void
gtk_xtext_size_request (GtkWidget * widget, GtkRequisition * requisition)
{
}

static void
gtk_xtext_size_allocate (GtkWidget * widget, GtkAllocation * allocation)
{
}

static int
gtk_xtext_selection_clear (xtext_buffer *buf)
{
}

static int
find_x (GtkXText *xtext, textentry *ent, unsigned char *text, int x, int indent)
{
}

static int
gtk_xtext_find_x (GtkXText * xtext, int x, textentry * ent, int subline,
						int line, int *out_of_bounds)
{
}

static textentry *
gtk_xtext_find_char (GtkXText * xtext, int x, int y, int *off,
							int *out_of_bounds)
{
}

static void
gtk_xtext_draw_sep (GtkXText * xtext, int y)
{
}

static void
gtk_xtext_draw_marker (GtkXText * xtext, textentry * ent, int y)
{
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
gtk_xtext_paint (GtkWidget *widget, GdkRectangle *area)
{
}

static gboolean
gtk_xtext_expose (GtkWidget * widget, GdkEventExpose * event)
{
}

/* render a selection that has extended or contracted upward */

static void
gtk_xtext_selection_up (GtkXText *xtext, textentry *start, textentry *end,
								int start_offset)
{
}

/* render a selection that has extended or contracted downward */

static void
gtk_xtext_selection_down (GtkXText *xtext, textentry *start, textentry *end,
								  int end_offset)
{
}

static void
gtk_xtext_selection_render (GtkXText *xtext,
									 textentry *start_ent, int start_offset,
									 textentry *end_ent, int end_offset)
{
}

static void
gtk_xtext_selection_draw (GtkXText * xtext, GdkEventMotion * event, gboolean render)
{
}

static gint
gtk_xtext_scrolldown_timeout (GtkXText * xtext)
{
}

static gint
gtk_xtext_scrollup_timeout (GtkXText * xtext)
{
}

static void
gtk_xtext_selection_update (GtkXText * xtext, GdkEventMotion * event, int p_y, gboolean render)
{
}

static char *
gtk_xtext_get_word (GtkXText * xtext, int x, int y, textentry ** ret_ent,
						  int *ret_off, int *ret_len)
{
}

#ifdef MOTION_MONITOR

static void
gtk_xtext_unrender_hilight (GtkXText *xtext)
{
}

static gboolean
gtk_xtext_leave_notify (GtkWidget * widget, GdkEventCrossing * event)
{
}

#endif

/* check if we should mark time stamps, and if a redraw is needed */

static gboolean
gtk_xtext_check_mark_stamp (GtkXText *xtext, GdkModifierType mask)
{
}

static gboolean
gtk_xtext_motion_notify (GtkWidget * widget, GdkEventMotion * event)
{
}

static void
gtk_xtext_set_clip_owner (GtkWidget * xtext, GdkEventButton * event)
{
}

static void
gtk_xtext_unselect (GtkXText *xtext)
{
}

static gboolean
gtk_xtext_button_release (GtkWidget * widget, GdkEventButton * event)
{
}

static gboolean
gtk_xtext_button_press (GtkWidget * widget, GdkEventButton * event)
{
}

/* another program has claimed the selection */

static gboolean
gtk_xtext_selection_kill (GtkXText *xtext, GdkEventSelection *event)
{
}

static char *
gtk_xtext_selection_get_text (GtkXText *xtext, int *len_ret)
{
}

/* another program is asking for our selection */

static void
gtk_xtext_selection_get (GtkWidget * widget,
								 GtkSelectionData * selection_data_ptr,
								 guint info, guint time)
{
}

static gboolean
gtk_xtext_scroll (GtkWidget *widget, GdkEventScroll *event)
{
}

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

/* strip MIRC colors and other attribs. */

/* CL: needs to strip hidden when called by gtk_xtext_text_width, but not when copying text */

static unsigned char *
gtk_xtext_strip_color (unsigned char *text, int len, unsigned char *outbuf,
							  int *newlen, int *mb_ret, int strip_hidden)
{
}

/* gives width of a string, excluding the mIRC codes */

static int
gtk_xtext_text_width (GtkXText *xtext, unsigned char *text, int len,
							 int *mb_ret)
{
}

/* actually draw text to screen (one run with the same color/attribs) */

static int
gtk_xtext_render_flush (GtkXText * xtext, int x, int y, unsigned char *str,
								int len, GdkGC *gc, int is_mb)
{
}

static void
gtk_xtext_reset (GtkXText * xtext, int mark, int attribs)
{
}

/* render a single line, which WONT wrap, and parse mIRC colors */

static int
gtk_xtext_render_str (GtkXText * xtext, int y, textentry * ent,
							 unsigned char *str, int len, int win_width, int indent,
							 int line, int left_only, int *x_size_ret)
{
}

#ifdef USE_XLIB

/* get the desktop/root window */

static Window desktop_window = None;

static Window
get_desktop_window (Display *xdisplay, Window the_window)
{
	Atom prop, type;
	int format;
	unsigned long length, after;
	unsigned char *data;
	unsigned int nchildren;
	Window w, root, *children, parent;

	prop = XInternAtom (xdisplay, "_XROOTPMAP_ID", True);
	if (prop == None)
	{
		prop = XInternAtom (xdisplay, "_XROOTCOLOR_PIXEL", True);
		if (prop == None)
			return None;
	}

	for (w = the_window; w; w = parent)
	{
		if ((XQueryTree (xdisplay, w, &root, &parent, &children,
				&nchildren)) == False)
			return None;

		if (nchildren)
			XFree (children);

		XGetWindowProperty (xdisplay, w, prop, 0L, 1L, False,
								  AnyPropertyType, &type, &format, &length, &after,
								  &data);
		if (data)
			XFree (data);

		if (type != None)
			return (desktop_window = w);
	}

	return (desktop_window = None);
}

/* find the root window (backdrop) Pixmap */

static Pixmap
get_pixmap_prop (Display *xdisplay, Window the_window)
{
	Atom type;
	int format;
	unsigned long length, after;
	unsigned char *data;
	Pixmap pix = None;
	static Atom prop = None;

	if (desktop_window == None)
		desktop_window = get_desktop_window (xdisplay, the_window);
	if (desktop_window == None)
		desktop_window = DefaultRootWindow (xdisplay);

	if (prop == None)
		prop = XInternAtom (xdisplay, "_XROOTPMAP_ID", True);
	if (prop == None)
		return None;

	XGetWindowProperty (xdisplay, desktop_window, prop, 0L, 1L, False,
							  AnyPropertyType, &type, &format, &length, &after,
							  &data);
	if (data)
	{
		if (type == XA_PIXMAP)
			pix = *((Pixmap *) data);

		XFree (data);
	}

	return pix;
}

/* slow generic routine, for the depths/bpp we don't know about */

static void
shade_ximage_generic (GdkVisual *visual, XImage *ximg, int bpl, int w, int h, int rm, int gm, int bm, int bg)
{
	int x, y;
	int bgr = (256 - rm) * (bg & visual->red_mask);
	int bgg = (256 - gm) * (bg & visual->green_mask);
	int bgb = (256 - bm) * (bg & visual->blue_mask);

	for (x = 0; x < w; x++)
	{
		for (y = 0; y < h; y++)
		{
			unsigned long pixel = XGetPixel (ximg, x, y);
			int r, g, b;

			r = rm * (pixel & visual->red_mask) + bgr;
			g = gm * (pixel & visual->green_mask) + bgg;
			b = bm * (pixel & visual->blue_mask) + bgb;

			XPutPixel (ximg, x, y,
							((r >> 8) & visual->red_mask) |
							((g >> 8) & visual->green_mask) |
							((b >> 8) & visual->blue_mask));
		}
	}
}

#endif

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

/* RGB 15 */
static void
shade_ximage_15 (void *data, int bpl, int w, int h, int rm, int gm, int bm, int bg)
{
	SHADE_IMAGE (2, guint16, 0x7c00, 0x3e0, 0x1f);
}

/* RGB 16 */
static void
shade_ximage_16 (void *data, int bpl, int w, int h, int rm, int gm, int bm, int bg)
{
	SHADE_IMAGE (2, guint16, 0xf800, 0x7e0, 0x1f);
}

/* RGB 24 */
static void
shade_ximage_24 (void *data, int bpl, int w, int h, int rm, int gm, int bm, int bg)
{
	/* 24 has to be a special case, there's no guint24, or 24bit MOV :) */
	unsigned char *ptr;
	int x, y;
	int bgr = (256 - rm) * ((bg & 0xff0000) >> 16);
	int bgg = (256 - gm) * ((bg & 0xff00) >> 8);
	int bgb = (256 - bm) * (bg & 0xff);

	ptr = (unsigned char *) data + (w * 3);
	for (y = h; --y >= 0;)
	{
		for (x = -(w * 3); x < 0; x += 3)
		{
			int r, g, b;

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
			r = (ptr[x + 0] * rm + bgr) >> 8;
			g = (ptr[x + 1] * gm + bgg) >> 8;
			b = (ptr[x + 2] * bm + bgb) >> 8;
			ptr[x + 0] = r;
			ptr[x + 1] = g;
			ptr[x + 2] = b;
#else
			r = (ptr[x + 2] * rm + bgr) >> 8;
			g = (ptr[x + 1] * gm + bgg) >> 8;
			b = (ptr[x + 0] * bm + bgb) >> 8;
			ptr[x + 2] = r;
			ptr[x + 1] = g;
			ptr[x + 0] = b;
#endif
		}
		ptr += bpl;
	}
}

/* RGB 32 */
static void
shade_ximage_32 (void *data, int bpl, int w, int h, int rm, int gm, int bm, int bg)
{
	SHADE_IMAGE (4, guint32, 0xff0000, 0xff00, 0xff);
}

static void
shade_image (GdkVisual *visual, void *data, int bpl, int bpp, int w, int h,
				 int rm, int gm, int bm, int bg, int depth)
{
	int bg_r, bg_g, bg_b;

	bg_r = bg & visual->red_mask;
	bg_g = bg & visual->green_mask;
	bg_b = bg & visual->blue_mask;

#ifdef USE_MMX
	/* the MMX routines are about 50% faster at 16-bit. */
	/* only use MMX routines with a pure black background */
	if (bg_r == 0 && bg_g == 0 && bg_b == 0 && have_mmx ())	/* do a runtime check too! */
	{
		switch (depth)
		{
		case 15:
			shade_ximage_15_mmx (data, bpl, w, h, rm, gm, bm);
			break;
		case 16:
			shade_ximage_16_mmx (data, bpl, w, h, rm, gm, bm);
			break;
		case 24:
			if (bpp != 32)
				goto generic;
		case 32:
			shade_ximage_32_mmx (data, bpl, w, h, rm, gm, bm);
			break;
		default:
			goto generic;
		}
	} else
	{
generic:
#endif
		switch (depth)
		{
		case 15:
			shade_ximage_15 (data, bpl, w, h, rm, gm, bm, bg);
			break;
		case 16:
			shade_ximage_16 (data, bpl, w, h, rm, gm, bm, bg);
			break;
		case 24:
			if (bpp != 32)
			{
				shade_ximage_24 (data, bpl, w, h, rm, gm, bm, bg);
				break;
			}
		case 32:
			shade_ximage_32 (data, bpl, w, h, rm, gm, bm, bg);
		}
#ifdef USE_MMX
	}
#endif
}

#ifdef USE_XLIB

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

static GdkPixmap *
shade_pixmap (GtkXText * xtext, Pixmap p, int x, int y, int w, int h)
{
	unsigned int dummy, width, height, depth;
	GdkPixmap *shaded_pix;
	Window root;
	Pixmap tmp;
	XImage *ximg;
	XGCValues gcv;
	GC tgc;
	Display *xdisplay = GDK_WINDOW_XDISPLAY (xtext->draw_buf);

#ifdef USE_SHM
	int shm_pixmaps;
	shm_pixmaps = have_shm_pixmaps(xdisplay);
#endif

	XGetGeometry (xdisplay, p, &root, &dummy, &dummy, &width, &height,
					  &dummy, &depth);

	if (width < x + w || height < y + h || x < 0 || y < 0)
	{
		gcv.subwindow_mode = IncludeInferiors;
		gcv.graphics_exposures = False;
		tgc = XCreateGC (xdisplay, p, GCGraphicsExposures|GCSubwindowMode,
							  &gcv);
		tmp = XCreatePixmap (xdisplay, p, w, h, depth);
		XSetTile (xdisplay, tgc, p);
		XSetFillStyle (xdisplay, tgc, FillTiled);
		XSetTSOrigin (xdisplay, tgc, -x, -y);
		XFillRectangle (xdisplay, tmp, tgc, 0, 0, w, h);
		XFreeGC (xdisplay, tgc);

#ifdef USE_SHM
		if (shm_pixmaps)
			ximg = get_image (xtext, xdisplay, &xtext->shminfo, 0, 0, w, h, depth, tmp);
		else
#endif
			ximg = XGetImage (xdisplay, tmp, 0, 0, w, h, -1, ZPixmap);
		XFreePixmap (xdisplay, tmp);
	} else
	{
#ifdef USE_SHM
		if (shm_pixmaps)
			ximg = get_image (xtext, xdisplay, &xtext->shminfo, x, y, w, h, depth, p);
		else
#endif
			ximg = XGetImage (xdisplay, p, x, y, w, h, -1, ZPixmap);
	}

	if (!ximg)
		return NULL;

	if (depth <= 14)
	{
		shade_ximage_generic (gdk_drawable_get_visual (GTK_WIDGET (xtext)->window),
									 ximg, ximg->bytes_per_line, w, h, xtext->tint_red,
									 xtext->tint_green, xtext->tint_blue,
									 xtext->palette[XTEXT_BG]);
	} else
	{
		shade_image (gdk_drawable_get_visual (GTK_WIDGET (xtext)->window),
						 ximg->data, ximg->bytes_per_line, ximg->bits_per_pixel,
						 w, h, xtext->tint_red, xtext->tint_green, xtext->tint_blue,
						 xtext->palette[XTEXT_BG], depth);
	}

	if (xtext->recycle)
		shaded_pix = xtext->pixmap;
	else
	{
#ifdef USE_SHM
		if (xtext->shm && shm_pixmaps)
		{
#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION == 0)
			shaded_pix = gdk_pixmap_foreign_new (
				XShmCreatePixmap (xdisplay, p, ximg->data, &xtext->shminfo, w, h, depth));
#else
			shaded_pix = gdk_pixmap_foreign_new_for_display (
				gdk_drawable_get_display (xtext->draw_buf),
				XShmCreatePixmap (xdisplay, p, ximg->data, &xtext->shminfo, w, h, depth));
#endif
		} else
#endif
		{
			shaded_pix = gdk_pixmap_new (GTK_WIDGET (xtext)->window, w, h, depth);
		}
	}

#ifdef USE_SHM
	if (!xtext->shm || !shm_pixmaps)
#endif
		XPutImage (xdisplay, GDK_WINDOW_XWINDOW (shaded_pix),
					  GDK_GC_XGC (xtext->fgc), ximg, 0, 0, 0, 0, w, h);
	XDestroyImage (ximg);

	return shaded_pix;
}

#endif /* !USE_XLIB */

/* free transparency xtext->pixmap */
#if defined(USE_XLIB) || defined(WIN32)

static void
gtk_xtext_free_trans (GtkXText * xtext)
{
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

/* grab pixmap from root window and set xtext->pixmap */
#if defined(USE_XLIB) || defined(WIN32)

static void
gtk_xtext_load_trans (GtkXText * xtext)
{
}

#endif /* ! XLIB || WIN32 */

/* walk through str until this line doesn't fit anymore */

static int
find_next_wrap (GtkXText * xtext, textentry * ent, unsigned char *str,
					 int win_width, int indent)
{
	unsigned char *last_space = str;
	unsigned char *orig_str = str;
	int str_width = indent;
	int rcol = 0, bgcol = 0;
	int hidden = FALSE;
	int mbl;
	int char_width;
	int ret;
	int limit_offset = 0;

	/* single liners */
	if (win_width >= ent->str_width + ent->indent)
		return ent->str_len;

	/* it does happen! */
	if (win_width < 1)
	{
		ret = ent->str_len - (str - ent->str);
		goto done;
	}

	while (1)
	{
		if (rcol > 0 && (isdigit (*str) || (*str == ',' && isdigit (str[1]) && !bgcol)))
		{
			if (str[1] != ',') rcol--;
			if (*str == ',')
			{
				rcol = 2;
				bgcol = 1;
			}
			limit_offset++;
			str++;
		} else
		{
			rcol = bgcol = 0;
			switch (*str)
			{
			case ATTR_COLOR:
				rcol = 2;
			case ATTR_BEEP:
			case ATTR_RESET:
			case ATTR_REVERSE:
			case ATTR_BOLD:
			case ATTR_UNDERLINE:
			case ATTR_ITALICS:
				limit_offset++;
				str++;
				break;
			case ATTR_HIDDEN:
				if (xtext->ignore_hidden)
					goto def;
				hidden = !hidden;
				limit_offset++;
				str++;
				break;
			default:
			def:
				char_width = backend_get_char_width (xtext, str, &mbl);
				if (!hidden) str_width += char_width;
				if (str_width > win_width)
				{
					if (xtext->wordwrap)
					{
						if (str - last_space > WORDWRAP_LIMIT + limit_offset)
							ret = str - orig_str; /* fall back to character wrap */
						else
						{
							if (*last_space == ' ')
								last_space++;
							ret = last_space - orig_str;
							if (ret == 0) /* fall back to character wrap */
								ret = str - orig_str;
						}
						goto done;
					}
					ret = str - orig_str;
					goto done;
				}

				/* keep a record of the last space, for wordwrapping */
				if (is_del (*str))
				{
					last_space = str;
					limit_offset = 0;
				}

				/* progress to the next char */
				str += mbl;

			}
		}

		if (str >= ent->str + ent->str_len)
		{
			ret = str - orig_str;
			goto done;
		}
	}

done:

	/* must make progress */
	if (ret < 1)
		ret = 1;

	return ret;
}

/* find the offset, in bytes, that wrap number 'line' starts at */

static int
gtk_xtext_find_subline (GtkXText *xtext, textentry *ent, int line)
{
}

/* horrible hack for drawing time stamps */

static void
gtk_xtext_render_stamp (GtkXText * xtext, textentry * ent,
								char *text, int len, int line, int win_width)
{
}

/* render a single line, which may wrap to more lines */

static int
gtk_xtext_render_line (GtkXText * xtext, textentry * ent, int line,
							  int lines_max, int subline, int win_width)
{
}


static void
gtk_xtext_fix_indent (xtext_buffer *buf)
{
}

static void
gtk_xtext_recalc_widths (xtext_buffer *buf, int do_str_width)
{
}

/* count how many lines 'ent' will take (with wraps) */

static int
gtk_xtext_lines_taken (xtext_buffer *buf, textentry * ent)
{
}

/* Calculate number of actual lines (with wraps), to set adj->lower. *
 * This should only be called when the window resizes.               */

static void
gtk_xtext_calc_lines (xtext_buffer *buf, int fire_signal)
{
}

/* find the n-th line in the linked list, this includes wrap calculations */

static textentry *
gtk_xtext_nth (GtkXText *xtext, int line, int *subline)
{
}

/* render enta (or an inclusive range enta->entb) */

static int
gtk_xtext_render_ents (GtkXText * xtext, textentry * enta, textentry * entb)
{
}

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

gboolean gtk_xtext_is_empty (xtext_buffer *buf){}
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
