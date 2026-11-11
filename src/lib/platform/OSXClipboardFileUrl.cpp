/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboardFileUrl.h"

#include <CoreFoundation/CoreFoundation.h>

#include <vector>

namespace deskflow::osx {

QString localPathFromFileUrl(const std::string &urlData)
{
  if (urlData.empty()) {
    return QString();
  }

  CFURLRef url = CFURLCreateWithBytes(
      kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(urlData.data()), urlData.size(), kCFStringEncodingUTF8,
      nullptr
  );
  if (url == nullptr) {
    return QString();
  }

  // resolves file reference urls (/ .file/id=...) into a real path url
  CFURLRef pathUrl = CFURLCreateFilePathURL(kCFAllocatorDefault, url, nullptr);
  CFRelease(url);
  if (pathUrl == nullptr) {
    return QString();
  }

  CFStringRef path = CFURLCopyFileSystemPath(pathUrl, kCFURLPOSIXPathStyle);
  CFRelease(pathUrl);
  if (path == nullptr) {
    return QString();
  }

  const CFIndex maxSize = CFStringGetMaximumSizeForEncoding(CFStringGetLength(path), kCFStringEncodingUTF8) + 1;
  std::vector<char> buffer(static_cast<size_t>(maxSize));
  QString result;
  if (CFStringGetCString(path, buffer.data(), maxSize, kCFStringEncodingUTF8)) {
    result = QString::fromUtf8(buffer.data());
  }
  CFRelease(path);
  return result;
}

} // namespace deskflow::osx