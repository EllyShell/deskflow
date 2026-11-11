/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFileConverter.h"

#include "base/Log.h"
#include "common/ClipboardFile.h"

#include <QDir>
#include <QFileInfo>

#include <shellapi.h>
#include <shlobj.h>

#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr UINT kMaxPathLength = 32768;
}

MSWindowsClipboardFileConverter::MSWindowsClipboardFileConverter() : m_format(CF_HDROP)
{
}

IClipboard::Format MSWindowsClipboardFileConverter::getFormat() const
{
  return IClipboard::Format::File;
}

UINT MSWindowsClipboardFileConverter::getWin32Format() const
{
  return m_format;
}

HANDLE MSWindowsClipboardFileConverter::fromIClipboard(const std::string &payload) const
{
  QString path;
  if (!deskflow::files::writeFilePayload(payload, path)) {
    return nullptr;
  }

  const QString native = QDir::toNativeSeparators(path);
  const std::wstring wide = native.toStdWString();

  const size_t headerSize = sizeof(DROPFILES);
  const size_t dataSize = (wide.size() + 2) * sizeof(wchar_t);
  HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, headerSize + dataSize);
  if (handle == nullptr) {
    return nullptr;
  }

  auto *drop = static_cast<DROPFILES *>(GlobalLock(handle));
  if (drop == nullptr) {
    GlobalFree(handle);
    return nullptr;
  }

  std::memset(drop, 0, headerSize);
  drop->pFiles = static_cast<DWORD>(headerSize);
  drop->fWide = TRUE;

  auto *target = reinterpret_cast<wchar_t *>(reinterpret_cast<BYTE *>(drop) + headerSize);
  std::memcpy(target, wide.c_str(), wide.size() * sizeof(wchar_t));
  target[wide.size()] = L'\0';
  target[wide.size() + 1] = L'\0';

  GlobalUnlock(handle);
  return handle;
}

std::string MSWindowsClipboardFileConverter::toIClipboard(HANDLE data) const
{
  if (DragQueryFileW(static_cast<HDROP>(data), 0xFFFFFFFF, nullptr, 0) == 0) {
    return std::string();
  }

  std::vector<wchar_t> buffer(kMaxPathLength);
  const UINT length = DragQueryFileW(static_cast<HDROP>(data), 0, buffer.data(), kMaxPathLength);
  if (length == 0) {
    return std::string();
  }

  const QString path = QString::fromWCharArray(buffer.data(), static_cast<int>(length));
  if (deskflow::files::isImageFile(path)) {
    // the bitmap converter handles image files, do not send them twice
    return std::string();
  }

  std::string payload;
  if (!deskflow::files::readFilePayload(path, payload)) {
    return std::string();
  }

  LOG_DEBUG(
      "clipboard: read file %s into payload %zu bytes", qPrintable(QFileInfo(path).fileName()), payload.size()
  );
  return payload;
}
