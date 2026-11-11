/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/OSXClipboard.h"

//! Treat a copied image file as clipboard image data
/*!
Screenshot utilities often put a file URL (public.file-url) on the pasteboard
instead of image data, and some only keep the image flavors available for a
moment. Without this converter those copies cannot be shared at all, because
the internal clipboard only understands text, html and bitmap. The converter
loads the referenced file when it decodes to an image and produces the
canonical 32bpp BI_RGB DIB used internally. Writing is a no-op: the internal
clipboard never produces a file URL.
*/
class OSXClipboardFileImageConverter : public IOSXClipboardConverter
{
public:
  OSXClipboardFileImageConverter() = default;
  ~OSXClipboardFileImageConverter() override = default;

  IClipboard::Format getFormat() const override;
  CFStringRef getOSXFormat() const override;

  std::string fromIClipboard(const std::string &) const override;
  std::string toIClipboard(const std::string &) const override;
};
