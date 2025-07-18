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

#include "background.h"
#include "bottombar.h"
#include "brightnessmanager.h"
#include "buildopts.h"
#include "cpu.h"
#include "debug.h"
#include "filedialog.h"
#include "filelister.h"
#include "font_stack.h"
#include "font_spec.h"
#include "gmenu2x.h"
#include "helppopup.h"
#include "iconbutton.h"
#include "inputdialog.h"
#include "launcher.h"
#include "layout.h"
#include "linkapp.h"
#include "mediamonitor.h"
#include "menu.h"
#include "menusettingbool.h"
#include "menusettingdir.h"
#include "menusettingfile.h"
#include "menusettingimage.h"
#include "menusettingint.h"
#include "menusettingmultistring.h"
#include "menusettingrgba.h"
#include "menusettingstring.h"
#include "messagebox.h"
#include "powersaver.h"
#include "settingsdialog.h"
#include "textdialog.h"
#include "wallpaperdialog.h"
#include "utilities.h"

#include "compat-filesystem.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <system_error>

#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <signal.h>

#include <errno.h>

//for browsing the filesystem
#include <sys/stat.h>
#include <sys/types.h>

#define DEFAULT_FONT_PATH "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf"
#define DEFAULT_FONT_SIZE 12

#ifndef DEFAULT_FALLBACK_FONTS
#define DEFAULT_FALLBACK_FONTS ,{"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",13},{"/usr/share/fonts/truetype/droid/DroidSansFallback.ttf",13}
#endif

using namespace std;

static GMenu2X *app;
static string gmenu2x_home;

// Note: Keep this in sync with the enum!
static const char *colorNames[NUM_COLORS] = {
	"topBarBg",
	"bottomBarBg",
	"selectionBg",
	"messageBoxBg",
	"messageBoxBorder",
	"messageBoxSelection",
};

static const std::pair<unsigned int, unsigned int> supported_resolutions[] = {
	{ 1280, 720 },
	{ 800, 480 },
	{ 640, 480 },
	{ 480, 272 },
	{ 320, 240 },
	{ 240, 160 },
};

static enum color stringToColor(const string &name)
{
	for (unsigned int i = 0; i < NUM_COLORS; i++) {
		if (strcmp(colorNames[i], name.c_str()) == 0) {
			return (enum color)i;
		}
	}
	return (enum color)-1;
}

static const char *colorToString(enum color c)
{
	return colorNames[c];
}

static void quit_all(int err) {
	delete app;
	SDL_Quit();
	exit(err);
}

static void update_battery(int err)
{
	app->bottomBar->updateBattery();
	request_repaint();
}

const string GMenu2X::getHome()
{
	return gmenu2x_home;
}

static void set_handler(int signal, void (*handler)(int))
{
	struct sigaction sig;
	sigaction(signal, NULL, &sig);
	sig.sa_handler = handler;
	sig.sa_flags |= SA_RESTART;
	sigaction(signal, &sig, NULL);
}

int main(int /*argc*/, char * /*argv*/[]) {
	INFO("---- GMenu2X starting ----\n");

	set_handler(SIGINT, &quit_all);
	set_handler(SIGSEGV, &quit_all);
	set_handler(SIGTERM, &quit_all);
	set_handler(SIGUSR1, &update_battery);

	char *home = getenv("HOME");
	if (home == NULL) {
		ERROR("Unable to find gmenu2x home directory. The $HOME variable is not defined.\n");
		return 1;
	}

	gmenu2x_home = (string)home + "/.gmenu2x";

	std::error_code ec;
	if (!compat::filesystem::create_directory(gmenu2x_home, ec) && ec.value()) {
		ERROR("Unable to create gmenu2x home directory: %d\n", ec.value());
		return 1;
	}

	DEBUG("Home path: %s.\n", gmenu2x_home.c_str());

	GMenu2X::run();

	return EXIT_FAILURE;
}

void GMenu2X::run() {
	auto menu = new GMenu2X();
	app = menu;
	DEBUG("Starting main()\n");
	menu->mainLoop();

	app = nullptr;
	Launcher *toLaunch = menu->toLaunch.release();
	delete menu;

	SDL_Quit();
	unsetenv("SDL_FBCON_DONT_CLEAR");

	if (toLaunch) {
		toLaunch->exec();
		// If control gets here, execution failed. Since we already destructed
		// everything, the easiest solution is to exit and let the system
		// respawn the menu.
		delete toLaunch;
	}
}

GMenu2X::GMenu2X() : input(*this), sc(this)
{
	useSelectionPng = false;

	powerSaver = PowerSaver::getInstance();
	layout = std::make_unique<Layout>();
	top = layout->topItem();

	/* Do not clear the screen on exit.
	 * This may require an SDL patch available at
	 * https://github.com/mthuurne/opendingux-buildroot/blob
	 * 			/opendingux-2010.11/package/sdl/sdl-fbcon-clear-onexit.patch
	 */
	setenv("SDL_FBCON_DONT_CLEAR", "1", 0);

	if( SDL_Init(SDL_INIT_TIMER) < 0) {
		ERROR("Could not initialize SDL: %s\n", SDL_GetError());
		// TODO: We don't use exceptions, so don't put things that can fail
		//       in a constructor.
		exit(EXIT_FAILURE);
	}

	/* We enable video at a later stage, so that the menu elements are
	 * loaded before SDL inits the video; this is made so that we won't show
	 * a black screen for a couple of seconds. */
	if( SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
		ERROR("Could not initialize SDL: %s\n", SDL_GetError());
		// TODO: We don't use exceptions, so don't put things that can fail
		//       in a constructor.
		exit(EXIT_FAILURE);
	}

#if defined(G2X_BUILD_OPTION_SCREEN_WIDTH) && defined(G2X_BUILD_OPTION_SCREEN_HEIGHT)
	s = OutputSurface::open(G2X_BUILD_OPTION_SCREEN_WIDTH, G2X_BUILD_OPTION_SCREEN_HEIGHT, 0);
#else
	// find largest resolution available
	for (const auto res : supported_resolutions) {
		if (OutputSurface::resolutionSupported(res.first, res.second) &&
		    (s = OutputSurface::open("GMenu2X", res.first, res.second, 0)))
			break;
	}
#endif

	if (!s) {
		ERROR("Failed to create main window\n");
		exit(EXIT_FAILURE);
	};

	DEBUG("%ux%u main window created\n", width(), height());

	top->setSize(width(), height());
	top->setContainer(LAY_FLEX | LAY_COLUMN);

	//load config data
	readConfig();

	brightnessmanager = std::make_unique<BrightnessManager>(this);
	confInt["brightnessLevel"] = brightnessmanager->currentBrightness();

	bottomBarIconY = height() - 18;
	bottomBarTextY = height() - 10;

	bg = NULL;
	font = NULL;
	setSkin(confStr["skin"], !fileExists(confStr["wallpaper"]));

	if (!fileExists(confStr["wallpaper"])) {
		DEBUG("No wallpaper defined; we will take the default one.\n");
		confStr["wallpaper"] = getSystemSkinPath("Default")
				     + "/wallpapers/default.png";
	}

	auto topBar = std::make_shared<LayoutItem>();
	topBar->setSize(0, skinConfInt["topBarHeight"]);
	topBar->setBehave(LAY_HFILL);
	top->addChild(topBar);

	auto background = std::make_shared<Background>(*this);
	top->addChild(background);
	layers.insert(layers.begin(), background);

	bottomBar = std::make_shared<BottomBar>(*this);
	top->addChild(bottomBar);

	initBG();

	/* the menu may take a while to load, so we show the background here */
	for (auto layer : layers)
		layer->paint(*s);
	layout->run();
	layout->render(*s);
	s->flip();

	initMenu();

#ifdef ENABLE_INOTIFY
	monitor = new MediaMonitor(GMENU2X_CARD_ROOT, menu.get());
#endif

	if (!input.init(menu.get())) {
		exit(EXIT_FAILURE);
	}

	powerSaver->setScreenTimeout(confInt["backlightTimeout"]);
}

GMenu2X::~GMenu2X() {
	fflush(NULL);
	sc.clear();

#ifdef ENABLE_INOTIFY
	delete monitor;
#endif
}

void GMenu2X::enableManualIcon(bool enable)
{
	bottomBar->enableManualIcon(enable);
}

void GMenu2X::showCpuFreq(unsigned long mhz)
{
	bottomBar->showCpuFreq(mhz);
}

void GMenu2X::initBG() {
	bg.reset();
	bgmain.reset();

	// Load wallpaper.
	bg = OffscreenSurface::loadImage(*this, confStr["wallpaper"], width(), height());
	if (!bg) {
		bg = OffscreenSurface::emptySurface(*this, width(), height());
	}

	drawTopBar(*bg);
	drawBottomBar(*bg);

	bgmain.reset(new OffscreenSurface(*bg));

	bgmain->convertToDisplayFormat();
}

bool GMenu2X::initFont() {
	std::string path = skinConfStr["font"];
	if (path.empty())
		path = DEFAULT_FONT_PATH;
	else if (path.rfind("skin:", 0) == 0)
		path = sc.getSkinFilePath(path.substr(5));
	unsigned int size = skinConfInt["fontsize"];
	if (size == 0)
		size = DEFAULT_FONT_SIZE;
	if (font == nullptr) font = std::make_unique<FontStack>();
	return font->LoadFonts({FontSpec{std::move(path), size} DEFAULT_FALLBACK_FONTS });
}

void GMenu2X::initMenu() {
	//Menu structure handler
	menu.reset(new Menu(*this));

	// Add action links in the applications section.
	auto appIdx = menu->sectionNamed("applications");
	menu->addActionLink(appIdx, tr["Explorer"],
			bind(&GMenu2X::explorer, this),
			tr["Launch an application"],
			"skin:icons/explorer.png");

	// Add action links in the settings section.
	auto settingIdx = menu->sectionNamed("settings");
	menu->addActionLink(settingIdx, "GMenu2X",
			bind(&GMenu2X::showSettings, this),
			tr["Configure GMenu2X's options"],
			"skin:icons/configure.png");
	menu->addActionLink(settingIdx, tr["Skin"],
			bind(&GMenu2X::skinMenu, this),
			tr["Configure skin"],
			"skin:icons/skin.png");
	menu->addActionLink(settingIdx, tr["Wallpaper"],
			bind(&GMenu2X::changeWallpaper, this),
			tr["Change GMenu2X wallpaper"],
			"skin:icons/wallpaper.png");
	if (fileExists(getLogFile())) {
		menu->addActionLink(settingIdx, tr["Log Viewer"],
				bind(&GMenu2X::viewLog, this),
				tr["Displays last launched program's output"],
				"skin:icons/ebook.png");
	}
	menu->addActionLink(settingIdx, tr["About"],
			bind(&GMenu2X::about, this),
			tr["Info about GMenu2X"],
			"skin:icons/about.png");

	menu->skinUpdated();
	menu->orderLinks();

	menu->setSectionIndex(confInt["section"]);
	menu->setLinkIndex(confInt["link"]);

	layers.push_back(menu);
}

void GMenu2X::about() {
	string text(readFileAsString(GMENU2X_SYSTEM_DIR "/about.txt"));
	string build_date("Build date: " __DATE__);
	TextDialog td(*this, "GMenu2X", build_date, "icons/about.png", text);
	td.exec();
}

void GMenu2X::viewLog() {
	string text(readFileAsString(getLogFile()));

	TextDialog td(*this, tr["Log Viewer"],
			tr["Displays last launched program's output"],
			"icons/ebook.png", text);
	td.exec();

	MessageBox mb(*this, tr["Do you want to delete the log file?"],
			 "icons/ebook.png");
	mb.setButton(InputManager::ACCEPT, tr["Yes"]);
	mb.setButton(InputManager::CANCEL, tr["No"]);
	if (mb.exec() == InputManager::ACCEPT) {
		unlink(getLogFile().c_str());
		menu->deleteSelectedLink();
	}
}

void GMenu2X::readConfig() {
	string conffile = getHome() + "/gmenu2x.conf";
	readConfig(conffile);

	sanitizeConfig();

	conffile = GMENU2X_SYSTEM_DIR "/gmenu2x.conf";
	readConfig(conffile);

	if (confStr["skin"].empty() || sc.getSkinPath(confStr["skin"]).empty())
		confStr["skin"] = "Default";

	if (!confStr["lang"].empty())
		tr.setLang(confStr["lang"]);
}

void GMenu2X::sanitizeConfig() {
	if (!confStr["wallpaper"].empty() && !fileExists(confStr["wallpaper"]))
		confStr.erase("wallpaper");

	evalIntConf( confInt, "outputLogs", 0, 0,1 );
	evalIntConf( confInt, "trimExt", 0, 0,1);
	evalIntConf( confInt, "backlightTimeout", 15, 0,120 );
	evalIntConf( confInt, "buttonRepeatRate", 10, 0, 20 );
	evalIntConf( confInt, "videoBpp", 32, 16, 32 );

	if (confStr["tvoutEncoding"] != "PAL")
		confStr["tvoutEncoding"] = "NTSC";

	/* We don't want the paths to be saved in the config */
	confStr.erase("brightnessSysfs");
	confStr.erase("powerSupplySysfs");
	confStr.erase("batterySysfs");
}

void GMenu2X::readConfig(string conffile) {
	ifstream inf(conffile.c_str(), ios_base::in);

	if (!inf.is_open())
		return;

	string line;
	while (getline(inf, line, '\n')) {
		string::size_type pos = line.find("=");
		string name = trim(line.substr(0, pos));
		string value = trim(line.substr(pos + 1));

		if (value.length() > 1 && value.at(0) == '"' &&
		    value.at(value.length() - 1) == '"') {
			if (confStr.find(name) == confStr.end() ||
			    confStr[name] == "")
				confStr[name] = value.substr(1, value.length() - 2);
		}
		else {
			if (confInt.find(name) == confInt.end())
			confInt[name] = atoi(value.c_str());
		}
	}

	inf.close();
}

void GMenu2X::saveSelection() {
	if (confInt["saveSelection"] && (
			confInt["section"] != menu->selSectionIndex()
			|| confInt["link"] != menu->selLinkIndex()
	)) {
		writeConfig();
	}
}

void GMenu2X::writeConfig() {
	string conffile = getHome() + "/gmenu2x.conf";
	ofstream inf(conffile.c_str());
	if (inf.is_open()) {
		ConfStrHash::iterator endS = confStr.end();
		for(ConfStrHash::iterator curr = confStr.begin(); curr != endS; curr++)
			inf << curr->first << "=\"" << curr->second << "\"" << endl;

		ConfIntHash::iterator endI = confInt.end();
		for(ConfIntHash::iterator curr = confInt.begin(); curr != endI; curr++)
			inf << curr->first << "=" << curr->second << endl;

		inf.close();
	}
}

void GMenu2X::writeSkinConfig() {
	string skin_dir = getLocalSkinPath(confStr["skin"]);

	compat::filesystem::create_directories(skin_dir);
	string conffile = skin_dir + "/skin.conf";

	ofstream inf(conffile.c_str());
	if (inf.is_open()) {
		ConfStrHash::iterator endS = skinConfStr.end();
		for(ConfStrHash::iterator curr = skinConfStr.begin(); curr != endS; curr++)
			inf << curr->first << "=\"" << curr->second << "\"" << endl;

		ConfIntHash::iterator endI = skinConfInt.end();
		for(ConfIntHash::iterator curr = skinConfInt.begin(); curr != endI; curr++)
			inf << curr->first << "=" << curr->second << endl;

		int i;
		for (i = 0; i < NUM_COLORS; ++i) {
			inf << colorToString((enum color)i) << "=#"
			    << skinConfColors[i] << endl;
		}

		inf.close();
	}
}

void GMenu2X::readTmp() {
	lastSelectorElement = -1;
	ifstream inf("/tmp/gmenu2x.tmp", ios_base::in);
	if (inf.is_open()) {
		string line;
		string section = "";
		while (getline(inf, line, '\n')) {
			string::size_type pos = line.find("=");
			string name = trim(line.substr(0,pos));
			string value = trim(line.substr(pos+1));

			if (name=="section")
				menu->setSectionIndex(atoi(value.c_str()));
			else if (name=="link")
				menu->setLinkIndex(atoi(value.c_str()));
			else if (name=="selectorelem")
				lastSelectorElement = atoi(value.c_str());
			else if (name=="selectordir")
				lastSelectorDir = value;
		}
		inf.close();
	}
}

void GMenu2X::writeTmp(int selelem, const string &selectordir) {
	string conffile = "/tmp/gmenu2x.tmp";
	ofstream inf(conffile.c_str());
	if (inf.is_open()) {
		inf << "section=" << menu->selSectionIndex() << endl;
		inf << "link=" << menu->selLinkIndex() << endl;
		if (selelem>-1)
			inf << "selectorelem=" << selelem << endl;
		if (!selectordir.empty())
			inf << "selectordir=" << selectordir << endl;
		inf.close();
	}
}

void GMenu2X::mainLoop() {
	// Recover last session
	readTmp();
	if (lastSelectorElement > -1 && menu->selLinkApp() &&
				(!menu->selLinkApp()->getSelectorDir().empty()
				 || !lastSelectorDir.empty()))
		menu->selLinkApp()->selector(lastSelectorElement, lastSelectorDir);

	while (true) {
		// Remove dismissed layers from the stack.
		for (auto it = layers.begin(); it != layers.end(); ) {
			if ((*it)->getStatus() == Layer::Status::DISMISSED) {
				it = layers.erase(it);
			} else {
				++it;
			}
		}

		// Run animations.
		bool animating = false;
		for (auto layer : layers) {
			animating |= layer->runAnimations();
		}

		// Paint layers.
		for (auto layer : layers)
			layer->paint(*s);
		layout->run();
		layout->render(*s);
		s->flip();

		// Exit main loop once we have something to launch.
		if (toLaunch) {
			break;
		}

		// Handle other input events.
		InputManager::Button button;
		bool gotEvent;
		const bool wait = !animating;
		do {
			gotEvent = input.getButton(&button, wait);
		} while (wait && !gotEvent);
		if (gotEvent) {
			if (button == InputManager::QUIT) {
				break;
			}
			for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
				if ((*it)->handleButtonPress(button)) {
					break;
				}
			}
		}
	}
}

void GMenu2X::explorer() {
	FileDialog fd(*this, tr["Select an application"], "sh,bin,py,elf,");
	if (fd.exec()) {
		if (confInt["saveSelection"] && (confInt["section"]!=menu->selSectionIndex() || confInt["link"]!=menu->selLinkIndex()))
			writeConfig();

		string command = cmdclean(fd.getPath()+"/"+fd.getFile());
		chdir(fd.getPath().c_str());

		toLaunch.reset(new Launcher(
				vector<string> { "/bin/sh", "-c", command }));
	}
}

void GMenu2X::queueLaunch(
	unique_ptr<Launcher>&& launcher, shared_ptr<Layer> launchLayer
) {
	toLaunch = move(launcher);
	layers.push_back(launchLayer);
}

void GMenu2X::showHelpPopup() {
	layers.push_back(make_shared<HelpPopup>(*this));
}

void GMenu2X::showSettings() {
	FileLister fl_tr;
	fl_tr.setShowDirectories(false);
	fl_tr.browse(GMENU2X_SYSTEM_DIR "/translations");
	fl_tr.browse(getHome() + "/translations", false);

	vector<string> translations = fl_tr.getFiles();
	translations.insert(translations.begin(), "English");
	string lang = tr.lang();

	vector<string> encodings;
	encodings.push_back("NTSC");
	encodings.push_back("PAL");

	SettingsDialog sd(*this, input, tr["Settings"]);
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingMultiString(
			*this, tr["Language"],
			tr["Set the language used by GMenu2X"],
			&lang, &translations)));
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingBool(
			*this, tr["Save last selection"],
			tr["Save the last selected link and section on exit"],
			&confInt["saveSelection"])));
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingBool(
			*this, tr["Output logs"],
			tr["Logs the output of the links. Use the Log Viewer to read them."],
			&confInt["outputLogs"])));
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingBool(
			*this, tr["Trim extensions"],
			tr["Trim extensions in the file explorer."],
			&confInt["trimExt"])));
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingInt(
			*this, tr["Screen Timeout"],
			tr["Set screen's backlight timeout in seconds"],
			&confInt["backlightTimeout"], 0, 120)));
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingInt(
			*this, tr["Button repeat rate"],
			tr["Set button repetitions per second"],
			&confInt["buttonRepeatRate"], 0, 20)));
	if (brightnessmanager->available()) {
		sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingInt(
				*this, tr["Brightness level"],
				tr["Set the brightness level"],
				&confInt["brightnessLevel"],
				1, brightnessmanager->maxBrightness())));
	}

	if (sd.exec()) {
		powerSaver->setScreenTimeout(confInt["backlightTimeout"]);

		input.repeatRateChanged();
		if (brightnessmanager->available())
			brightnessmanager->setBrightness(confInt["brightnessLevel"]);

		if (lang == "English") lang = "";
		if (lang != tr.lang()) {
			tr.setLang(lang);
			confStr["lang"] = lang;
		}

		writeConfig();
	}
}

void GMenu2X::skinMenu() {
	FileLister fl_sk;
	fl_sk.setShowFiles(false);
	fl_sk.setShowUpdir(false);
	fl_sk.browse(getLocalSkinTopPath());
	fl_sk.browse(getSystemSkinTopPath(), false);

	string curSkin = confStr["skin"];

	SettingsDialog sd(*this, input, tr["Skin"]);
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingMultiString(
			*this, tr["Skin"],
			tr["Set the skin used by GMenu2X"],
			&confStr["skin"], &fl_sk.getDirectories())));
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingRGBA(
			*this, tr["Top Bar"],
			tr["Color of the top bar"],
			&skinConfColors[COLOR_TOP_BAR_BG])));
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingRGBA(
			*this, tr["Bottom Bar"],
			tr["Color of the bottom bar"],
			&skinConfColors[COLOR_BOTTOM_BAR_BG])));
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingRGBA(
			*this, tr["Selection"],
			tr["Color of the selection and other interface details"],
			&skinConfColors[COLOR_SELECTION_BG])));
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingRGBA(
			*this, tr["Message Box"],
			tr["Background color of the message box"],
			&skinConfColors[COLOR_MESSAGE_BOX_BG])));
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingRGBA(
			*this, tr["Message Box Border"],
			tr["Border color of the message box"],
			&skinConfColors[COLOR_MESSAGE_BOX_BORDER])));
	sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingRGBA(
			*this, tr["Message Box Selection"],
			tr["Color of the selection of the message box"],
			&skinConfColors[COLOR_MESSAGE_BOX_SELECTION])));

	if (sd.exec()) {
		if (curSkin != confStr["skin"]) {
			setSkin(confStr["skin"]);
			writeConfig();
		}
		writeSkinConfig();
		initBG();
	}
}

void GMenu2X::setSkin(const string &skin, bool setWallpaper) {
	confStr["skin"] = skin;

	//Clear previous skin settings
	skinConfStr.clear();
	skinConfInt.clear();

	DEBUG("GMenu2X: setting new skin %s.\n", skin.c_str());

	//clear collection and change the skin path
	sc.clear();
	sc.setSkin(skin);

	//reset colors to the default values
	skinConfColors[COLOR_TOP_BAR_BG] = RGBAColor(255, 255, 255, 130);
	skinConfColors[COLOR_BOTTOM_BAR_BG] = RGBAColor(255, 255, 255, 130);
	skinConfColors[COLOR_SELECTION_BG] = RGBAColor(255, 255, 255, 130);
	skinConfColors[COLOR_MESSAGE_BOX_BG] = RGBAColor(255, 255, 255);
	skinConfColors[COLOR_MESSAGE_BOX_BORDER] = RGBAColor(80, 80, 80);
	skinConfColors[COLOR_MESSAGE_BOX_SELECTION] = RGBAColor(160, 160, 160);

	/* Load skin settings from user directory if present,
	 * or from the system directory. */
	if (!readSkinConfig(getLocalSkinPath(skin) + "/skin.conf"))
		readSkinConfig(getSystemSkinPath(skin) + "/skin.conf");

	if (!skinConfInt["topBarBgUseColor"]) {
		std::shared_ptr<OffscreenSurface> bar = sc.skinRes("imgs/topbar.png", false);
		if (bar)
			skinConfInt["topBarHeight"] = bar->height();
	}

	if (!skinConfInt["bottomBarBgUseColor"]) {
		std::shared_ptr<OffscreenSurface> bar = sc.skinRes("imgs/bottombar.png", false);
		if (bar)
			skinConfInt["bottomBarHeight"] = bar->height();
	}

	if (setWallpaper && !skinConfStr["wallpaper"].empty()) {
		string fp = sc.getSkinFilePath("wallpapers/" + skinConfStr["wallpaper"]);
		if (!fp.empty())
			confStr["wallpaper"] = fp;
		else
			WARNING("Unable to find wallpaper defined on skin %s\n", skin.c_str());
	}

	const bool fontChanged = initFont();
	if (menu != nullptr) {
		menu->skinUpdated();
		if (fontChanged) menu->fontChanged();
	}

	//Selection png
	if (!skinConfInt["selectionBgUseColor"])
		useSelectionPng = !!sc.addSkinRes("imgs/selection.png", false);
}

bool GMenu2X::readSkinConfig(const string& conffile)
{
	ifstream skinconf(conffile.c_str(), ios_base::in);
	if (skinconf.is_open()) {
		string line;
		while (getline(skinconf, line, '\n')) {
			line = trim(line);
			DEBUG("skinconf: '%s'\n", line.c_str());
			string::size_type pos = line.find("=");
			string name = trim(line.substr(0,pos));
			string value = trim(line.substr(pos+1));

			if (value.length()>0) {
				if (value.length()>1 && value.at(0)=='"' && value.at(value.length()-1)=='"')
					skinConfStr[name] = value.substr(1,value.length()-2);
				else if (value.at(0) == '#')
					skinConfColors[stringToColor(name)] =
						RGBAColor::fromString(value.substr(1));
				else
					skinConfInt[name] = atoi(value.c_str());
			}
		}
		skinconf.close();
		return true;
	} else {
		return false;
	}
}

void GMenu2X::showManual() {
	menu->selLinkApp()->showManual();
}

void GMenu2X::showContextMenu() {
	layers.push_back(make_shared<ContextMenu>(*this, *menu));
}

void GMenu2X::changeWallpaper() {
	WallpaperDialog wp(*this);
	if (wp.exec() && confStr["wallpaper"] != wp.wallpaper) {
		confStr["wallpaper"] = wp.wallpaper;
		initBG();
		writeConfig();
	}
}

void GMenu2X::addLink() {
	FileDialog fd(*this, tr["Select an application"], "sh,bin,py,elf,");
	if (fd.exec())
		menu->addLink(fd.getPath(), fd.getFile());
}

void GMenu2X::editLink() {
	LinkApp *linkApp = menu->selLinkApp();
	if (!linkApp) return;

	string oldSection = menu->selSection();
	string newSection = oldSection;

	string linkTitle = linkApp->getTitle();
	string linkDescription = linkApp->getDescription();
	string linkIcon = linkApp->getIcon();
	string linkManual = linkApp->getManual();
	string linkSelFilter = linkApp->getSelectorFilter();
	string linkSelDir = linkApp->getSelectorDir();
	bool linkSelBrowser = linkApp->getSelectorBrowser();

	string diagTitle = tr.translate("Edit $1",linkTitle.c_str(),NULL);
	string diagIcon = linkApp->getIconPath();

	SettingsDialog sd(*this, input, diagTitle, diagIcon);
	if (!linkApp->isOpk()) {
		sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingString(
				*this, tr["Title"],
				tr["Link title"],
				&linkTitle, diagTitle, diagIcon)));
		sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingString(
				*this, tr["Description"],
				tr["Link description"],
				&linkDescription, diagTitle, diagIcon)));
		sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingMultiString(
				*this, tr["Section"],
				tr["The section this link belongs to"],
				&newSection, &menu->getSections())));
		sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingImage(
				*this, tr["Icon"],
				tr.translate("Select an icon for this link", linkTitle.c_str(), NULL),
				&linkIcon, "png")));
		sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingFile(
				*this, tr["Manual"],
				tr["Select a manual or README file"],
				&linkManual, "man.png,txt")));
	}
	if (!linkApp->isOpk() || !linkApp->getSelectorDir().empty()) {
		sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingDir(
				*this, tr["Selector Directory"],
				tr["Directory to scan for the selector"],
				&linkSelDir)));
		sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingBool(
				*this, tr["Selector Browser"],
				tr["Allow the selector to change directory"],
				&linkSelBrowser)));
	}
#ifdef ENABLE_CPUFREQ
	vector<string> cpufreqs = cpu.getFrequencies();
	string freq = cpu.freqStr(linkApp->clock());

	if (!cpufreqs.empty()) {
		sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingMultiString(
				*this, tr["Clock frequency"],
				tr["CPU clock frequency for this link"],
				&freq, &cpufreqs)));
	}
#endif
	if (!linkApp->isOpk()) {
		sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingString(
				*this, tr["Selector Filter"],
				tr["Selector filter (Separate values with a comma)"],
				&linkSelFilter, diagTitle, diagIcon)));
		sd.addSetting(unique_ptr<MenuSetting>(new MenuSettingBool(
				*this, tr["Display Console"],
				tr["Must be enabled for console-based applications"],
				&linkApp->consoleApp)));
	}

	if (sd.exec()) {
		linkApp->setTitle(linkTitle);
		linkApp->setDescription(linkDescription);
		linkApp->setIcon(linkIcon);
		linkApp->setManual(linkManual);
		linkApp->setSelectorFilter(linkSelFilter);
		linkApp->setSelectorDir(linkSelDir);
		linkApp->setSelectorBrowser(linkSelBrowser);
#ifdef ENABLE_CPUFREQ
		linkApp->setClock(cpu.freqFromStr(freq));
#endif
		linkApp->save();

		if (oldSection != newSection) {
			INFO("Changed section: '%s' -> '%s'\n",
					oldSection.c_str(), newSection.c_str());
			menu->moveSelectedLink(newSection);
		}
	}
}

void GMenu2X::deleteLink() {
	if (menu->selLinkApp()!=NULL) {
		MessageBox mb(*this, tr.translate("Deleting $1",menu->selLink()->getTitle().c_str(),NULL)+"\n"+tr["Are you sure?"], menu->selLink()->getIconPath());
		mb.setButton(InputManager::ACCEPT, tr["Yes"]);
		mb.setButton(InputManager::CANCEL, tr["No"]);
		if (mb.exec() == InputManager::ACCEPT)
			menu->deleteSelectedLink();
	}
}

void GMenu2X::addSection() {
	InputDialog id(*this, input, tr["Insert a name for the new section"]);
	if (id.exec()) {
		// Look up section; create if it doesn't exist yet.
		auto idx = menu->sectionNamed(id.getInput());
		// Switch to the new section.
		menu->setSectionIndex(idx);
	}
}

void GMenu2X::deleteSection()
{
	menu->deleteSelectedSection();
}

int GMenu2X::drawButton(Surface& surface, const string &btn,
			const string &text, int x, int y) {
	int w = 0;
	auto icon = sc["skin:imgs/buttons/" + btn + ".png"];
	if (icon) {
		if (y < 0) y = height() + y;
		w = icon->width();
		icon->blit(surface, x, y - 7);
		if (!text.empty()) {
			w += 3;
			w += font->write(surface, text, x + w, y,
					 Font::HAlignLeft, Font::VAlignMiddle);
			w += 6;
		}
	}
	return x + w;
}

int GMenu2X::drawButtonRight(Surface& surface, const string &btn,
			     const string &text, int x, int y) {
	int w = 0;
	auto icon = sc["skin:imgs/buttons/" + btn + ".png"];
	if (icon) {
		if (y < 0) y = height() + y;
		w = icon->width();
		icon->blit(surface, x - w, y - 7);
		if (!text.empty()) {
			w += 3;
			w += font->write(surface, text, x - w, y,
					 Font::HAlignRight, Font::VAlignMiddle);
			w += 6;
		}
	}
	return x - w;
}

void GMenu2X::drawScrollBar(uint32_t pageSize, uint32_t totalSize, uint32_t pagePos) {
	if (totalSize <= pageSize) {
		// Everything fits on one screen, no scroll bar needed.
		return;
	}

	unsigned int top, height;
	tie(top, height) = getContentArea();
	top += 1;
	height -= 2;

	s->rectangle(width() - 8, top, 7, height,
		     skinConfColors[COLOR_SELECTION_BG]);
	top += 2;
	height -= 4;

	const uint32_t barSize = std::max(height * pageSize / totalSize, 4u);
	const uint32_t barPos = (height - barSize) * pagePos / (totalSize - pageSize);

	s->box(width() - 6, top + barPos, 3, barSize,
	       skinConfColors[COLOR_SELECTION_BG]);
}

void GMenu2X::drawTopBar(Surface& surface) {
	std::shared_ptr<OffscreenSurface> bar;
	if (!skinConfInt["topBarBgUseColor"])
		bar = sc.skinRes("imgs/topbar.png", false);
	if (bar) {
		for (unsigned int x = 0; x < width(); x++)
			bar->blit(surface, x, 0);
	} else {
		const int h = skinConfInt["topBarHeight"];
		surface.box(0, 0, width(), h,
			    skinConfColors[COLOR_TOP_BAR_BG]);
	}
}

void GMenu2X::drawBottomBar(Surface& surface) {
	std::shared_ptr<OffscreenSurface> bar;
	if (!skinConfInt["bottomBarBgUseColor"])
		bar = sc.skinRes("imgs/bottombar.png", false);
	if (bar) {
		for (unsigned int x = 0; x < width(); x++)
			bar->blit(surface, x, height() - bar->height());
	} else {
		const int h = skinConfInt["bottomBarHeight"];
		surface.box(0, height() - h, width(), h,
		      skinConfColors[COLOR_BOTTOM_BAR_BG]);
	}
}
