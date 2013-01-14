/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __EmbedPromptService_h
#define __EmbedPromptService_h

#include "nsIPrompt.h"
#include "nsIPromptFactory.h"
#include "nsIAuthPrompt2.h"
#include "mozilla/RefPtr.h"
#include "nsIDOMWindow.h"
#include "nsCOMPtr.h"
#include "nsStringGlue.h"
#include "nsICancelable.h"
#include "nsIAuthInformation.h"
#include "nsDataHashtable.h"
#include <map>
#include <string>

namespace mozilla {
namespace embedlite {

class EmbedLiteViewThreadChild;

class EmbedLiteViewPromptResponse
{
    NS_INLINE_DECL_REFCOUNTING(EmbedLiteViewPromptResponse)
public:
    EmbedLiteViewPromptResponse(nsIDOMWindow* aWin);
    virtual ~EmbedLiteViewPromptResponse();
    nsIDOMWindow* mWin;
    bool confirm;
    bool checkvalue;
    nsString retVal;
    nsString username;
    nsString password;
};

class EmbedPromptService : public nsIPrompt
{
public:
    EmbedPromptService(EmbedLiteViewThreadChild* aView, nsIDOMWindow* aWin);
    virtual ~EmbedPromptService();

    NS_DECL_ISUPPORTS
    NS_DECL_NSIPROMPT
private:
    RefPtr<EmbedLiteViewThreadChild> mView;
    nsCOMPtr<nsIDOMWindow> mWin;
};

class EmbedAuthPromptService;
class EmbedAsyncAuthPrompt
{
public:
    EmbedAsyncAuthPrompt(nsICancelable* aCancelable, nsIChannel* aChannel,
                         nsIAuthInformation* aAuthInfo, uint32_t aLevel,
                         bool aInProgress, EmbedLiteViewThreadChild* aView)
        : mChannel(aChannel)
        , mAuthInfo(aAuthInfo)
        , mLevel(aLevel)
        , mInProgress(aInProgress)
        , mView(aView)
        , mService(NULL)
    {
        consumers.AppendElement(aCancelable);
    }

    virtual ~EmbedAsyncAuthPrompt() {}
    nsTArray<nsCOMPtr<nsICancelable>> consumers;
    nsIDOMWindow* mWin;
    nsCOMPtr<nsIChannel> mChannel;
    nsCOMPtr<nsIAuthInformation> mAuthInfo;
    uint32_t mLevel;
    bool mInProgress;
    EmbedLiteViewThreadChild* mView;
    nsCString mHashKey;
    nsCOMPtr<EmbedAuthPromptService> mService;
};

class EmbedAuthPromptService : public nsIAuthPrompt2
{
public:
    EmbedAuthPromptService(EmbedLiteViewThreadChild* aView, nsIDOMWindow* aWin);
    virtual ~EmbedAuthPromptService();

    NS_DECL_ISUPPORTS
    NS_DECL_NSIAUTHPROMPT2

    void DoResponseAsyncPrompt(EmbedAsyncAuthPrompt* aPrompt,
                               const bool& confirmed,
                               const nsCString& username,
                               const nsCString& password);

private:
    void DoAsyncPrompt();


    RefPtr<EmbedLiteViewThreadChild> mView;
    nsCOMPtr<nsIDOMWindow> mWin;
    std::map<std::string, EmbedAsyncAuthPrompt*> asyncPrompts;
    std::map<EmbedLiteViewThreadChild*, bool> asyncPromptInProgress;
};

class EmbedPromptFactory :  public nsIPromptFactory
{
public:
    EmbedPromptFactory();
    virtual ~EmbedPromptFactory();

    NS_DECL_ISUPPORTS
    NS_DECL_NSIPROMPTFACTORY
};

}}

#define EMBED_LITE_PROMPT_SERVICE_CID \
 {0x6781a3b0, 0x5cfa, 0x11e2, {0x8c, 0x9c, 0x33, 0x95, 0x8b, 0xdf, 0x7a, 0xb6}}

#endif /* __EmbedPromptService_h */
