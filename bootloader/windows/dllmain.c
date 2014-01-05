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


PyObject *configDict = NULL;
PyObject *ptype, *pvalue, *ptraceback;
int _fileLength;

int Py_TUF_configure(char* tuf_intrp_json, char* p_repo_dir, char* p_ssl_cert_dir) {
    PyObject *moduleName;
    PyObject *tufInterMod;

	/* import tuf module into the interpreter ~ import tuf.interposition */
	moduleName = PI_PyString_FromString( "tuf.interposition" );
	tufInterMod = PI_PyImport_Import( moduleName );
	if ( tufInterMod == NULL ) {
		PI_PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    //PyObject_Print( pvalue, stderr, Py_PRINT_RAW);
    printf("\n"); fflush(stderr);
		return 0;
	}
	Py_XDECREF( moduleName );
	
	/* python equivalent ~ tuf.interposition.configure( tuf_intrp_json, p_repo_dir, p_ssl_cert_dir ) */
	
	configDict = PI_PyObject_CallMethod( tufInterMod, (char *)"configure", "(sss)", 
									  tuf_intrp_json, p_repo_dir, p_ssl_cert_dir );
	if ( configDict == NULL ) {
		PI_PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    //PyObject_Print( pvalue, stderr, Py_PRINT_RAW);
    printf("\n"); fflush(stderr);
		return 0;
	}
	Py_XDECREF( tufInterMod );
	
	printf( "**Protected by TUF**\n" );
	return 1;
}

int Py_TUF_deconfigure(PyObject* tuf_config_obj) {
    // Init the python env
	PyObject *tufInterMod;
	PyObject *configFunction;

	//import TUF module
	tufInterMod = PI_PyImport_AddModule( "tuf.interposition" );
	if ( tufInterMod == NULL ) {
		PI_PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    PI_PyObject_Print( pvalue, stderr, Py_PRINT_RAW);
    printf("\n"); fflush(stderr);
		return 0;
	}
	
	//get the configure function from tuf.interposition
	configFunction = PI_PyObject_GetAttrString( tufInterMod, "deconfigure" );
	if ( configFunction == NULL ) {
		PI_PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    PI_PyObject_Print( pvalue, stderr, Py_PRINT_RAW);
    printf("\n"); fflush(stderr);
		return 0;
	}
	Py_XDECREF( tufInterMod );

	//calls the config function from the tuf.interposition module
	//returns a dictionary with the configurations	
	//we are currently storing this globally 	
	configDict = PI_PyObject_CallObject( configFunction, tuf_config_obj );

	//Py_XDECREF( arg0 );
	//Py_XDECREF( arg1 );
	//Py_XDECREF( arg2 );
	//Py_XDECREF( args );
	//Py_XDECREF( configFunction );

	if ( configDict == NULL ) {
		PI_PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    PI_PyObject_Print( pvalue, stderr, Py_PRINT_RAW);
    printf("\n"); fflush(stderr);
		return 0;
	}

	printf( "TUF deconfigured.\n" );
	return 1;
}
char* Py_TUF_urllib_urlretrieve(char* url) {
	char* fileLocation;
	PyObject *urllibMod;
	PyObject* http_resp;
	PyObject* data;

	/* Load the urllib_tuf module ~ from tuf.interposition import urllib_tuf */
	urllibMod = PI_PyImport_AddModule( "urllib_tuf" );
	if ( urllibMod == NULL ) {
		PI_PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    PI_PyObject_Print( pvalue, stderr, Py_PRINT_RAW);
    printf("\n"); fflush(stderr);
		return NULL;
	}
	
	/* call ~ http_resp = tuf.interposition.urlretrieve( url ) 
	   This returns a tuple so I decided to return the /location/filename */
	http_resp = PI_PyObject_CallMethod( urllibMod, (char *)"urlretrieve", "s", url );
	if ( http_resp == NULL ) {
		PI_PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    PI_PyObject_Print( pvalue, stderr, Py_PRINT_RAW);
    printf("\n"); fflush(stderr);
		return NULL;
	}	
	
	data = PI_PyTuple_GetItem( http_resp, 0 );
	if ( data == NULL ) {
		PI_PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    PI_PyObject_Print( pvalue, stderr, Py_PRINT_RAW);
    printf("\n"); fflush(stderr);
		return NULL;
	}
	fileLocation = PI_PyString_AsString( data );
	Py_XDECREF( data );
	
	return fileLocation;
}

char* Py_TUF_urllib_urlopen(char* url) {
    PyObject *urllibMod;
	PyObject *http_resp;
	PyObject *data;
	PyObject *args;
	char* resp;

	/* Load the urllib_tuf module ~ from tuf.interposition import urllib_tuf */
	urllibMod = PI_PyImport_AddModule( "urllib_tuf" );
	if ( urllibMod == NULL ) {
		PI_PyErr_Print();
		return NULL;
	}
	
	/* call ~ http_resp = tuf.interposition.urllib_tuf.urlopen( url ) */
	http_resp = PI_PyObject_CallMethod( urllibMod, (char *)"urlopen", "(s)", url );
	if ( http_resp == NULL ) {
		PI_PyErr_Print();
		return NULL;
	}
	
	/* call ~ data = http_resp.read() */
	data = PI_PyObject_CallMethod( http_resp, (char *)"read" , NULL );
	if ( data == NULL ) {
		PI_PyErr_Print();
		return NULL;
	}
	Py_XDECREF( http_resp );
	
    args = PI_PyTuple_New( 1 );
	PI_PyTuple_SetItem(args, 0, data);
    
	_fileLength = PI_PyString_Size( data );

	if ( !PI_PyArg_ParseTuple( args, "s#", &resp, &_fileLength ) ) {
		PI_PyErr_Print();
		return NULL;
	}
	Py_XDECREF( data );

    // Return the file
	return resp;
}