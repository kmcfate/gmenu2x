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

#ifndef MESSAGEBOX_H
#define MESSAGEBOX_H

#include "inputmanager.h"

#include <SDL2/SDL.h>
#include <string>

class GMenu2X;

class MessageBox {
public:
	MessageBox(GMenu2X& gmenu2x, const std::string &text,
			const std::string &icon="");
	void setButton(InputManager::Button button, const std::string &label);
	int exec();

private:
	GMenu2X& gmenu2x;
	std::string text, icon;
	std::string buttons[InputManager::BUTTON_TYPE_SIZE];
	std::string buttonLabels[InputManager::BUTTON_TYPE_SIZE];
	SDL_Rect buttonPositions[InputManager::BUTTON_TYPE_SIZE];
};

#endif // MESSAGEBOX_H
