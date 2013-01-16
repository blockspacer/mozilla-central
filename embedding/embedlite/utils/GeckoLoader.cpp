/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define LOG_COMPONENT "GeckoLoader"
#include "EmbedLog.h"

#include "GeckoLoader.h"
#include "DirProvider.h"

#include <stdio.h>
#include "nscore.h"

// getenv
#include <stdlib.h>
#include <iostream>

// Possibly only UNIX
#include <unistd.h>

// XRE_ Functions
#include "nsXULAppAPI.h"

#if defined(XP_WIN)
#include <windows.h>
#include <stdlib.h>
#elif defined(XP_UNIX)
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#ifdef XP_MACOSX
#include "MacQuirks.h"
#endif

#ifdef WIN32
//TODO: make this file fully X platform
#  include <windows.h>
#  undef MAX_PATH
#  define MAX_PATH _MAX_PATH
#else
#  include <unistd.h>
#  include <string.h>
#  ifndef PATH_MAX
#    define PATH_MAX 1024
#  endif
#  define MAX_PATH PATH_MAX
#endif

static DirProvider kDirectoryProvider;
static bool sInitialized = false;

bool
GeckoLoader::InitEmbedding(const char* aProfilePath)
{
    if (sInitialized) {
        LOGW("Already initialized embedding\n");
        return false;
    }
    sInitialized = true;
    nsresult rv;
    
    static const char* sleepBeforeGeckoInit = getenv("SLEEP_BEFORE_EMBEDDING");
    if (sleepBeforeGeckoInit) {
        LOGF("Sleep for: %ss\n", sleepBeforeGeckoInit);
        sleep(atoi(sleepBeforeGeckoInit));
        printf("Start XRE Init Embedding\n");
    }

    // get rid of the bogus TLS warnings
    NS_LogInit();

    // create nsIFile pointing to xpcomDir
    const char* greHome = getenv("XRE_LIBXPCOM_PATH");
    nsCOMPtr<nsIFile> xuldir;
    rv = XRE_GetBinaryPath(greHome, getter_AddRefs(xuldir));
    if (NS_FAILED(rv)) {
        LOGE("Unable to create nsIFile for xuldir: %s\n", greHome);
        return false;
    }

    // create nsIFile pointing to appdir
    char self[MAX_PATH];
#ifdef WIN32
    GetModuleFileNameA(GetModuleHandle(NULL), self, sizeof(self));
#else
    // TODO: works on linux, need solution for unices which do not support this
    ssize_t len;
    if ((len = readlink("/proc/self/exe", self, sizeof(self)-1)) != -1)
        self[len] = '\0';
#endif
    std::string selfPath(self);
    size_t lastslash_t = selfPath.find_last_of("/\\");
    if (lastslash_t == std::string::npos) {
        LOGE("Invalid module filename: %s", self);
        return false;
    }

    selfPath = selfPath.substr(0, lastslash_t);

    nsCOMPtr<nsIFile> appdir;
    rv = XRE_GetBinaryPath(selfPath.c_str(), getter_AddRefs(appdir));
    if (NS_FAILED(rv)) {
        LOGE("Unable to create nsIFile for appdir: %s", selfPath.c_str());
        return false;
    }
    printf("Loaded xulDir:%s, appDir:%s\n", greHome, selfPath.c_str());

    // setup profile dir
    if (aProfilePath) {
        nsCString pr(aProfilePath);
        if (!pr.IsEmpty()) {
            if (pr.First() != '/') {
                pr.Assign(getenv("HOME"));
                pr.AppendLiteral("/.mozilla/");
                pr.Append(aProfilePath);
            }
            LOGF("Creating profile in:%s\n", pr.get());
            rv = NS_NewNativeLocalFile(pr, PR_FALSE,
                                       getter_AddRefs(kDirectoryProvider.sProfileDir));
            if (NS_FAILED(rv)) {
                LOGE("NS_NewNativeLocalFile failed.");
                return false;
            }
        } else {
            // for now use a subdir under appdir
            nsCOMPtr<nsIFile> profFile;
            rv = appdir->Clone(getter_AddRefs(profFile));
            if (NS_FAILED(rv)) {
                LOGE("Unable to clone nsIFile.");
                return false;
            }

            kDirectoryProvider.sProfileDir = do_QueryInterface(profFile);
            kDirectoryProvider.sProfileDir->AppendNative(NS_LITERAL_CSTRING("mozembed"));
        }

        // create dir if needed
        bool dirExists = true;
        rv = kDirectoryProvider.sProfileDir->Exists(&dirExists);
        if (!dirExists) {
            kDirectoryProvider.sProfileDir->Create(nsIFile::DIRECTORY_TYPE, 0700);
        }

        // Lock profile directory
        if (kDirectoryProvider.sProfileDir && !kDirectoryProvider.sProfileLock) {
            rv = XRE_LockProfileDirectory(kDirectoryProvider.sProfileDir, &kDirectoryProvider.sProfileLock);
            if (NS_FAILED(rv)) {
                LOGE("Unable to lock profile directory.");
                return false;
            }
        }
    }

    nsCString greHomeCSTR(getenv("GRE_HOME"));
    rv = NS_NewNativeLocalFile(greHomeCSTR, PR_FALSE,
                               getter_AddRefs(kDirectoryProvider.sGREDir));

    // init embedding
    rv = XRE_InitEmbedding2(xuldir, appdir,
                            aProfilePath ? const_cast<DirProvider*>(&kDirectoryProvider) : nullptr);
    if (NS_FAILED(rv)) {
        LOGE("XRE_InitEmbedding2 failed.");
        return false;
    }

    if (aProfilePath) {
        // initialize profile:
        XRE_NotifyProfile();
    }

    NS_LogTerm();

    return true;
}

bool
GeckoLoader::TermEmbedding()
{
    if (!sInitialized) {
        LOGE("Not initialized embedding\n");
        return false;
    }
    sInitialized = false;

    // get rid of the bogus TLS warnings
    NS_LogInit();

    // make sure this is freed before shutting down xpcom
    NS_IF_RELEASE(kDirectoryProvider.sProfileLock);
    kDirectoryProvider.sProfileDir = 0;
    kDirectoryProvider.sGREDir = 0;

    XRE_TermEmbedding();

    NS_LogTerm();

    return true;
}
