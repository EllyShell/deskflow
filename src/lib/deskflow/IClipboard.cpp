/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/IClipboard.h"

#include "base/Log.h"

#include <assert.h>
#include <utility>
#include <vector>

//
// IClipboard
//

void IClipboard::unmarshall(IClipboard *clipboard, const std::string_view &data, Time time)
{
  assert(clipboard != nullptr);

  const char *index = data.data();
  const char *const end = index + data.size();

  if (end - index < 4) {
    LOG_ERR("clipboard unmarshall: truncated header");
    return;
  }
  // read the number of formats
  const uint32_t numFormats = readUInt32(index);
  index += 4;

  // Parse the whole payload before touching the destination. Clearing first and
  // discovering afterwards that nothing usable arrived is what wiped the local
  // clipboard: a peer sent one zero length format, we took ownership, and the
  // user's data was gone.
  std::vector<std::pair<IClipboard::Format, std::string>> items;
  for (uint32_t i = 0; i < numFormats; ++i) {
    // need 8 bytes for format id + payload size
    if (end - index < 8) {
      LOG_ERR("clipboard unmarshall: truncated format header at %u/%u", i, numFormats);
      break;
    }
    // get the format id
    const uint32_t formatID = readUInt32(index);
    index += 4;

    // get the size of the format data
    const uint32_t size = readUInt32(index);
    index += 4;

    // peer-supplied size must not exceed remaining buffer
    if (size > static_cast<uint32_t>(end - index)) {
      LOG_ERR("clipboard unmarshall: payload size %u exceeds remaining %zd", size, end - index);
      break;
    }

    // save the data if it's a known format.  if either the client
    // or server supports more clipboard formats than the other
    // then one of them will get a format >= TotalFormats here.
    if (formatID >= static_cast<uint32_t>(IClipboard::Format::TotalFormats)) {
      LOG_DEBUG("clipboard unmarshall: ignoring unknown format %u", formatID);
    } else if (size == 0) {
      // an empty payload carries no data; accepting it would let a peer hand us
      // a format that owns the clipboard while holding nothing at all
      LOG_DEBUG("clipboard unmarshall: ignoring empty format %u", formatID);
    } else {
      items.emplace_back(static_cast<IClipboard::Format>(formatID), std::string(index, size));
    }
    index += size;
  }

  if (items.empty()) {
    LOG_DEBUG("clipboard unmarshall: no usable formats in %u, leaving clipboard untouched", numFormats);
    return;
  }

  if (clipboard->open(time)) {
    // clear existing data
    clipboard->empty();

    for (const auto &[format, payload] : items) {
      clipboard->add(format, payload);
    }

    // done
    LOG_DEBUG("clipboard: unmarshalled formats=%u kept=%zu bytes=%zu", numFormats, items.size(), data.size());
    clipboard->close();
  }
}

std::string IClipboard::marshall(const IClipboard *clipboard)
{
  // return data format:
  // 4 bytes => number of formats included
  // 4 bytes => format enum
  // 4 bytes => clipboard data size n
  // n bytes => clipboard data
  // back to the second 4 bytes if there is another format

  assert(clipboard != nullptr);

  std::string data;
  static const auto totalClipboardFormats = static_cast<int>(Format::TotalFormats);
  // Only formats that actually carry bytes are marshalled. A format that
  // reports has() but yields nothing would otherwise travel as "present but
  // empty" and make the peer clear its pasteboard for no data at all.
  std::vector<std::pair<uint32_t, std::string>> formats;
  // FIXME -- use current time
  if (clipboard->open(0)) {

    // compute size of marshalled data
    uint32_t size = 4;
    for (int32_t format = 0; format != totalClipboardFormats; ++format) {
      const auto eFormat = static_cast<IClipboard::Format>(format);
      if (!clipboard->has(eFormat)) {
        continue;
      }
      auto payload = clipboard->get(eFormat);
      if (payload.empty()) {
        LOG_DEBUG("clipboard: skipping empty format=%d", format);
        continue;
      }
      LOG_VERBOSE("clipboard: format=%d bytes=%zu", format, payload.size());
      size += 4 + 4 + static_cast<uint32_t>(payload.size());
      formats.emplace_back(static_cast<uint32_t>(format), std::move(payload));
    }

    // allocate space
    data.reserve(size);

    // marshall the data
    writeUInt32(&data, static_cast<uint32_t>(formats.size()));
    for (const auto &[format, payload] : formats) {
      writeUInt32(&data, format);
      writeUInt32(&data, static_cast<uint32_t>(payload.size()));
      data += payload;
    }
    clipboard->close();

    LOG_DEBUG("clipboard: marshalled formats=%u bytes=%u", static_cast<uint32_t>(formats.size()), size);
  }

  return data;
}

bool IClipboard::copy(IClipboard *dst, const IClipboard *src)
{
  assert(dst != nullptr);
  assert(src != nullptr);

  return copy(dst, src, src->getTime());
}

bool IClipboard::copy(IClipboard *dst, const IClipboard *src, Time time)
{
  assert(dst != nullptr);
  assert(src != nullptr);

  static const auto totalClipboardFormats = static_cast<int>(Format::TotalFormats);
  bool success = false;
  if (src->open(time)) {
    // Gather first, write second: emptying the destination before knowing that
    // there is anything to put back is how the local clipboard got lost.
    std::vector<std::pair<Format, std::string>> items;
    for (int32_t format = 0; format != totalClipboardFormats; ++format) {
      const auto eFormat = static_cast<IClipboard::Format>(format);
      if (!src->has(eFormat)) {
        continue;
      }
      auto payload = src->get(eFormat);
      if (payload.empty()) {
        LOG_DEBUG("clipboard: copy skipping empty format=%d", format);
        continue;
      }
      items.emplace_back(eFormat, std::move(payload));
    }

    if (items.empty()) {
      LOG_DEBUG("clipboard: copy skipped, source holds no data");
    } else if (dst->open(time)) {
      if (dst->empty()) {
        for (const auto &[format, payload] : items) {
          dst->add(format, payload);
        }
        success = true;
      }
      dst->close();
    }
    src->close();
  }

  return success;
}

uint32_t IClipboard::readUInt32(const char *buf)
{
  const auto *ubuf = reinterpret_cast<const unsigned char *>(buf);
  return (static_cast<uint32_t>(ubuf[0]) << 24) | (static_cast<uint32_t>(ubuf[1]) << 16) |
         (static_cast<uint32_t>(ubuf[2]) << 8) | static_cast<uint32_t>(ubuf[3]);
}

void IClipboard::writeUInt32(std::string *buf, uint32_t v)
{
  *buf += static_cast<uint8_t>((v >> 24) & 0xff);
  *buf += static_cast<uint8_t>((v >> 16) & 0xff);
  *buf += static_cast<uint8_t>((v >> 8) & 0xff);
  *buf += static_cast<uint8_t>(v & 0xff);
}
