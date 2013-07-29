/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define LOG_COMPONENT "EmbedLiteUILoop"
#include "EmbedLog.h"

#include "EmbedLiteUILoop.h"
#include "EmbedLiteMessageLoop.h"

namespace mozilla {
namespace embedlite {

EmbedLiteUILoop::EmbedLiteUILoop()
  : MessageLoopForUI(MessageLoop::TYPE_UI)
{
  LOGT();
}

EmbedLiteUILoop::EmbedLiteUILoop(EmbedLiteMessageLoop* aCustomLoop)
  : MessageLoopForUI(aCustomLoop->GetPump())
{
  LOGT();
}

EmbedLiteUILoop::~EmbedLiteUILoop()
{
  LOGT();
}

void EmbedLiteUILoop::StartLoop()
{
  LOGT();
  // Run the UI event loop on the main thread.
  MessageLoop::Run();
  LOGF("Loop Stopped, exit");
}

void EmbedLiteUILoop::DoQuit()
{
  LOGT();
  DeletePendingTasks();
  Quit();
  DoIdleWork();
}

} // namespace embedlite
} // namespace mozilla

