/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

#include <string>

namespace deskflow::osx {

//! Resolve a pasteboard file url, including finder file reference urls
/*!
Finder publishes file:///.file/id=... reference urls. QUrl treats those as
local paths and hands back "/.file/id=..." which cannot be opened, so the url
has to be resolved through CoreFoundation first.
*/
QString localPathFromFileUrl(const std::string &urlData);

} // namespace deskflow::osx