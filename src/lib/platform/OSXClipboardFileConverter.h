/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/OSXClipboard.h"

//! Share a copied file with the other computers
/*!
macOS puts a file url on the pasteboard when the user copies a file in Finder.
The internal clipboard has no file format, so copies like that were silently
lost. This converter turns the referenced file into the internal file payload
(basename + content) and, on the receiving side, writes the payload to a
temporary file and publishes public.file-url for it.

Image files are skipped on purpose: the image converter already transfers those
as a bitmap, which is the behaviour applications expect for screenshots.
*/
class OSXClipboardFileConverter : public IOSXClipboardConverter
{
public:
  OSXClipboardFileConverter() = default;
  ~OSXClipboardFileConverter() override = default;

  IClipboard::Format getFormat() const override;
  CFStringRef getOSXFormat() const override;

  std::string fromIClipboard(const std::string &) const override;
  std::string toIClipboard(const std::string &) const override;
};
