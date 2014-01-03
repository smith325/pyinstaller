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
#ifndef DLLMAIN_H
#define DLLMAIN_H
#define DLLExport __declspec(dllimport)



DLLExport void startUp();
DLLExport void sayHi();


#endif