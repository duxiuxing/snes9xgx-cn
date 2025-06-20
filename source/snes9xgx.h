/****************************************************************************
 * Snes9x Nintendo Wii/Gamecube Port
 *
 * softdev July 2006
 * crunchy2 May 2007-July 2007
 * Michniewski 2008
 * Tantric 2008-2023
 *
 * snes9xgx.h
 *
 * This file controls overall program flow. Most things start and end here!
 ***************************************************************************/

#ifndef _SNES9XGX_H_
#define _SNES9XGX_H_

#include "utils/FreeTypeGX.h"
#include "snes9x.h"
#include "filter.h"
#include "filelist.h"

#define APPNAME 			"Snes9x GX"
#define APPVERSION 			"4.5.6"
#define APPFOLDER 			"snes9xgx"
#define PREF_FILE_NAME		"settings.xml"

#define MAXPATHLEN 1024
#define NOTSILENT 0
#define SILENT 1

const char pathPrefix[10][11] =
{ "", "sd:/", "usb:/", "dvd:/", "smb:/", "carda:/", "cardb:/", "port2:/", "gcloader:/" };

enum {
	DEVICE_AUTO,
	DEVICE_SD,
	DEVICE_USB,
	DEVICE_DVD,
	DEVICE_SMB,
	DEVICE_SD_SLOTA,
	DEVICE_SD_SLOTB,
	DEVICE_SD_PORT2,
	DEVICE_SD_GCLOADER,
};

enum {
	FILE_SRAM,
	FILE_SNAPSHOT,
	FILE_ROM,
	FILE_CHEAT
};

enum
{
	CTRL_PAD,
	CTRL_SCOPE,
	CTRL_JUST,
	CTRL_MOUSE,
	CTRL_MOUSE_PORT2,
	CTRL_MOUSE_BOTH_PORTS,
	CTRL_PAD2,
	CTRL_PAD4,
	CTRL_LENGTH
};

const char ctrlName[8][24] =
{ 
	"SNES Controller", 
	"Super Scope", 
	"Justifier", 
	"SNES Mouse (Port 1)", 
	"SNES Mouse (Port 2)", 
	"SNES Mouse (Both Ports)",
	"SNES Controllers (2)", 
	"SNES Controllers (4)"
};

enum {
	TURBO_BUTTON_RSTICK = 0,
	TURBO_BUTTON_A,
	TURBO_BUTTON_B,
	TURBO_BUTTON_X,
	TURBO_BUTTON_Y,
	TURBO_BUTTON_L,
	TURBO_BUTTON_R,
	TURBO_BUTTON_ZL,
	TURBO_BUTTON_ZR,
	TURBO_BUTTON_Z,
	TURBO_BUTTON_C,
	TURBO_BUTTON_1,
	TURBO_BUTTON_2,
	TURBO_BUTTON_PLUS,
	TURBO_BUTTON_MINUS,
};

enum {
	LANG_JAPANESE = 0,
	LANG_ENGLISH,
	LANG_GERMAN,
	LANG_FRENCH,
	LANG_SPANISH,
	LANG_ITALIAN,
	LANG_DUTCH,
	LANG_SIMP_CHINESE,
	LANG_TRAD_CHINESE,
	LANG_KOREAN,
	LANG_PORTUGUESE,
	LANG_BRAZILIAN_PORTUGUESE,
	LANG_CATALAN,
	LANG_TURKISH,
	LANG_SWEDISH,
	LANG_LENGTH,
	LANG_DEFAULT = LANG_SIMP_CHINESE
};

struct SGCSettings{
	int		AutoLoad;
	int		AutoSave;
	int		LoadMethod; // For ROMS: Auto, SD, DVD, USB, Network (SMB)
	int		SaveMethod; // For SRAM, Freeze, Prefs: Auto, SD, USB, SMB
	int		AppendAuto; // 0 - no, 1 - yes
	char	LoadFolder[MAXPATHLEN]; 	// Path to game files
	char	LastFileLoaded[MAXPATHLEN]; // Last file loaded filename
	char	SaveFolder[MAXPATHLEN]; 	// Path to save files
	char	CheatFolder[MAXPATHLEN]; 	// Path to cheat files
	char	ScreenshotsFolder[MAXPATHLEN]; // Path to screenshots files
	char	CoverFolder[MAXPATHLEN]; 	// Path to cover files
	char	ArtworkFolder[MAXPATHLEN]; 	// Path to artwork files
	int		HideSRAMSaving;
	int		AutoloadGame;

	char	smbip[80];
	char	smbuser[20];
	char	smbpwd[20];
	char	smbshare[20];

	float	zoomHor; // horizontal zoom amount
	float	zoomVert; // vertical zoom amount
	int		videomode; // 0 - Automatic, 1 - NTSC (480i), 2 - Progressive (480p), 3 - PAL (50Hz), 4 - PAL (60Hz), 5 - Progressive (576p)
	int		render;		// 0 - original, 1 - filtered, 2 - unfiltered
	int		FilterMethod; // convert to RenderFilter
	int		Controller;
	int		HiResolution;
	int		SpriteLimit;
	int		FrameSkip;
	int		crosshair;
	int		widescreen;	// 0 - 4:3 aspect, 1 - 16:9 aspect
	int		xshift;	// video output shift
	int		yshift;
	int		WiimoteOrientation;
	int		ExitAction;
	int		MusicVolume;
	int		SFXVolume;
	int		Rumble;

#ifdef MULTI_LANGUAGE_SUPPORT
private:
	int 	language;
public:
	int Language() { return language; }
	void SetLanguage(int value)
	{
		if (value < 0 || value >= LANG_LENGTH)
			value = LANG_DEFAULT;
		language = value;

	}
#else
	int Language() { return LANG_DEFAULT; }
	void SetLanguage(int value) {}
#endif

	int		PreviewImage;

	int		sfxOverclock;
	
	int		Interpolation;
	int		MuteAudio;

	int		TurboModeEnabled; // 0 - disabled, 1 - enabled
	int		TurboModeButton;
	int		GamepadMenuToggle;
	int		MapABXYRightStick;
};

void ExitApp();
void ShutdownWii();
bool SupportedIOS(u32 ios);
bool SaneIOS(u32 ios);
extern struct SGCSettings GCSettings;
extern int ScreenshotRequested;
extern int ConfigRequested;
extern int ShutdownRequested;
extern int ExitRequested;
extern char appPath[];
extern FreeTypeGX *fontSystem[];
extern bool isWiiVC;
static inline bool IsWiiU(void)
{
	return ((*(vu16*)0xCD8005A0 == 0xCAFE) || isWiiVC);
}
static inline bool IsWiiUFastCPU(void)
{
	return ((*(vu16*)0xCD8005A0 == 0xCAFE) && ((*(vu32*)0xCD8005B0 & 0x20) == 0));
}
#endif
