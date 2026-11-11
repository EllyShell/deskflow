/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

#include <string>

namespace deskflow::files {

//! largest file name (basename) that fits in the clipboard file payload
constexpr uint32_t kMaxNameLength = 1024;
//! largest file that is transferred through the clipboard
constexpr qint64 kMaxFileBytes = 64LL * 1024 * 1024;

//! True when the path looks like an image, those travel as a bitmap instead
bool isImageFile(const QString &path);

//! Read the file into the internal payload: name length, name, content
bool readFilePayload(const QString &path, std::string &payload);

//! Write the internal payload to a file in the clipboard temp directory
bool writeFilePayload(const std::string &payload, QString &path);

} // namespace deskflow::files
