/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 *	 Patrick C. Beard <beard@netscape.com>
 */

////////////////////////////////////////////////////////////////////////////////
// Console stubs that send all console output to the Mac OS X Console.app, or
// to the terminal, if Mozilla is launched from the command line.
////////////////////////////////////////////////////////////////////////////////


#include <console.h>
#include <size_t.h>

#include <CFURL.h>
#include <CFBundle.h>
#include <CFString.h>
#include <MacErrors.h>

static CFBundleRef getBundle(CFStringRef frameworkPath)
{
	CFBundleRef bundle = NULL;
    
	//	Make a CFURLRef from the CFString representation of the bundle's path.
	//	See the Core Foundation URL Services chapter for details.
	CFURLRef bundleURL = CFURLCreateWithFileSystemPath(NULL, frameworkPath, kCFURLPOSIXPathStyle, true);
	if (bundleURL != NULL) {
        bundle = CFBundleCreate(NULL, bundleURL);
        if (bundle != NULL)
        	CFBundleLoadExecutable(bundle);
        CFRelease(bundleURL);
	}
	
	return bundle;
}

static void* getSystemFunction(CFStringRef functionName)
{
  static CFBundleRef systemBundle = getBundle(CFSTR("/System/Library/Frameworks/System.framework"));
  if (systemBundle) return CFBundleGetFunctionPointerForName(systemBundle, functionName);
  return NULL;
}

// Useful Carbon-CFM debugging tool, printf that goes to the system console.

typedef int (*read_write_proc_ptr) (int fd, void* buffer, long count);
static read_write_proc_ptr _read = (read_write_proc_ptr) getSystemFunction(CFSTR("read"));
static read_write_proc_ptr _write = (read_write_proc_ptr) getSystemFunction(CFSTR("write"));

/*
 *	The following four functions provide the UI for the console package.
 *	Users wishing to replace SIOUX with their own console package need
 *	only provide the four functions below in a library.
 */

/*
 *	extern short InstallConsole(short fd);
 *
 *	Installs the Console package, this function will be called right
 *	before any read or write to one of the standard streams.
 *
 *	short fd:		The stream which we are reading/writing to/from.
 *	returns short:	0 no error occurred, anything else error.
 */

short InstallConsole(short /*fd*/)
{
	return 0;
}

/*
 *	extern void RemoveConsole(void);
 *
 *	Removes the console package.  It is called after all other streams
 *	are closed and exit functions (installed by either atexit or _atexit)
 *	have been called.  Since there is no way to recover from an error,
 *	this function doesn't need to return any.
 */

void RemoveConsole()
{
}

/*
 *	extern long WriteCharsToConsole(char *buffer, long n);
 *
 *	Writes a stream of output to the Console window.  This function is
 *	called by write.
 *
 *	char *buffer:	Pointer to the buffer to be written.
 *	long n:			The length of the buffer to be written.
 *	returns short:	Actual number of characters written to the stream,
 *					-1 if an error occurred.
 */

long WriteCharsToConsole(char *buffer, long n)
{
    if (_write) return _write(1, buffer, n);
	return 0;
}

/*
 *	extern long ReadCharsFromConsole(char *buffer, long n);
 *
 *	Reads from the Console into a buffer.  This function is called by
 *	read.
 *
 *	char *buffer:	Pointer to the buffer which will recieve the input.
 *	long n:			The maximum amount of characters to be read (size of
 *					buffer).
 *	returns short:	Actual number of characters read from the stream,
 *					-1 if an error occurred.
 */

long ReadCharsFromConsole(char *buffer, long n)
{
    if (_read) return _read(0, buffer, n);
	return -1;
}

extern char *__ttyname(long fildes)
{
	if (fildes >= 0 && fildes <= 2)
		return "console";
	return (0L);
}

int kbhit(void)
{
      return 0; 
}

int getch(void)
{
      return 0; 
}

void clrscr()
{
	/* Could send the appropriate VT100 sequence here... */
}

/*
 * Stuff needed to make NSStdLibCarbon link.
 */

#include <SIOUX.h>

tSIOUXSettings	SIOUXSettings;

short SIOUXHandleOneEvent(EventRecord *userevent)
{
    return false;
}


