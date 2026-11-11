/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/ClipboardText.h"

namespace deskflow::text {

std::string toLf(std::string_view in)
{
  std::string out;
  out.reserve(in.size());

  for (size_t i = 0; i < in.size(); ++i) {
    const char ch = in[i];
    if (ch != '\r') {
      out.push_back(ch);
      continue;
    }

    // A CR is a line break on its own; CRLF counts once.
    out.push_back('\n');
    if (i + 1 < in.size() && in[i + 1] == '\n') {
      ++i;
    }
  }

  return out;
}

std::string toCrLf(std::string_view in)
{
  const std::string lf = toLf(in);
  std::string out;
  out.reserve(lf.size() + lf.size() / 8);

  for (const char ch : lf) {
    if (ch == '\n') {
      out.push_back('\r');
    }
    out.push_back(ch);
  }

  return out;
}

std::string toCr(std::string_view in)
{
  const std::string lf = toLf(in);
  std::string out;
  out.reserve(lf.size());

  for (const char ch : lf) {
    out.push_back(ch == '\n' ? '\r' : ch);
  }

  return out;
}

} // namespace deskflow::text
