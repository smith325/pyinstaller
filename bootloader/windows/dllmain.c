/*
 * ****************************************************************************
 * Copyright (c) 2013, PyInstaller Development Team.
 * Distributed under the terms of the GNU General Public License with exception
 * for distributing bootloader.
 *
 * The full license is in the file COPYING.txt, distributed with this software.
 * ****************************************************************************
 */


/*
 * Bootloader for a DLL 
 */


#include "dllmain.h"
#include <windows.h>
#include <olectl.h>  // callSimpleEntryPoint
#include <memory.h>
#include <stdio.h>  // FILE
#include <string.h>


/* 
 * Use Sean's Tool Box -- public domain -- http://nothings.org/stb.h. 
 */
#define STB_DEFINE 1
#define STB_NO_REGISTRY 1  // Disable registry functions.
#define STB_NO_STB_STRINGS 1  // Disable config read/write functions.


/* PyInstaller headers. */
#include "stb.h"
#include "pyi_global.h"
#include "pyi_archive.h"
#include "pyi_python.h"
#include "pyi_pythonlib.h"
#include "pyi_launch.h"  // callSimpleEntryPoint
#include "utils.h"  // CreateActContext, ReleaseActContext
#include "pyi_utils.h"


HINSTANCE gPythoncom = 0;
char here[PATH_MAX + 1];
HINSTANCE gInstance;
PyThreadState *thisthread = NULL;

int launch(ARCHIVE_STATUS *status, char const * archivePath, char  const * archiveName)
{
	PyObject *obHandle;
	int loadedNew = 0;
	char pathnm[PATH_MAX];

	char MEIPASS2[PATH_MAX];
	char cmd[PATH_MAX];
	char tmp[PATH_MAX];
	char *extractionpath = NULL;

    VS("START");
	strcpy(pathnm, archivePath);
	strcat(pathnm, archiveName);
    
    /* Open the archive */
    if (pyi_arch_setup(status,archivePath,archiveName))
        return -1;
	VS("Opened Archive");
	extractionpath = pyi_getenv("_MEIPASS2");

    VS("LOADER: _MEIPASS2 is %s\n", (extractionpath ? extractionpath : "NULL"));

	#ifdef WIN32
    /* On Windows use single-process for --onedir mode. */
    if (!extractionpath && !pyi_launch_need_to_extract_binaries(status)) {
        VS("LOADER: No need to extract files to run; setting extractionpath to homepath\n");
        extractionpath = archivePath;
        strcpy(MEIPASS2, archivePath);
        pyi_setenv("_MEIPASS2", MEIPASS2); //Bootstrap sets sys._MEIPASS, plugins rely on it
    }
	#endif

    /* Load Python DLL */
    if (pyi_pylib_attach(status, &loadedNew))
        return -1;

	if (loadedNew) {
		/* Start Python with silly command line */
		PI_PyEval_InitThreads();
		if (pyi_pylib_start_python(status, 1, (char**)&pathnm))
			return -1;
		VS("Started new Python");
		thisthread = PI_PyThreadState_Swap(NULL);
		PI_PyThreadState_Swap(thisthread);
	}
	else {
		VS("Attached to existing Python");

		/* start a mew interp */
		thisthread = PI_PyThreadState_Swap(NULL);
		PI_PyThreadState_Swap(thisthread);
		if (thisthread == NULL) {
			thisthread = PI_Py_NewInterpreter();
			VS("created thisthread");
		}
		else
			VS("grabbed thisthread");
		PI_PyRun_SimpleString("import sys;sys.argv=[]");
	}

	/* a signal to scripts */
	PI_PyRun_SimpleString("import sys;sys.frozen='dll'\n");

	strcpy(tmp,status->homepath);
	if (tmp[strlen(tmp)-1] == '\\'){
		tmp[strlen(tmp)-1] = 0;
	}
	sprintf(cmd,"sys._MEIPASS=r\"%s\"",tmp);
	PI_PyRun_SimpleString(cmd);
	VS("set sys.frozen");
	/* Create a 'frozendllhandle' as a counterpart to
	   sys.dllhandle (which is the Pythonxx.dll handle)
	*/
	obHandle = PI_Py_BuildValue("i", gInstance);
	PI_PySys_SetObject("frozendllhandle", obHandle);
	Py_XDECREF(obHandle);
    /* Import modules from archive - this is to bootstrap */
    if (pyi_pylib_import_modules(status))
        return -1;
	VS("Imported Modules");
    /* Install zlibs - now import hooks are in place */
    if (pyi_pylib_install_zlibs(status))
        return -1;
	VS("Installed Zlibs");
    /* Run scripts */
    if (pyi_pylib_run_scripts(status))
        return -1;
	VS("All scripts run");
    if (PI_PyErr_Occurred()) {
		// PI_PyErr_Print();
		//PI_PyErr_Clear();
		VS("Some error occurred");
    }
	VS("PGL released");
	// Abandon our thread state.
	PI_PyEval_ReleaseThread(thisthread);
    VS("OK.");
    return 0;
}
void startUp()
{
	ARCHIVE_STATUS *archive_status = NULL;
	char thisfile[PATH_MAX];
	char *p;
	int len;
	archive_status = (ARCHIVE_STATUS *) calloc(1,sizeof(ARCHIVE_STATUS));
    if (archive_status == NULL) {
        FATALERROR("Cannot allocate memory for ARCHIVE_STATUS\n");
        return -1;
    }
	
	if (!GetModuleFileNameA(gInstance, thisfile, PATH_MAX)) {
		FATALERROR("System error - unable to load!");
		return;
	}
	// fill in here (directory of thisfile)
	//GetModuleFileName returns an absolute path
	strcpy(here, thisfile);
	for (p=here+strlen(here); *p != '\\' && p >= here+2; --p);
	*++p = '\0';
	len = p - here;
	//VS(here);
	//VS(&thisfile[len]);
	launch(archive_status, here, &thisfile[len]);
	// Now Python is up and running (any scripts have run)
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	if ( dwReason == DLL_PROCESS_ATTACH) {
		VS("Attach from thread %x", GetCurrentThreadId());
		gInstance = hInstance;
	}
	else if ( dwReason == DLL_PROCESS_DETACH ) {
		VS("Process Detach");
		//if (gPythoncom)
		//	releasePythonCom();
		//pyi_pylib_finalize();
	}

	return TRUE; 
}

void sayHi(){
	PI_PyRun_SimpleString("print('hello world')");
	PI_PyRun_SimpleString("import tuf.client.updater");
}