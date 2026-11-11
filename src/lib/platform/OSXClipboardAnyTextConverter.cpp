/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboardAnyTextConverter.h"

#include "common/ClipboardText.h"

//
// OSXClipboardAnyTextConverter
//

IClipboard::Format OSXClipboardAnyTextConverter::getFormat() const
{
  return IClipboard::Format::Text;
}

std::string OSXClipboardAnyTextConverter::fromIClipboard(const std::string &data) const
{
  // convert linefeeds and then convert to desired encoding
  return doFromIClipboard(deskflow::text::toCr(data));
}

std::string OSXClipboardAnyTextConverter::toIClipboard(const std::string &data) const
{
  // convert text then newlines
  return deskflow::text::toLf(doToIClipboard(data));
}
