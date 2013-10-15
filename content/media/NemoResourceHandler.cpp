/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NemoResourceHandler.h"

#include <QtCore/QCoreApplication>
#define signals Q_SIGNALS
#define slots Q_SLOTS
#include <policy/audio-resource.h>
#include <policy/resource-set.h>
#include "nsThreadUtils.h"
using namespace ResourcePolicy;

namespace mozilla {

NemoResourceHandler* NemoResourceHandler::mGlobalHandler = nullptr;

void
NemoResourceHandler::AquireResources(void* aHolder)
{
    MOZ_ASSERT(NS_IsMainThread());
    if (mGlobalHandler == nullptr)
    {
        mGlobalHandler = new NemoResourceHandler();
    }
    mGlobalHandler->Aquire();
}

void
NemoResourceHandler::ReleaseResources(void* aHolder)
{
    MOZ_ASSERT(NS_IsMainThread());
    if (!mGlobalHandler) {
        return;
    }

    mGlobalHandler->Release();

    if (mGlobalHandler->CanDestroy())
    {
        delete mGlobalHandler;
        mGlobalHandler = nullptr;
    }
}

void
NemoResourceHandler::Aquire()
{
    mCounter++;
    if (mCounter > 0 && !mResourceSet)
    {
        ResourceSet* set = new ResourcePolicy::ResourceSet("player");
        ResourcePolicy::AudioResource *audioResource = new ResourcePolicy::AudioResource("player");
        audioResource->setProcessID(QCoreApplication::applicationPid());
        audioResource->setStreamTag("media.name", "*");
        set->addResourceObject(audioResource);
        set->addResource(ResourcePolicy::VideoPlaybackType);
        set->acquire();
        mResourceSet = set;
    }
}

void
NemoResourceHandler::Release()
{
    mCounter--;
    if (mCounter == 0 && mResourceSet)
    {
        delete static_cast<ResourceSet*>(mResourceSet);
        mResourceSet = nullptr;
    }
}

bool
NemoResourceHandler::CanDestroy()
{
    return mCounter <= 0;
}

NemoResourceHandler::NemoResourceHandler()
  : mResourceSet(nullptr)
  , mCounter(0)
{
    MOZ_ASSERT(mGlobalHandler == nullptr);
    mGlobalHandler = this;
}

NemoResourceHandler::~NemoResourceHandler()
{
    MOZ_ASSERT(mGlobalHandler != nullptr);
    mGlobalHandler = nullptr;
}

} // namespace mozilla

