/****************************************************************************
 * Snes9x Nintendo Wii/Gamecube Port
 *
 * Tantric 2008-2023
 *
 * fileop.cpp
 *
 * File operations
 ***************************************************************************/

#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ogcsys.h>
#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>
#include <malloc.h>
#include <fat.h>
#include <sdcard/wiisd_io.h>
#include <sdcard/gcsd.h>
#include <ogc/usbstorage.h>
#include <di/di.h>
#include <ogc/dvd.h>
#include <iso9660.h>

#include "snes9xgx.h"
#include "fileop.h"
#include "networkop.h"
#include "gcunzip.h"
#include "menu.h"
#include "filebrowser.h"
#include "gui/gui.h"

#ifdef HW_RVL
	#include "mem2.h"
#endif

#define THREAD_SLEEP 100

unsigned char *savebuffer = NULL;
u8 *ext_font_ttf = NULL;
static mutex_t bufferLock = LWP_MUTEX_NULL;
FILE * file; // file pointer - the only one we should ever use!
bool unmountRequired[9] = { false, false, false, false, false, false, false, false, false };
bool isMounted[9] = { false, false, false, false, false, false, false, false, false };

#ifdef HW_RVL
	static DISC_INTERFACE* sd = &__io_wiisd;
	static DISC_INTERFACE* usb = &__io_usbstorage;
	static DISC_INTERFACE* dvd = &__io_wiidvd;
#else
	static DISC_INTERFACE* carda = &__io_gcsda;
	static DISC_INTERFACE* cardb = &__io_gcsdb;
	static DISC_INTERFACE* port2 = &__io_gcsd2;
	static DISC_INTERFACE* dvd = &__io_gcdvd;
	static DISC_INTERFACE* gcloader = &__io_gcode;
#endif

// folder parsing thread
static lwp_t parsethread = LWP_THREAD_NULL;
static DIR *dir = NULL;
static bool parseHalt = true;
static bool parseFilter = true;
static bool ParseDirEntries();
int selectLoadedFile = 0;

// device thread
static lwp_t devicethread = LWP_THREAD_NULL;
static bool deviceHalt = true;

/****************************************************************************
 * ResumeDeviceThread
 *
 * Signals the device thread to start, and resumes the thread.
 ***************************************************************************/
void
ResumeDeviceThread()
{
	deviceHalt = false;
	LWP_ResumeThread(devicethread);
}

/****************************************************************************
 * HaltGui
 *
 * Signals the device thread to stop.
 ***************************************************************************/
void
HaltDeviceThread()
{
#ifdef HW_RVL
	deviceHalt = true;

	// wait for thread to finish
	while(!LWP_ThreadIsSuspended(devicethread))
		usleep(THREAD_SLEEP);
#endif
}

/****************************************************************************
 * HaltParseThread
 *
 * Signals the parse thread to stop.
 ***************************************************************************/
void
HaltParseThread()
{
	parseHalt = true;

	while(!LWP_ThreadIsSuspended(parsethread))
		usleep(THREAD_SLEEP);
}


/****************************************************************************
 * devicecallback
 *
 * This checks our devices for changes (SD/USB/DVD removed)
 ***************************************************************************/
#ifdef HW_RVL
static int devsleep;

static void *
devicecallback (void *arg)
{
	while (1)
	{
		if(isMounted[DEVICE_SD])
		{
			if(!sd->isInserted(sd)) // check if the device was removed
			{
				unmountRequired[DEVICE_SD] = true;
				isMounted[DEVICE_SD] = false;
			}
		}

		if(isMounted[DEVICE_USB])
		{
			if(!usb->isInserted(usb)) // check if the device was removed
			{
				unmountRequired[DEVICE_USB] = true;
				isMounted[DEVICE_USB] = false;
			}
		}

		if(isMounted[DEVICE_DVD])
		{
			if(!dvd->isInserted(dvd)) // check if the device was removed
			{
				unmountRequired[DEVICE_DVD] = true;
				isMounted[DEVICE_DVD] = false;
			}
		}

		devsleep = 1000*1000; // 1 sec

		while(devsleep > 0)
		{
			if(deviceHalt)
				LWP_SuspendThread(devicethread);
			usleep(THREAD_SLEEP);
			devsleep -= THREAD_SLEEP;
		}
	}
	return NULL;
}
#endif

static void *
parsecallback (void *arg)
{
	while(1)
	{
		while(ParseDirEntries())
			usleep(THREAD_SLEEP);
		LWP_SuspendThread(parsethread);
	}
	return NULL;
}

/****************************************************************************
 * InitDeviceThread
 *
 * libOGC provides a nice wrapper for LWP access.
 * This function sets up a new local queue and attaches the thread to it.
 ***************************************************************************/
void
InitDeviceThread()
{
#ifdef HW_RVL
	LWP_CreateThread (&devicethread, devicecallback, NULL, NULL, 0, 40);
#endif
	LWP_CreateThread (&parsethread, parsecallback, NULL, NULL, 0, 80);
}

/****************************************************************************
 * UnmountAllFAT
 * Unmounts all FAT devices
 ***************************************************************************/
void UnmountAllFAT()
{
#ifdef HW_RVL
	fatUnmount("sd:");
	fatUnmount("usb:");
#else
	fatUnmount("port2:");
	fatUnmount("carda:");
	fatUnmount("cardb:");
	fatUnmount("gcloader:");
#endif
}

/****************************************************************************
 * MountFAT
 * Checks if the device needs to be (re)mounted
 * If so, unmounts the device
 * Attempts to mount the device specified
 * Sets libfat to use the device by default
 ***************************************************************************/

static bool MountFAT(int device, int silent)
{
	bool mounted = false;
	int retry = 1;
	char name[10], name2[10];
	DISC_INTERFACE* disc = NULL;

	switch(device)
	{
#ifdef HW_RVL
		case DEVICE_SD:
			sprintf(name, "sd");
			sprintf(name2, "sd:");
			disc = sd;
			break;
		case DEVICE_USB:
			sprintf(name, "usb");
			sprintf(name2, "usb:");
			disc = usb;
			break;
#else
		case DEVICE_SD_SLOTA:
			sprintf(name, "carda");
			sprintf(name2, "carda:");
			disc = carda;
			break;
		case DEVICE_SD_SLOTB:
			sprintf(name, "cardb");
			sprintf(name2, "cardb:");
			disc = cardb;
			break;
		case DEVICE_SD_PORT2:
			sprintf(name, "port2");
			sprintf(name2, "port2:");
			disc = port2;
			break;
		case DEVICE_SD_GCLOADER:
			sprintf(name, "gcloader");
			sprintf(name2, "gcloader:");
			disc = gcloader;
			break;
#endif
		default:
			return false; // unknown device
	}

	if(unmountRequired[device])
	{
		unmountRequired[device] = false;
		fatUnmount(name2);
		disc->shutdown(disc);
		isMounted[device] = false;
	}

	while(retry)
	{
		if(disc->startup(disc) && fatMountSimple(name, disc))
			mounted = true;

		if(mounted || silent)
			break;

#ifdef HW_RVL
		if(device == DEVICE_SD)
			retry = ErrorPromptRetry("SD card not found!");
		else
			retry = ErrorPromptRetry("USB drive not found!");
#else
		retry = ErrorPromptRetry("SD card not found!");
#endif
	}

	isMounted[device] = mounted;
	return mounted;
}

void MountAllFAT()
{
#ifdef HW_RVL
	MountFAT(DEVICE_SD, SILENT);
	MountFAT(DEVICE_USB, SILENT);
#endif
}

/****************************************************************************
 * MountDVD()
 *
 * Tests if a ISO9660 DVD is inserted and available, and mounts it
 ***************************************************************************/
bool MountDVD(bool silent)
{
	bool mounted = false;
	int retry = 1;

	if(unmountRequired[DEVICE_DVD])
	{
		unmountRequired[DEVICE_DVD] = false;
		ISO9660_Unmount("dvd:");
	}

	while(retry)
	{
		ShowAction("Loading DVD...");

#ifdef HW_DOL
		DVD_Mount();
		s32 dvdstatus = DVD_GetDriveStatus();

		if (dvdstatus == DVD_STATE_NO_DISK)
#else
		if(!dvd->isInserted(dvd))
#endif
		{
			if(silent)
				break;

			retry = ErrorPromptRetry("No disc inserted!");
		}
		else if(!ISO9660_Mount("dvd", dvd))
		{
			if(silent)
				break;
			
			retry = ErrorPromptRetry("Unrecognized DVD format.");
		}
		else
		{
			mounted = true;
			break;
		}
	}
	CancelAction();
	isMounted[DEVICE_DVD] = mounted;
	return mounted;
}

bool FindDevice(char * filepath, int * device)
{
	if(!filepath || filepath[0] == 0)
		return false;

	if(strncmp(filepath, "sd:", 3) == 0)
	{
		*device = DEVICE_SD;
		return true;
	}
	else if(strncmp(filepath, "usb:", 4) == 0)
	{
		*device = DEVICE_USB;
		return true;
	}
	else if(strncmp(filepath, "smb:", 4) == 0)
	{
		*device = DEVICE_SMB;
		return true;
	}
	else if(strncmp(filepath, "carda:", 6) == 0)
	{
		*device = DEVICE_SD_SLOTA;
		return true;
	}
	else if(strncmp(filepath, "cardb:", 6) == 0)
	{
		*device = DEVICE_SD_SLOTB;
		return true;
	}
	else if(strncmp(filepath, "port2:", 6) == 0)
	{
		*device = DEVICE_SD_PORT2;
		return true;
	}
	else if(strncmp(filepath, "dvd:", 4) == 0)
	{
		*device = DEVICE_DVD;
		return true;
	}
	else if(strncmp(filepath, "gcloader:", 9) == 0)
	{
		*device = DEVICE_SD_GCLOADER;
		return true;
	}
	return false;
}

char * StripDevice(char * path)
{
	if(path == NULL)
		return NULL;
	
	char * newpath = strchr(path,'/');
	
	if(newpath != NULL)
		newpath++;
	
	return newpath;
}

/****************************************************************************
 * ChangeInterface
 * Attempts to mount/configure the device specified
 ***************************************************************************/
bool ChangeInterface(int device, bool silent)
{
	if(isMounted[device])
		return true;

	bool mounted = false;

	switch(device)
	{
#ifdef HW_RVL
		case DEVICE_SD:
		case DEVICE_USB:
#else
		case DEVICE_SD_SLOTA:
		case DEVICE_SD_SLOTB:
		case DEVICE_SD_PORT2:
		case DEVICE_SD_GCLOADER:
#endif
			mounted = MountFAT(device, silent);
			break;
		case DEVICE_DVD:
			mounted = MountDVD(silent);
			break;
		case DEVICE_SMB:
			mounted = ConnectShare(silent);
			break;
	}

	return mounted;
}

bool ChangeInterface(char * filepath, bool silent)
{
	int device = -1;

	if(!FindDevice(filepath, &device))
		return false;

	return ChangeInterface(device, silent);
}

void CreateAppPath(int argc, char* argv[])
{
	snprintf(appPath, MAXPATHLEN - 1, "sd:/apps/%s", APPFOLDER);

	if (argc <= 0 || !argv[0] || argv[0][0] == 0)
		return;

	char* path = strdup(argv[0]); // make a copy so we don't mess up original

	if(!path)
		return;
	
	char * loc = strrchr(path,'/');
	if (loc != NULL)
		*loc = 0; // strip file name

	int pos = 0;

	// replace fat:/ with sd:/
	if(strncmp(path, "fat:/", 5) == 0 || strncmp(path, "sd1:/", 5) == 0)
	{
		pos++;
		path[1] = 's';
		path[2] = 'd';
	}
	if(ChangeInterface(&path[pos], SILENT))
		snprintf(appPath, MAXPATHLEN-1, "%s", &path[pos]);

	free(path);
}

static char *GetExt(char *file)
{
	if(!file)
		return NULL;

	char *ext = strrchr(file,'.');
	if(ext != NULL)
	{
		ext++;
		int extlen = strlen(ext);
		if(extlen > 5)
			return NULL;
	}
	return ext;
}

void FindAndSelectLastLoadedFile () 
{
	int indexFound = -1;
	
	for(int j=1; j < browser.numEntries; j++)
	{
		if(strcmp(browserList[j].filename, GCSettings.LastFileLoaded) == 0)
		{
			indexFound = j;
			break;
		}
	}

	// move to this file
	if(indexFound > 0)
	{
		if(indexFound >= FILE_PAGESIZE)
		{			
			int newIndex = (floor(indexFound/(float)FILE_PAGESIZE)) * FILE_PAGESIZE;

			if(newIndex + FILE_PAGESIZE > browser.numEntries)
				newIndex = browser.numEntries - FILE_PAGESIZE;

			if(newIndex < 0)
				newIndex = 0;

			browser.pageIndex = newIndex;
		}
		browser.selIndex = indexFound;
	}
	
	selectLoadedFile = 2; // selecting done
}

static bool ParseDirEntries()
{
	if(!dir)
		return false;

	char *ext;
	struct dirent *entry = NULL;
	int isdir;

	int i = 0;

	while(i < 20 && !parseHalt)
	{
		entry = readdir(dir);

		if(entry == NULL)
			break;

		if(entry->d_name[0] == '.' && entry->d_name[1] != '.')
			continue;

		if(strcmp(entry->d_name, "..") == 0)
		{
			isdir = 1;
		}
		else
		{
			if(entry->d_type==DT_DIR)
				isdir = 1;
			else
				isdir = 0;
			
			// don't show the file if it's not a valid ROM
			if(parseFilter && !isdir)
			{
				ext = GetExt(entry->d_name);
				
				if(ext == NULL)
					continue;

				if(	strcasecmp(ext, "bs") != 0 && strcasecmp(ext, "smc") != 0 &&
					strcasecmp(ext, "fig") != 0 && strcasecmp(ext, "sfc") != 0 &&
					strcasecmp(ext, "swc") != 0 && strcasecmp(ext, "zip") != 0 &&
					strcasecmp(ext, "7z") != 0)
					continue;
			}
		}

		if(!AddBrowserEntry())
		{
			parseHalt = true;
			break;
		}

		snprintf(browserList[browser.numEntries+i].filename, MAXJOLIET, "%s", entry->d_name);
		browserList[browser.numEntries+i].isdir = isdir; // flag this as a dir

		if(isdir)
		{
			if(strcmp(entry->d_name, "..") == 0)
				sprintf(browserList[browser.numEntries+i].displayname, "Up One Level");
			else
				snprintf(browserList[browser.numEntries+i].displayname, MAXJOLIET, "%s", browserList[browser.numEntries+i].filename);
			browserList[browser.numEntries+i].icon = ICON_FOLDER;
		}
		else
		{
			StripExt(browserList[browser.numEntries+i].displayname, browserList[browser.numEntries+i].filename); // hide file extension
		}
		i++;
	}

	if(!parseHalt)
	{
		// Sort the file list
		if(i >= 0)
			qsort(browserList, browser.numEntries+i, sizeof(BROWSERENTRY), FileSortCallback);
	
		browser.numEntries += i;
	}

	if(entry == NULL || parseHalt)
	{
		closedir(dir); // close directory
		dir = NULL;
		
		return false; // no more entries
	}
	return true; // more entries
}

/***************************************************************************
 * Browse subdirectories
 **************************************************************************/
int
ParseDirectory(bool waitParse, bool filter)
{
	int retry = 1;
	bool mounted = false;
	parseFilter = filter;
	
	ResetBrowser(); // reset browser
	
	// add trailing slash
	if(browser.dir[strlen(browser.dir)-1] != '/')
		strcat(browser.dir, "/");

	// open the directory
	while(dir == NULL && retry == 1)
	{
		mounted = ChangeInterface(browser.dir, NOTSILENT);

		if(mounted)
			dir = opendir(browser.dir);
		else
			return -1;

		if(dir == NULL)
			retry = ErrorPromptRetry("Error opening directory!");
	}

	// if we can't open the dir, try higher levels
	if (dir == NULL)
	{
		char * devEnd = strrchr(browser.dir, '/');

		while(!IsDeviceRoot(browser.dir))
		{
			devEnd[0] = 0; // strip slash
			devEnd = strrchr(browser.dir, '/');

			if(devEnd == NULL)
				break;

			devEnd[1] = 0; // strip remaining file listing
			dir = opendir(browser.dir);
			if (dir)
				break;
		}
	}
	
	if(dir == NULL)
		return -1;

	if(IsDeviceRoot(browser.dir))
	{
		AddBrowserEntry();
		sprintf(browserList[0].filename, "..");
		sprintf(browserList[0].displayname, "Up One Level");
		browserList[0].isdir = 1; // flag this as a dir
		browserList[0].icon = ICON_FOLDER;
		browser.numEntries++;
	}

	parseHalt = false;
	ParseDirEntries(); // index first 20 entries

	LWP_ResumeThread(parsethread); // index remaining entries

	if(waitParse) // wait for complete parsing
	{
		ShowAction("Loading...");

		while(!LWP_ThreadIsSuspended(parsethread))
			usleep(THREAD_SLEEP);

		CancelAction();
	}

	return browser.numEntries;
}

bool CreateDirectory(char * path)
{
	DIR *dir = opendir(path);
	if (!dir) {
		if(mkdir(path, 0777) != 0) {
			return false;
		}
	}
	else {
		closedir(dir);
	}
	return true;
}

/****************************************************************************
 * AllocSaveBuffer ()
 * Clear and allocate the savebuffer
 ***************************************************************************/
void
AllocSaveBuffer ()
{
	if(bufferLock == LWP_MUTEX_NULL)
		LWP_MutexInit(&bufferLock, false);

	if(bufferLock != LWP_MUTEX_NULL)
		LWP_MutexLock(bufferLock);
	memset (savebuffer, 0, SAVEBUFFERSIZE);
}

/****************************************************************************
 * FreeSaveBuffer ()
 * Free the savebuffer memory
 ***************************************************************************/
void
FreeSaveBuffer ()
{
	if(bufferLock != LWP_MUTEX_NULL)
		LWP_MutexUnlock(bufferLock);
}

/****************************************************************************
 * LoadSzFile
 * Loads the selected file # from the specified 7z into rbuffer
 * Returns file size
 ***************************************************************************/
size_t
LoadSzFile(char * filepath, unsigned char * rbuffer)
{
	size_t size = 0;

	// stop checking if devices were removed/inserted
	// since we're loading a file
	HaltDeviceThread();

	// halt parsing
	HaltParseThread();

	file = fopen (filepath, "rb");
	if (file)
	{
		size = SzExtractFile(browserList[browser.selIndex].filenum, rbuffer);
		fclose (file);
	}
	else
	{
		ErrorPrompt("Error opening file!");
	}

	// go back to checking if devices were inserted/removed
	ResumeDeviceThread();

	return size;
}

/****************************************************************************
 * LoadFile
 ***************************************************************************/
size_t
LoadFile (char * rbuffer, char *filepath, size_t length, size_t buffersize, bool silent)
{
	char zipbuffer[2048];
	size_t size = 0, offset = 0, readsize = 0;
	int retry = 1;
	int device;

	if(!FindDevice(filepath, &device))
		return 0;

	// stop checking if devices were removed/inserted
	// since we're loading a file
	HaltDeviceThread();

	// halt parsing
	HaltParseThread();

	// open the file
	while(retry)
	{
		if(!ChangeInterface(device, silent))
			break;

		file = fopen (filepath, "rb");

		if(!file)
		{
			if(silent)
				break;

			retry = ErrorPromptRetry("Error opening file!");
			continue;
		}

		if(length > 0 && length <= 2048) // do a partial read (eg: to check file header)
		{
			size = fread (rbuffer, 1, length, file);
		}
		else // load whole file
		{
			readsize = fread (zipbuffer, 1, 32, file);

			if(!readsize)
			{
				unmountRequired[device] = true;
				retry = ErrorPromptRetry("Error reading file!");
				fclose (file);
				continue;
			}

			if (IsZipFile (zipbuffer))
			{
				size = UnZipBuffer ((unsigned char *)rbuffer, buffersize); // unzip
			}
			else
			{
				fseeko(file,0,SEEK_END);
				size = ftello(file);
				fseeko(file,0,SEEK_SET);

				if(size > buffersize) {
					size = 0;
				}
				else {
					while(!feof(file))
					{
						ShowProgress ("Loading...", offset, size);
						readsize = fread (rbuffer + offset, 1, 4096, file); // read in next chunk

						if(readsize <= 0)
							break; // reading finished (or failed)

						offset += readsize;
					}
					size = offset;
					CancelAction();
				}
			}
		}
		retry = 0;
		fclose (file);
	}

	// go back to checking if devices were inserted/removed
	ResumeDeviceThread();
	CancelAction();
	return size;
}

size_t LoadFile(char * filepath, bool silent)
{
	return LoadFile((char *)savebuffer, filepath, 0, SAVEBUFFERSIZE, silent);
}

#ifdef HW_RVL
size_t LoadFont(char * filepath)
{
	FILE *file = fopen (filepath, "rb");

	if(!file) {
		ErrorPrompt("Font file not found!");
		return 0;
	}

	fseeko(file,0,SEEK_END);
	size_t loadSize = ftello(file);

	if(loadSize == 0) {
		ErrorPrompt("Error loading font!");
		return 0;
	}

	if(ext_font_ttf) {
		mem2_free(ext_font_ttf);
	}

	ext_font_ttf = (u8 *)mem2_malloc(loadSize);

	if(!ext_font_ttf) {
		ErrorPrompt("Font file is too large!");
		fclose(file);
		return 0;
	}

	fseeko(file,0,SEEK_SET);
	fread (ext_font_ttf, 1, loadSize, file);
	fclose(file);
	return loadSize;
}

void LoadBgMusic()
{
	char filepath[MAXPATHLEN];
	sprintf(filepath, "%s/bg_music.ogg", appPath);
	FILE *file = fopen (filepath, "rb");
	if(!file) {
		return;
	}

	fseeko(file,0,SEEK_END);
	size_t ogg_size = ftello(file);

	if(ogg_size == 0) {
		return;
	}

	u8 * ogg_data = (u8 *)mem2_malloc(ogg_size);

	if(!ogg_data) {
		return;
	}

	fseeko(file, 0, SEEK_SET);
	fread (ogg_data, 1, ogg_size, file);
	fclose(file);
	bg_music = ogg_data;
	bg_music_size = ogg_size;
}
#endif

/****************************************************************************
 * SaveFile
 * Write buffer to file
 ***************************************************************************/
size_t
SaveFile (char * buffer, char *filepath, size_t datasize, bool silent)
{
	size_t written = 0;
	size_t writesize, nextwrite;
	int retry = 1;
	int device;
		
	if(!FindDevice(filepath, &device))
		return 0;

	if(datasize == 0)
		return 0;

	// stop checking if devices were removed/inserted
	// since we're saving a file
	HaltDeviceThread();

	// halt parsing
	HaltParseThread();

	if(!silent)
		ShowAction("Saving...");

	while(!written && retry == 1)
	{
		if(!ChangeInterface(device, silent))
			break;

		file = fopen (filepath, "wb");

		if(!file)
		{
			if(silent)
				break;

			retry = ErrorPromptRetry("Error creating file!");
			continue;
		}

		while(written < datasize)
		{
			if(datasize - written > 4096) nextwrite=4096;
			else nextwrite = datasize-written;
			writesize = fwrite (buffer+written, 1, nextwrite, file);
			if(writesize != nextwrite) break; // write failure
			written += writesize;
		}
		fclose (file);

		if(written != datasize) written = 0;

		if(!written)
		{
			unmountRequired[device] = true;
			if(silent) break;
			retry = ErrorPromptRetry("Error saving file!");
		}
	}

	// go back to checking if devices were inserted/removed
	ResumeDeviceThread();
	if(!silent)
		CancelAction();
	return written;
}

size_t SaveFile(char * filepath, size_t datasize, bool silent)
{
	return SaveFile((char *)savebuffer, filepath, datasize, silent);
}
