/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/OSXClipboard.h"

//! Convert clipboard images between the internal DIB and a macOS image UTI
/*!
Most macOS applications publish images as public.tiff or public.png and never as
com.microsoft.bmp, so the BMP-only converter made images impossible to sync.
This converter decodes the given UTI into the canonical 32bpp BI_RGB DIB used
internally and encodes the internal DIB back to that UTI.
*/
class OSXClipboardImageConverter : public IOSXClipboardConverter
{
public:
  explicit OSXClipboardImageConverter(CFStringRef type);
  ~OSXClipboardImageConverter() override = default;

  IClipboard::Format getFormat() const override;
  CFStringRef getOSXFormat() const override;

  std::string fromIClipboard(const std::string &) const override;
  std::string toIClipboard(const std::string &) const override;

private:
  CFStringRef m_type;
};
