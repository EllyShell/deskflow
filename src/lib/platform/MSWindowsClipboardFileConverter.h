/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

//! Share a copied file with the other computers
/*!
Explorer publishes copied files as CF_HDROP. The internal clipboard had no file
format, so copying a file between computers did nothing. This converter turns
the first path in the drop list into the internal file payload (basename +
content) and, on the receiving side, writes the payload to a temporary file and
publishes a CF_HDROP for it.

Image files are skipped on purpose: the bitmap converter already transfers those.
*/
class MSWindowsClipboardFileConverter : public IMSWindowsClipboardConverter
{
public:
  MSWindowsClipboardFileConverter();
  ~MSWindowsClipboardFileConverter() override = default;

  // IMSWindowsClipboardConverter overrides
  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &) const override;
  std::string toIClipboard(HANDLE) const override;

private:
  UINT m_format;
};
