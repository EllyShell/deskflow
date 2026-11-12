/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardChunk.h"
#include "server/ClientProxy1_5.h"

#include <deque>
#include <string>

class Server;
class IEventQueue;
class EventQueueTimer;

//! Proxy for client implementing protocol version 1.6
class ClientProxy1_6 : public ClientProxy1_5
{
public:
  ClientProxy1_6(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ~ClientProxy1_6() override;

  void setClipboard(ClipboardID id, const IClipboard *clipboard) override;
  bool recvClipboard() override;

private:
  struct PendingClipboardSend
  {
    ClipboardID id;
    uint32_t sequence;
    std::string data;
    size_t offset = 0;
    bool started = false;
  };

  void scheduleClipboardChunk(double delay);
  void sendNextClipboardChunk();

  IEventQueue *m_events;
  EventQueueTimer *m_clipboardSendTimer = nullptr;
  std::deque<PendingClipboardSend> m_pendingClipboardSends;
  std::string m_clipboardDataCached;
  ClipboardChunkAssemblyState m_clipboardChunkState;
};
