/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <string>
#include <string_view>

namespace deskflow::text {

//! Convert CR, LF and CRLF line endings to LF.
/*!
IClipboard defines text and HTML data as UTF-8 with LF line endings, so this is
the canonical form used inside deskflow.  Every CR is treated as a line break,
including one that is not followed by LF.  The conversion is idempotent:
toLf(toLf(x)) == toLf(x).
*/
std::string toLf(std::string_view in);

//! Convert to Windows line endings (CRLF).
/*!
Normalises to LF first, therefore input that already contains CRLF can never
produce CRCRLF (the source of double line spacing when pasting).
*/
std::string toCrLf(std::string_view in);

//! Convert to classic macOS line endings (CR).
/*!
The pasteboard uses CR.  Normalises to LF first so a CRLF input cannot turn into
two line breaks.
*/
std::string toCr(std::string_view in);

} // namespace deskflow::text
