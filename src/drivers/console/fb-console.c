/*
 * drivers/console/fb-console.c
 *
 * Copyright(c) 2007-2013 jianjun jiang <jerryjianjun@gmail.com>
 * official site: http://xboot.org
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <xboot.h>
#include <console/console.h>
#include <console/fb-font.h>
#include <console/fb-console.h>

struct fb_console_cell_t
{
	/* code pointer */
	u32_t cp;

	/* foreground color and background color */
	struct color_t fc, bc;
};

struct fb_console_info_t
{
	/* the console name */
	char * name;

	/* framebuffer driver */
	struct fb_t * fb;

	/* console font width and height in pixel */
	s32_t fw, fh;

	/* console width and height */
	s32_t w, h;

	/* console current x, y */
	s32_t x, y;

	/* console front color and background color */
	enum tcolor_t f, b;
	struct color_t fc, bc;

	/* cursor status, on or off */
	bool_t cursor;

	/* on/off status */
	bool_t onoff;

	/* fb console's cell */
	struct fb_console_cell_t * cell;

	/* fb console cell's length */
	u32_t clen;
};

static const u8_t tcolor_to_rgba_table[16][3] = {
	/* 0x00 */	{ 0x00, 0x00, 0x00 },
	/* 0x01 */	{ 0xcd, 0x00, 0x00 },
	/* 0x02 */	{ 0x00, 0xcd, 0x00 },
	/* 0x03 */	{ 0xcd, 0xcd, 0x00 },
	/* 0x04 */	{ 0x00, 0x00, 0xee },
	/* 0x05 */	{ 0xcd, 0x00, 0xcd },
	/* 0x06 */	{ 0x00, 0xcd, 0xcd },
	/* 0x07 */	{ 0xe5, 0xe5, 0xe5 },
	/* 0x08 */	{ 0x7f, 0x7f, 0x7f },
	/* 0x09 */	{ 0xff, 0x00, 0x00 },
	/* 0x0a */	{ 0x00, 0xff, 0x00 },
	/* 0x0b */	{ 0xff, 0xff, 0x00 },
	/* 0x0c */	{ 0x5c, 0x5c, 0xff },
	/* 0x0d */	{ 0xff, 0x00, 0xff },
	/* 0x0e */	{ 0x00, 0xff, 0xff },
	/* 0x0f */	{ 0xff, 0xff, 0xff },
};

static bool_t tcolor_to_color(enum tcolor_t c, struct color_t * col)
{
	u8_t index = c & 0x0f;

	col->r = tcolor_to_rgba_table[index][0];
	col->g = tcolor_to_rgba_table[index][1];
	col->b = tcolor_to_rgba_table[index][2];
	col->a = 0xff;

	return TRUE;
}

static void fb_helper_fill_rect(struct fb_t * fb, struct color_t * c, u32_t x, u32_t y, u32_t w, u32_t h)
{
	struct rect_t rect;

	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;

	render_clear(fb->alone, &rect, c);
}

static void fb_helper_blit(struct fb_t * fb, struct texture_t * texture, u32_t x, u32_t y, u32_t w, u32_t h, u32_t ox, u32_t oy)
{
	struct rect_t drect, srect;

	drect.x = x;
	drect.y = y;
	drect.w = w;
	drect.h = h;

	srect.x = ox;
	srect.y = oy;
	srect.w = w;
	srect.h = h;

	render_blit_texture(fb->alone, &drect, texture, &srect);
}

static void fb_helper_putcode(struct fb_t * fb, u32_t code, struct color_t * fc, struct color_t * bc, u32_t x, u32_t y)
{
	struct texture_t * face = lookup_console_font_face(fb->alone, code, fc, bc);

	if(face)
		fb_helper_blit(fb, face, x, y, face->width, face->height, 0, 0);
	render_free_texture(fb->alone, face);
}

static bool_t fb_console_getwh(struct console_t * console, s32_t * w, s32_t * h)
{
	struct fb_console_info_t * info = console->priv;

	if(!info->onoff)
		return FALSE;

	if(w)
		*w = info->w;

	if(h)
		*h = info->h;

	return TRUE;
}

static bool_t fb_console_getxy(struct console_t * console, s32_t * x, s32_t * y)
{
	struct fb_console_info_t * info = console->priv;

	if(!info->onoff)
		return FALSE;

	if(x)
		*x = info->x;

	if(y)
		*y = info->y;

	return TRUE;
}

static bool_t fb_console_gotoxy(struct console_t * console, s32_t x, s32_t y)
{
	struct fb_console_info_t * info = console->priv;
	struct fb_console_cell_t * cell;
	s32_t pos, px, py;

	if(!info->onoff)
		return FALSE;

	if(x < 0)
		x = 0;
	if(y < 0)
		y = 0;

	if(x > info->w - 1)
		x = info->w - 1;
	if(y > info->h - 1)
		y = info->h - 1;

	if(info->cursor)
	{
		pos = info->w * info->y + info->x;
		cell = &(info->cell[pos]);
		px = (pos % info->w) * info->fw;
		py = (pos / info->w) * info->fh;
		fb_helper_putcode(info->fb, cell->cp, &(cell->fc), &(cell->bc), px, py);

		pos = info->w * y + x;
		cell = &(info->cell[pos]);
		px = (pos % info->w) * info->fw;
		py = (pos / info->w) * info->fh;
		fb_helper_putcode(info->fb, cell->cp, &(info->bc), &(info->fc), px, py);
	}

	info->x = x;
	info->y = y;

	return TRUE;
}

static bool_t fb_console_setcursor(struct console_t * console, bool_t on)
{
	struct fb_console_info_t * info = console->priv;
	struct fb_console_cell_t * cell;
	s32_t pos, px, py;

	if(!info->onoff)
		return FALSE;

	info->cursor = on;

	pos = info->w * info->y + info->x;
	cell = &(info->cell[pos]);
	px = (pos % info->w) * info->fw;
	py = (pos / info->w) * info->fh;

	if(info->cursor)
		fb_helper_putcode(info->fb, cell->cp, &(info->bc), &(info->fc), px, py);
	else
		fb_helper_putcode(info->fb, cell->cp, &(cell->fc), &(cell->bc), px, py);

	return TRUE;
}

static bool_t fb_console_getcursor(struct console_t * console)
{
	struct fb_console_info_t * info = console->priv;

	if(!info->onoff)
		return FALSE;

	return info->cursor;
}

static bool_t fb_console_setcolor(struct console_t * console, enum tcolor_t f, enum tcolor_t b)
{
	struct fb_console_info_t * info = console->priv;

	if(!info->onoff)
		return FALSE;

	info->f = f;
	info->b = b;

	tcolor_to_color(f, &(info->fc));
	tcolor_to_color(b, &(info->bc));

	return TRUE;
}

static bool_t fb_console_getcolor(struct console_t * console, enum tcolor_t * f, enum tcolor_t * b)
{
	struct fb_console_info_t * info = console->priv;

	if(!info->onoff)
		return FALSE;

	*f = info->f;
	*b = info->b;

	return TRUE;
}

static bool_t fb_console_cls(struct console_t * console)
{
	struct fb_console_info_t * info = console->priv;
	struct fb_console_cell_t * cell = &(info->cell[0]);
	s32_t i;

	if(!info->onoff)
		return FALSE;

	for(i = 0; i < info->clen; i++)
	{
		cell->cp = UNICODE_SPACE;
		memcpy(&(cell->fc), &(info->fc), sizeof(struct color_t));
		memcpy(&(cell->bc), &(info->bc), sizeof(struct color_t));

		cell++;
	}

	fb_helper_fill_rect(info->fb, &(info->bc), 0, 0, (info->w * info->fw), (info->h * info->fh));
	fb_console_gotoxy(console, 0, 0);

	return TRUE;
}

static bool_t fb_console_scrollup(struct console_t * console)
{
	struct fb_console_info_t * info = console->priv;
	struct fb_console_cell_t * p, * q;
	s32_t m, l;
	s32_t i;

	l = info->w;
	m = info->clen - l;
	p = &(info->cell[0]);
	q = &(info->cell[l]);

	for(i = 0; i < m; i++)
	{
		p->cp = q->cp;
		p->fc = q->fc;
		p->bc = q->bc;

		p++;
		q++;
	}

	while( (l--) > 0 )
	{
		p->cp = UNICODE_SPACE;
		p->fc = info->fc;
		p->bc = info->bc;

		p++;
	}

	struct texture_t * t = render_snapshot(info->fb->alone);
	fb_helper_blit(info->fb, t, 0, 0, (info->w * info->fw), ((info->h - 1) * info->fh), 0, info->fh);
	render_free_texture(info->fb->alone, t);
	fb_helper_fill_rect(info->fb, &(info->bc), 0, ((info->h - 1) * info->fh), (info->w * info->fw), info->fh);
	fb_console_gotoxy(console, info->x, info->y - 1);

	return TRUE;
}

static bool_t fb_console_putcode(struct console_t * console, u32_t code)
{
	struct fb_console_info_t * info = console->priv;
	struct fb_console_cell_t * cell;
	s32_t pos, px, py;
	s32_t w, i;

	if(!info->onoff)
		return FALSE;

	switch(code)
	{
	case UNICODE_BS:
		return TRUE;

	case UNICODE_TAB:
		i = 8 - (info->x % 8);
		if(i + info->x >= info->w)
			i = info->w - info->x - 1;

		while(i--)
		{
			pos = info->w * info->y + info->x;
			cell = &(info->cell[pos]);

			cell->cp = UNICODE_SPACE;
			memcpy(&(cell->fc), &(info->fc), sizeof(struct color_t));
			memcpy(&(cell->bc), &(info->bc), sizeof(struct color_t));

			px = (pos % info->w) * info->fw;
			py = (pos / info->w) * info->fh;
			fb_helper_putcode(info->fb, cell->cp, &(cell->fc), &(cell->bc), px, py);
			info->x = info->x + 1;
		}
		fb_console_gotoxy(console, info->x, info->y);
		break;

	case UNICODE_LF:
		if(info->y + 1 >= info->h)
			fb_console_scrollup(console);
		fb_console_gotoxy(console, 0, info->y + 1);
		break;

	case UNICODE_CR:
		fb_console_gotoxy(console, 0, info->y);
		break;

	default:
		w = ucs4_width(code);
		if(w <= 0)
			return TRUE;

		pos = info->w * info->y + info->x;
		cell = &(info->cell[pos]);

		cell->cp = code;
		memcpy(&(cell->fc), &(info->fc), sizeof(struct color_t));
		memcpy(&(cell->bc), &(info->bc), sizeof(struct color_t));

		for(i = 1; i < w; i++)
		{
			((struct fb_console_cell_t *)(cell + i))->cp = UNICODE_SPACE;
			((struct fb_console_cell_t *)(cell + i))->fc = info->fc;
			((struct fb_console_cell_t *)(cell + i))->bc = info->bc;
		}

		px = (pos % info->w) * info->fw;
		py = (pos / info->w) * info->fh;
		fb_helper_putcode(info->fb, cell->cp, &(cell->fc), &(cell->bc), px, py);

		if(info->x + w < info->w)
			fb_console_gotoxy(console, info->x + w, info->y);
		else
		{
			if(info->y + 1 >= info->h)
				fb_console_scrollup(console);
			fb_console_gotoxy(console, 0, info->y + 1);
		}
		break;
	}

	return TRUE;
}

static bool_t fb_console_onoff(struct console_t * console, bool_t flag)
{
	struct fb_console_info_t * info = console->priv;

	info->onoff = flag;
	return TRUE;
}

bool_t register_framebuffer_console(struct fb_t * fb)
{
	struct console_t * console;
	struct fb_console_info_t * info;

	if(!fb || !fb->name)
		return FALSE;

	console = malloc(sizeof(struct console_t));
	info = malloc(sizeof(struct fb_console_info_t));
	if(!console || !info)
	{
		free(console);
		free(info);
		return FALSE;
	}

	info->name = (char *)fb->name;
	info->fb = fb;
	info->fw = 8;
	info->fh = 16;
	info->w = fb->alone->width / info->fw;
	info->h = fb->alone->height / info->fh;
	info->x = 0;
	info->y = 0;
	info->f = TCOLOR_WHITE;
	info->b = TCOLOR_BLACK;
	tcolor_to_color(info->f, &(info->fc));
	tcolor_to_color(info->b, &(info->bc));
	info->cursor = TRUE;
	info->onoff = TRUE;
	info->clen = info->w * info->h;
	info->cell = malloc(info->clen * sizeof(struct fb_console_cell_t));
	if(!info->cell)
	{
		free(console);
		free(info);
		return FALSE;
	}
	memset(info->cell, 0, info->clen * sizeof(struct fb_console_cell_t));

	console->name = strdup(info->name);
	console->getwh = fb_console_getwh;
	console->getxy = fb_console_getxy;
	console->gotoxy = fb_console_gotoxy;
	console->setcursor = fb_console_setcursor;
	console->getcursor = fb_console_getcursor;
	console->setcolor = fb_console_setcolor;
	console->getcolor = fb_console_getcolor;
	console->cls = fb_console_cls;
	console->getcode = NULL;
	console->putcode = fb_console_putcode;
	console->onoff = fb_console_onoff;
	console->priv = info;

	if(!register_console(console))
	{
		free(console->name);
		free(console);
		free(info->cell);
		free(info);
		return FALSE;
	}

	return TRUE;
}

bool_t unregister_framebuffer_console(struct fb_t * fb)
{
	struct console_t * console;
	struct fb_console_info_t * info;

	if(!fb || !fb->name)
		return FALSE;

	console = search_console(fb->name);
	if(console)
		info = (struct fb_console_info_t *)console->priv;
	else
		return FALSE;

	if(!unregister_console(console))
		return FALSE;

	free(console->name);
	free(console);
	free(info->cell);
	free(info);

	return TRUE;
}