/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/ClipboardFile.h"

#include "base/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

namespace deskflow::files {

bool isImageFile(const QString &path)
{
  static const QStringList imageExtensions = {
      QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("tif"),
      QStringLiteral("tiff"), QStringLiteral("bmp"),  QStringLiteral("gif"),  QStringLiteral("webp"),
      QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("avif"), QStringLiteral("ico"),
  };
  return imageExtensions.contains(QFileInfo(path).suffix().toLower());
}

bool readFilePayload(const QString &path, std::string &payload)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    LOG_DEBUG("clipboard: dropped reason=unreadable-file %s", qPrintable(path));
    return false;
  }

  const qint64 size = file.size();
  if (size <= 0 || size > kMaxFileBytes) {
    LOG_INFO("clipboard: dropped reason=file-size file=%s bytes=%lld", qPrintable(QFileInfo(path).fileName()), size);
    return false;
  }

  const QByteArray name = QFileInfo(path).fileName().toUtf8();
  if (name.isEmpty() || static_cast<uint32_t>(name.size()) > kMaxNameLength) {
    return false;
  }

  QByteArray data;
  data.reserve(static_cast<int>(4 + name.size() + size));
  uchar lengthBytes[4];
  qToLittleEndian<quint32>(static_cast<quint32>(name.size()), lengthBytes);
  data.append(reinterpret_cast<const char *>(lengthBytes), 4);
  data.append(name);

  const QByteArray content = file.readAll();
  if (content.size() != size) {
    return false;
  }
  data.append(content);

  payload.assign(data.constData(), static_cast<size_t>(data.size()));
  return true;
}

bool writeFilePayload(const std::string &payload, QString &path)
{
  if (payload.size() < 4) {
    return false;
  }

  const auto *raw = reinterpret_cast<const uchar *>(payload.data());
  const quint32 nameLength = qFromLittleEndian<quint32>(raw);
  if (nameLength == 0 || nameLength > kMaxNameLength || payload.size() < 4 + nameLength) {
    LOG_DEBUG("clipboard: dropped reason=invalid-file-payload bytes=%zu", payload.size());
    return false;
  }

  // never trust the received name as a path, keep the basename only
  QString name = QFileInfo(QString::fromUtf8(payload.data() + 4, static_cast<int>(nameLength))).fileName();
  if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) {
    return false;
  }

  QDir directory(QDir::temp().filePath(QStringLiteral("DeskflowClipboard")));
  if (!directory.mkpath(QStringLiteral("."))) {
    return false;
  }

  // do not overwrite a file that a previous transfer already published
  path = directory.filePath(name);
  const QFileInfo existing(path);
  if (existing.exists()) {
    const QString base = existing.completeBaseName();
    const QString suffix = existing.suffix();
    for (int index = 1; index < 1000; ++index) {
      const QString candidate =
          suffix.isEmpty()
              ? QStringLiteral("%1-%2").arg(base).arg(index)
              : QStringLiteral("%1-%2.%3").arg(base).arg(index).arg(suffix);
      path = directory.filePath(candidate);
      if (!QFileInfo::exists(path)) {
        break;
      }
    }
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    LOG_DEBUG("clipboard: dropped reason=unwritable-file %s", qPrintable(path));
    return false;
  }

  const auto *content = payload.data() + 4 + nameLength;
  const auto contentSize = static_cast<qint64>(payload.size() - 4 - nameLength);
  if (file.write(content, contentSize) != contentSize) {
    file.close();
    QFile::remove(path);
    return false;
  }
  file.close();

  LOG_DEBUG("clipboard: stored received file %s (%lld bytes)", qPrintable(path), contentSize);
  return true;
}

} // namespace deskflow::files
