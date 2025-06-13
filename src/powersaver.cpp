#include "powersaver.h"
#include "debug.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

std::shared_ptr<PowerSaver> PowerSaver::instance;

Uint32 screenTimerCallback(Uint32 timeout, void *d) {
	unsigned int * old_ticks = (unsigned int *) d;
	unsigned int new_ticks = SDL_GetTicks();

	if (new_ticks > *old_ticks + timeout + 1000) {
		DEBUG("Suspend occured, restarting timer\n");
		*old_ticks = new_ticks;
		return timeout;
	}

	DEBUG("Disable Backlight Event\n");
	PowerSaver::instance->disableScreen();
	return 0;
}

std::shared_ptr<PowerSaver> PowerSaver::getInstance()
{
	if (!instance)
		instance = std::shared_ptr<PowerSaver>(new PowerSaver());

	return instance;
}

PowerSaver::PowerSaver()
	: screenState(false)
	, screenTimeout(0)
	, screenTimer(0)
{
	enableScreen();
}

PowerSaver::~PowerSaver() {
	removeScreenTimer();
	enableScreen();
}

void PowerSaver::setScreenTimeout(unsigned int seconds) {
	screenTimeout = seconds;
	resetScreenTimer();
}

void PowerSaver::resetScreenTimer() {
	removeScreenTimer();
	enableScreen();
	if (screenTimeout != 0) {
		addScreenTimer();
	}
}

void PowerSaver::addScreenTimer() {
	assert(!screenTimer);
	timeout_startms = SDL_GetTicks();
	screenTimer = SDL_AddTimer(
			screenTimeout * 1000, screenTimerCallback, &timeout_startms);
	if (!screenTimer) {
		ERROR("Could not initialize SDLTimer: %s\n", SDL_GetError());
	}
}

void PowerSaver::removeScreenTimer() {
	if (screenTimer) {
		SDL_RemoveTimer(screenTimer);
		screenTimer = 0;
	}
}

#define SCREEN_BLANK_PATH "/sys/class/graphics/fb0/blank"
void PowerSaver::setScreenBlanking(bool state) {
	const char *path = SCREEN_BLANK_PATH;
	const char *blank = state
		? "0" /* FB_BLANK_UNBLANK */
		: "4" /* FB_BLANK_POWERDOWN */;

	int fd = open(path, O_RDWR);
	if (fd == -1) {
		WARNING("Failed to open '%s': %s\n", path, strerror(errno));
	} else {
		ssize_t written = write(fd, blank, strlen(blank));
		if (written == -1) {
			WARNING("Error writing '%s': %s\n", path, strerror(errno));
		}
		close(fd);
	}
	screenState = state;
}

void PowerSaver::enableScreen() {
	if (!screenState) {
		setScreenBlanking(true);
	}
}

void PowerSaver::disableScreen() {
	if (screenState) {
		setScreenBlanking(false);
	}
}
