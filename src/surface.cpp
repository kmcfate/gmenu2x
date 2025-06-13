/***************************************************************************
 *   Copyright (C) 2006 by Massimiliano Torromeo                           *
 *   massimiliano.torromeo@gmail.com                                       *
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

#include "surface.h"

#include "SDL_render.h"
#include "compat-algorithm.h"
#include "debug.h"
#include "gmenu2x.h"
#include "imageio.h"
#include "utilities.h"
#include "buildopts.h"

#include <cassert>
#include <iomanip>
#include <utility>

#include <SDL2/SDL2_rotozoom.h>

using namespace std;

SDL_Renderer* Surface::globalRenderer = nullptr;

// RGBAColor:

RGBAColor RGBAColor::fromString(const string &strColor) {
	return {
		uint8_t(compat::clamp(std::stoi(strColor.substr(0, 2), nullptr, 16),
		                     0, 255)),
		uint8_t(compat::clamp(std::stoi(strColor.substr(2, 2).c_str(), nullptr, 16),
		                     0, 255)),
		uint8_t(compat::clamp(std::stoi(strColor.substr(4, 2).c_str(), nullptr, 16),
		                     0, 255)),
		uint8_t(compat::clamp(std::stoi(strColor.substr(6, 2).c_str(), nullptr, 16),
		                     0, 255)),
	};
}

ostream& operator<<(ostream& os, RGBAColor const& color) {
	auto oldfill = os.fill('0');
	auto oldflags = os.setf(ios::hex | ios::right,
	                        ios::basefield | ios::adjustfield);
	os << setw(2) << uint32_t(color.r)
	   << setw(2) << uint32_t(color.g)
	   << setw(2) << uint32_t(color.b)
	   << setw(2) << uint32_t(color.a);
	os.fill(oldfill);
	os.setf(oldflags);
	return os;
}


// Surface:

Surface::Surface(Surface const& other)
	: renderer(other.renderer)
	, w(other.w)
	, h(other.h)
{
	Uint32 format;
	SDL_QueryTexture(other.texture, &format, nullptr, nullptr, nullptr);
	texture = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_TARGET, w, h);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	SDL_Texture* currentTexture = SDL_GetRenderTarget(renderer);
	SDL_SetRenderTarget(renderer, texture);
	SDL_RenderCopy(renderer, other.texture, nullptr, nullptr);
	SDL_SetRenderTarget(renderer, currentTexture);
}

void Surface::blit(SDL_Texture *destination, int x, int y, int w, int h, int a) const {
	if (destination == NULL || a==0) return;

	SDL_Rect src = { 0, 0, static_cast<Uint16>(w ? w : this->w), static_cast<Uint16>(h ? h : this->h) };
	SDL_Rect dest = { x, y, src.w, src.h };
	
	if (a != -1) {
		SDL_SetTextureAlphaMod(texture, a);
	}
	SDL_RenderCopy(renderer, texture, &src, &dest);
	if (a != -1) {
		SDL_SetTextureAlphaMod(texture, 255);
	}
}

void Surface::blit(Surface& destination, int x, int y, int w, int h, int a) const {
	blit(destination.texture, x, y, w, h, a);
}

void Surface::blitCenter(SDL_Texture *destination, int x, int y, int w, int h, int a) const {
	int ow = this->w / 2; if (w != 0) ow = min(ow, w / 2);
	int oh = this->h / 2; if (h != 0) oh = min(oh, h / 2);
	blit(destination, x - ow, y - oh, w, h, a);
}

void Surface::blitCenter(Surface& destination, int x, int y, int w, int h, int a) const {
	blitCenter(destination.texture, x, y, w, h, a);
}

void Surface::blitRight(SDL_Texture *destination, int x, int y, int w, int h, int a) const {
	if (!w) w = this->w;
	blit(destination, x - min(this->w, w), y, w, h, a);
}

void Surface::blitRight(Surface& destination, int x, int y, int w, int h, int a) const {
	if (!w) w = this->w;
	blitRight(destination.texture, x, y, w, h, a);
}

void Surface::box(SDL_Rect re, RGBAColor c) {
	if (c.a == 255) {
		SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
		SDL_RenderFillRect(renderer, &re);
	} else if (c.a != 0) {
		fillRectAlpha(re, c);
	}
}

void Surface::rectangle(SDL_Rect re, RGBAColor c) {
	if (re.h >= 1) {
		// Top.
		box(SDL_Rect { re.x, re.y, re.w, 1 }, c);
	}
	if (re.h >= 2) {
		Sint16 ey = re.y + re.h - 1;
		// Bottom.
		box(SDL_Rect { re.x, ey, re.w, 1 }, c);

		Sint16 ex = re.x + re.w - 1;
		Sint16 sy = re.y + 1;
		Uint16 sh = re.h - 2;
		// Left.
		if (re.w >= 1) {
			box(SDL_Rect { re.x, sy, 1, sh }, c);
		}
		// Right.
		if (re.w >= 2) {
			box(SDL_Rect { ex, sy, 1, sh }, c);
		}
	}
}

void Surface::clearClipRect() {
	SDL_RenderSetClipRect(renderer, nullptr);
}

void Surface::setClipRect(int x, int y, int w, int h) {
	SDL_Rect rect = {
		static_cast<Sint16>(x), static_cast<Sint16>(y),
		static_cast<Uint16>(w), static_cast<Uint16>(h)
	};
	setClipRect(rect);
}

void Surface::setClipRect(SDL_Rect rect) {
	SDL_RenderSetClipRect(renderer, &rect);
}

void Surface::applyClipRect(SDL_Rect& rect) {
	SDL_Rect clip;
	SDL_RenderGetClipRect(renderer, &clip);

	// Clip along X-axis.
	if (rect.x < clip.x) {
		rect.w = max(rect.x + rect.w - clip.x, 0);
		rect.x = clip.x;
	}
	if (rect.x + rect.w > clip.x + clip.w) {
		rect.w = max(clip.x + clip.w - rect.x, 0);
	}

	// Clip along Y-axis.
	if (rect.y < clip.y) {
		rect.h = max(rect.y + rect.h - clip.y, 0);
		rect.y = clip.y;
	}
	if (rect.y + rect.h > clip.y + clip.h) {
		rect.h = max(clip.y + clip.h - rect.y, 0);
	}
}

void Surface::blit(Surface& destination, SDL_Rect container, Font::HAlign halign, Font::VAlign valign) const {
	switch (halign) {
	case Font::HAlignLeft:
		break;
	case Font::HAlignCenter:
		container.x += container.w / 2 - w / 2;
		break;
	case Font::HAlignRight:
		container.x += container.w-w;
		break;
	}

	switch (valign) {
	case Font::VAlignTop:
		break;
	case Font::VAlignMiddle:
		container.y += container.h / 2 - h / 2;
		break;
	case Font::VAlignBottom:
		container.y += container.h-h;
		break;
	}

	blit(destination, container.x, container.y);
}

void Surface::fillRectAlpha(SDL_Rect rect, RGBAColor c) {
	applyClipRect(rect);
	if (rect.w == 0 || rect.h == 0) {
		return;
	}

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
	SDL_RenderFillRect(renderer, &rect);
}


// OffscreenSurface:

shared_ptr<OffscreenSurface> OffscreenSurface::emptySurface(
		const GMenu2X &gmenu2x, int width, int height)
{
	SDL_Texture *texture = SDL_CreateTexture(
		Surface::getGlobalRenderer(),
		SDL_PIXELFORMAT_ABGR8888,
		SDL_TEXTUREACCESS_TARGET,
		width,
		height
	);
	if (!texture)
		return shared_ptr<OffscreenSurface>();

	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	SDL_Texture* currentTexture = SDL_GetRenderTarget(Surface::getGlobalRenderer());
	SDL_SetRenderTarget(Surface::getGlobalRenderer(), texture);
	SDL_SetRenderDrawColor(Surface::getGlobalRenderer(), 0, 0, 0, 255);
	SDL_RenderClear(Surface::getGlobalRenderer());
	SDL_SetRenderTarget(Surface::getGlobalRenderer(), currentTexture);

	return shared_ptr<OffscreenSurface>(new OffscreenSurface(texture));
}

shared_ptr<OffscreenSurface> OffscreenSurface::loadImage(
		const GMenu2X &gmenu2x, const string& img,
		unsigned int width, unsigned int height, bool loadAlpha)
{
	SDL_Surface *raw = loadPNG(img, loadAlpha);
	if (!raw) {
		DEBUG("Couldn't load surface '%s'\n", img.c_str());
		return shared_ptr<OffscreenSurface>();
	}

	SDL_Texture *texture = SDL_CreateTextureFromSurface(Surface::getGlobalRenderer(), raw);
	SDL_FreeSurface(raw);

	if (!texture) {
		DEBUG("Couldn't create texture from surface '%s'\n", img.c_str());
		return shared_ptr<OffscreenSurface>();
	}

	int texW, texH;
	Uint32 format;
	SDL_QueryTexture(texture, &format, nullptr, &texW, &texH);

	if ((width && texW != width) || (height && texH != height)) {
		SDL_Texture *stretched = SDL_CreateTexture(
			Surface::getGlobalRenderer(),
			format,
			SDL_TEXTUREACCESS_TARGET,
			width ? width : texW,
			height ? height : texH
		);
		if (stretched) {
			SDL_SetTextureBlendMode(stretched, SDL_BLENDMODE_BLEND);
			SDL_Texture* currentTexture = SDL_GetRenderTarget(Surface::getGlobalRenderer());
			SDL_SetRenderTarget(Surface::getGlobalRenderer(), stretched);
			SDL_RenderCopy(Surface::getGlobalRenderer(), texture, nullptr, nullptr);
			SDL_SetRenderTarget(Surface::getGlobalRenderer(), currentTexture);
			SDL_DestroyTexture(texture);
			texture = stretched;
		}
	}

	return shared_ptr<OffscreenSurface>(new OffscreenSurface(texture));
}

OffscreenSurface::OffscreenSurface(OffscreenSurface&& other)
	: Surface(other.texture, other.renderer)
{
	other.texture = nullptr;
}

OffscreenSurface::~OffscreenSurface()
{
	if (texture) SDL_DestroyTexture(texture);
}

OffscreenSurface& OffscreenSurface::operator=(OffscreenSurface other)
{
	swap(other);
	return *this;
}

void OffscreenSurface::swap(OffscreenSurface& other)
{
	std::swap(texture, other.texture);
	std::swap(renderer, other.renderer);
	std::swap(w, other.w);
	std::swap(h, other.h);
}

void OffscreenSurface::convertToDisplayFormat() {
	// No need to convert format with textures
}

bool OutputSurface::resolutionSupported(int width, int height)
{
	SDL_DisplayMode mode;
	SDL_DisplayMode target = {0, width, height, 0, nullptr};
	if (SDL_GetClosestDisplayMode(0, &target, &mode) == nullptr) {
		return false;
	}
	if (mode.w < width || mode.h < height) {
		return false;
	}
	if (mode.w > width && mode.h > height) {
		return false;
	}
	DEBUG("Resolution supported: %dx%d\n", mode.w, mode.h);
	return true;
}

// OutputSurface:

OutputSurface::OutputSurface(SDL_Texture *texture, SDL_Renderer *renderer, SDL_Window *window)
	: Surface(texture, renderer)
	, window(window)
{
		SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
}

OutputSurface::~OutputSurface() {
	if (texture) SDL_DestroyTexture(texture);
	if (renderer) SDL_DestroyRenderer(renderer);
	if (window) SDL_DestroyWindow(window);
}

unique_ptr<OutputSurface> OutputSurface::open(
		const char *caption, int width, int height, int bitsPerPixel)
{
	SDL_ShowCursor(SDL_DISABLE);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

	Uint32 flags = SDL_WINDOW_SHOWN;

#if !defined(G2X_BUILD_OPTION_WINDOWED_MODE)
	flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif

	SDL_Window *window = SDL_CreateWindow(caption,
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		0, 0, flags);

	if (!window) {
		return nullptr;
	}

	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) {
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	}

	if (!renderer) {
		SDL_DestroyWindow(window);
		return nullptr;
	}

	// Set the global renderer
	Surface::setGlobalRenderer(renderer);

	Uint32 format;
	SDL_QueryTexture(SDL_GetRenderTarget(renderer), &format, nullptr, nullptr, nullptr);
	SDL_Texture *texture = SDL_CreateTexture(
		renderer,
		format,
		SDL_TEXTUREACCESS_TARGET,
		width,
		height
	);

	if (!texture) {
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		return nullptr;
	}

	SDL_SetRenderTarget(renderer, texture);

	return unique_ptr<OutputSurface>(new OutputSurface(texture, renderer, window));
}

void OutputSurface::flip() {
	SDL_SetRenderTarget(renderer, nullptr);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, nullptr, nullptr);
	SDL_RenderPresent(renderer);
	SDL_SetRenderTarget(renderer, texture);
}
