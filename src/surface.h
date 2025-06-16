/***************************************************************************
 *   Copyright (C) 2006 by Massimiliano Torromeo                           *
 *                         massimiliano.torromeo@gmail.com                 *
 *   Copyright (C) 2010-2014 by various authors; see Git log               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef SURFACE_H
#define SURFACE_H

#include "font_stack.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

class GMenu2X;

struct RGBAColor {
	uint8_t r, g, b, a;
	static RGBAColor fromString(std::string const& strColor);
	RGBAColor() : r(0), g(0), b(0), a(0) {}
	RGBAColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
		: r(r), g(g), b(b), a(a) {}
	Uint32 pixelValue(SDL_PixelFormat *fmt) const {
		return SDL_MapRGBA(fmt, r, g, b, a);
	}
};
std::ostream& operator<<(std::ostream& os, RGBAColor const& color);

/**
 * Abstract base class for surfaces; wraps SDL_Texture.
 */
class Surface {
public:
	static void setGlobalRenderer(SDL_Renderer* renderer) { globalRenderer = renderer; }
	static SDL_Renderer* getGlobalRenderer() { return globalRenderer; }

	Surface& operator=(Surface const& other) = delete;

	int width() const { return w; }
	int height() const { return h; }

	void clearClipRect();
	void setClipRect(int x, int y, int w, int h);
	void setClipRect(SDL_Rect rect);

	void blit(Surface& destination, int x, int y, int w=0, int h=0, int a=-1) const;
	void blit(Surface& destination, SDL_Rect container, Font::HAlign halign = Font::HAlignLeft, Font::VAlign valign = Font::VAlignTop) const;
	void blitCenter(Surface& destination, int x, int y, int w=0, int h=0, int a=-1) const;
	void blitRight(Surface& destination, int x, int y, int w=0, int h=0, int a=-1) const;

	void box(SDL_Rect re, RGBAColor c);
	void box(Sint16 x, Sint16 y, Uint16 w, Uint16 h, RGBAColor c) {
		box(SDL_Rect{ x, y, w, h }, c);
	}
	void box(Sint16 x, Sint16 y, Uint16 w, Uint16 h, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
		box(SDL_Rect{ x, y, w, h }, RGBAColor(r, g, b, a));
	}
	void rectangle(SDL_Rect re, RGBAColor c);
	void rectangle(Sint16 x, Sint16 y, Uint16 w, Uint16 h, RGBAColor c) {
		rectangle(SDL_Rect{ x, y, w, h }, c);
	}
	void rectangle(Sint16 x, Sint16 y, Uint16 w, Uint16 h, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
		rectangle(SDL_Rect{ x, y, w, h }, RGBAColor(r, g, b, a));
	}

protected:
	Surface(SDL_Texture *texture, SDL_Renderer *renderer = nullptr) 
		: texture(texture)
		, renderer(renderer ? renderer : globalRenderer)
		, w(0)
		, h(0)
	{
		if (texture) {
			SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
		}
	}
	Surface(Surface const& other);

	SDL_Texture *texture;
	SDL_Renderer *renderer;
	int w, h;

	// For direct access to texture and renderer
	friend class Font;

private:
	static SDL_Renderer* globalRenderer;
	static SDL_Texture* globalTexture;

	void blit(SDL_Texture *destination, int x, int y, int w=0, int h=0, int a=-1) const;
	void blitCenter(SDL_Texture *destination, int x, int y, int w=0, int h=0, int a=-1) const;
	void blitRight(SDL_Texture *destination, int x, int y, int w=0, int h=0, int a=-1) const;

	/** Clips the given rectangle against this surface's active clipping
	  * rectangle.
	  */
	void applyClipRect(SDL_Rect& rect);
};

/**
 * A surface that is off-screen: not visible.
 */
class OffscreenSurface: public Surface {
public:
	static std::shared_ptr<OffscreenSurface> emptySurface(
			const GMenu2X &gmenu2x, int width, int height);
	static std::shared_ptr<OffscreenSurface> loadImage(
			const GMenu2X &gmenu2x, const std::string& img,
			unsigned int width = 0, unsigned int height = 0,
			bool loadAlpha = true);

	OffscreenSurface(Surface const& other) : Surface(other) {}
	OffscreenSurface(OffscreenSurface const& other) : Surface(other) {}
	OffscreenSurface(OffscreenSurface&& other);
	~OffscreenSurface();
	OffscreenSurface& operator=(OffscreenSurface other);
	void swap(OffscreenSurface& other);

	/**
	 * Converts the underlying surface to the same pixel format as the frame
	 * buffer, for faster blitting. This removes the alpha channel if the
	 * image has one.
	 */
	void convertToDisplayFormat();

private:
	friend class FontStack;
	OffscreenSurface(SDL_Surface *raw) : Surface(SDL_CreateTextureFromSurface(Surface::getGlobalRenderer(), raw)) {}
	OffscreenSurface(SDL_Texture *texture, SDL_Renderer *renderer = nullptr) : Surface(texture, renderer) {}
};

/**
 * A surface that is used for writing to a video output device.
 */
class OutputSurface: public Surface {
public:
	static std::unique_ptr<OutputSurface> open(
			const char *caption, int width, int height, int bitsPerPixel);

	static bool resolutionSupported(int width, int height);

	/**
	 * Offers the current buffer to the video system to be presented and
	 * acquires a new buffer to draw into.
	 */
	void flip();
	~OutputSurface();

private:
	OutputSurface(SDL_Texture *texture, SDL_Renderer *renderer, SDL_Window *window);
	SDL_Window *window;
};

#endif
