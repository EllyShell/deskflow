/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboardFileConverter.h"

#include "base/Log.h"
#include "common/ClipboardFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <CoreFoundation/CoreFoundation.h>

IClipboard::Format OSXClipboardFileConverter::getFormat() const
{
  return IClipboard::Format::File;
}

CFStringRef OSXClipboardFileConverter::getOSXFormat() const
{
  return CFSTR("public.file-url");
}

std::string OSXClipboardFileConverter::toIClipboard(const std::string &urlData) const
{
  // the pasteboard stores the url as a utf-8 string
  const QUrl url = QUrl::fromEncoded(QByteArray(urlData.data(), static_cast<int>(urlData.size())));
  if (!url.isLocalFile()) {
    return std::string();
  }

  const QString path = url.toLocalFile();
  if (path.isEmpty()) {
    return std::string();
  }

  if (deskflow::files::isImageFile(path)) {
    // the image converter handles image files, do not send them twice
    return std::string();
  }

  std::string payload;
  if (!deskflow::files::readFilePayload(path, payload)) {
    return std::string();
  }

  LOG_DEBUG("clipboard: read file %s into payload %zu bytes", qPrintable(QFileInfo(path).fileName()), payload.size());
  return payload;
}

std::string OSXClipboardFileConverter::fromIClipboard(const std::string &payload) const
{
  QString path;
  if (!deskflow::files::writeFilePayload(payload, path)) {
    return std::string();
  }

  const QByteArray url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded).toUtf8();
  LOG_DEBUG("clipboard: wrote file payload to %s", qPrintable(path));
  return std::string(url.constData(), static_cast<size_t>(url.size()));
}
