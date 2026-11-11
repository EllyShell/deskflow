/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

//! Convert clipboard images between the internal DIB and the Windows PNG format
/*!
Browsers, Electron based applications and the Windows screenshot tool publish
images as PNG (registered "PNG" format) and often never as CF_DIB, so a DIB-only
converter misses them. This converter decodes PNG into the canonical 32bpp
BI_RGB DIB used internally and encodes the internal DIB as PNG.
*/
class MSWindowsClipboardPNGConverter : public IMSWindowsClipboardConverter
{
public:
  MSWindowsClipboardPNGConverter();
  ~MSWindowsClipboardPNGConverter() override = default;

  // IMSWindowsClipboardConverter overrides
  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &) const override;
  std::string toIClipboard(HANDLE) const override;

private:
  UINT m_format;
};
