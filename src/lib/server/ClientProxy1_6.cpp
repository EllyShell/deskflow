/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_6.h"

#include "base/Log.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/StreamChunker.h"
#include "io/IStream.h"
#include "server/Server.h"

#include <QString>

#include <algorithm>
#include <memory>
#include <utility>

namespace {
constexpr size_t kPacedClipboardThreshold = 64 * 1024;
constexpr size_t kClipboardChunkSize = 16 * 1024;
constexpr uint32_t kMaxPendingOutputBytes = 64 * 1024;
constexpr double kInitialClipboardDelay = 0.03;
constexpr double kNextClipboardChunkDelay = 0.002;
constexpr double kOutputDrainRetryDelay = 0.005;
} // namespace

//
// ClientProxy1_6
//

ClientProxy1_6::ClientProxy1_6(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events)
    : ClientProxy1_5(name, stream, server, events),
      m_events(events)
{
  m_events->addHandler(EventTypes::ClipboardSending, this, [this](const auto &e) {
    ClipboardChunk::send(getStream(), e.getDataObject());
  });
}

ClientProxy1_6::~ClientProxy1_6()
{
  if (m_clipboardSendTimer != nullptr) {
    m_events->removeHandler(EventTypes::Timer, m_clipboardSendTimer);
    m_events->deleteTimer(m_clipboardSendTimer);
  }
  m_events->removeHandler(EventTypes::ClipboardSending, this);
}

void ClientProxy1_6::scheduleClipboardChunk(double delay)
{
  m_clipboardSendTimer = m_events->newOneShotTimer(delay, nullptr);
  m_events->addHandler(EventTypes::Timer, m_clipboardSendTimer, [this](const auto &) { sendNextClipboardChunk(); });
}

void ClientProxy1_6::sendNextClipboardChunk()
{
  auto *timer = std::exchange(m_clipboardSendTimer, nullptr);
  m_events->removeHandler(EventTypes::Timer, timer);
  m_events->deleteTimer(timer);

  if (m_pendingClipboardSends.empty()) {
    return;
  }
  if (getStream()->getPendingOutputSize() >= kMaxPendingOutputBytes) {
    scheduleClipboardChunk(kOutputDrainRetryDelay);
    return;
  }

  auto &transfer = m_pendingClipboardSends.front();
  if (!transfer.started) {
    const std::string size = QString::number(transfer.data.size()).toStdString();
    const std::unique_ptr<ClipboardChunk> chunk(ClipboardChunk::start(transfer.id, transfer.sequence, size));
    ClipboardChunk::send(getStream(), chunk.get());
    transfer.started = true;
    LOG_DEBUG("clipboard: started paced send id=%d bytes=%zu", transfer.id, transfer.data.size());
  } else {
    const size_t count = std::min(kClipboardChunkSize, transfer.data.size() - transfer.offset);
    const std::string data(transfer.data.data() + transfer.offset, count);
    const std::unique_ptr<ClipboardChunk> chunk(ClipboardChunk::data(transfer.id, transfer.sequence, data));
    ClipboardChunk::send(getStream(), chunk.get());
    transfer.offset += count;
    if (transfer.offset == transfer.data.size()) {
      const std::unique_ptr<ClipboardChunk> end(ClipboardChunk::end(transfer.id, transfer.sequence));
      ClipboardChunk::send(getStream(), end.get());
      LOG_DEBUG("clipboard: finished paced send id=%d bytes=%zu", transfer.id, transfer.data.size());
      m_pendingClipboardSends.pop_front();
    }
  }

  if (!m_pendingClipboardSends.empty()) {
    scheduleClipboardChunk(kNextClipboardChunkDelay);
  }
}

void ClientProxy1_6::setClipboard(ClipboardID id, const IClipboard *clipboard)
{
  // ignore if this clipboard is already clean
  if (m_clipboard[id].m_dirty) {
    // this clipboard is now clean
    m_clipboard[id].m_dirty = false;
    Clipboard::copy(&m_clipboard[id].m_clipboard, clipboard);

    std::string data = m_clipboard[id].m_clipboard.marshall();

    size_t size = data.size();
    LOG_DEBUG("sending clipboard %d to \"%s\"", id, getName().c_str());

    if (size < kPacedClipboardThreshold && m_pendingClipboardSends.empty()) {
      StreamChunker::sendClipboard(data, size, id, 0, m_events, this);
    } else {
      auto pending = std::find_if(m_pendingClipboardSends.begin(), m_pendingClipboardSends.end(), [id](const auto &send) {
        return send.id == id && !send.started;
      });
      if (pending != m_pendingClipboardSends.end()) {
        pending->data = std::move(data);
      } else {
        m_pendingClipboardSends.push_back({id, 0, std::move(data)});
      }
      if (m_clipboardSendTimer == nullptr) {
        scheduleClipboardChunk(kInitialClipboardDelay);
      }
    }
  }
}

bool ClientProxy1_6::recvClipboard()
{
  // parse message
  ClipboardID id;
  uint32_t seq;

  auto r = ClipboardChunk::assemble(
      getStream(), m_clipboardDataCached, id, seq, m_clipboardChunkState, m_server->getMaximumClipboardSizeBytes()
  );

  if (r == TransferState::Started) {
    size_t size = ClipboardChunk::getExpectedSize(m_clipboardChunkState);
    LOG_DEBUG("receiving clipboard %d size=%zu", id, size);
  } else if (r == TransferState::Finished) {
    LOG(
        (CLOG_DEBUG "received client \"%s\" clipboard %d seqnum=%d, size=%zu", getName().c_str(), id, seq,
         m_clipboardDataCached.size())
    );
    // save clipboard
    m_clipboard[id].m_clipboard.unmarshall(m_clipboardDataCached, 0);
    m_clipboard[id].m_sequenceNumber = seq;
    m_clipboardDataCached.clear();
    m_clipboardDataCached.shrink_to_fit();

    // notify
    auto *info = new ClipboardInfo;
    info->m_id = id;
    info->m_sequenceNumber = seq;
    m_events->addEvent(Event(EventTypes::ClipboardChanged, getEventTarget(), info));
  } else if (r == TransferState::Error) {
    return false;
  }

  return true;
}
