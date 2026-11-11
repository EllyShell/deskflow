/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardTextTests.h"

#include "common/ClipboardText.h"

namespace {
QString asQString(const std::string &value)
{
  return QString::fromStdString(value);
}
} // namespace

void ClipboardTextTests::lfFromCrLf()
{
  QCOMPARE(asQString(deskflow::text::toLf("A\r\nB")), QStringLiteral("A\nB"));
  QCOMPARE(asQString(deskflow::text::toLf("A\r\nB\r\nC")), QStringLiteral("A\nB\nC"));
}

void ClipboardTextTests::lfFromLoneCr()
{
  QCOMPARE(asQString(deskflow::text::toLf("A\rB")), QStringLiteral("A\nB"));
  QCOMPARE(asQString(deskflow::text::toLf("A\r\rB")), QStringLiteral("A\n\nB"));
}

void ClipboardTextTests::lfFromMixedEndings()
{
  // the sequence produced by a CRLF round trip through the Windows clip board
  QCOMPARE(asQString(deskflow::text::toLf("A\r\r\nB")), QStringLiteral("A\n\nB"));
  QCOMPARE(asQString(deskflow::text::toLf("A\n\rB")), QStringLiteral("A\n\nB"));
  QCOMPARE(asQString(deskflow::text::toLf("A\r\n\r\nB")), QStringLiteral("A\n\nB"));
}

void ClipboardTextTests::lfIsIdempotent()
{
  const std::string input = "x\r\ny\rz\n\rw\r\r\n";
  const std::string once = deskflow::text::toLf(input);
  QCOMPARE(asQString(deskflow::text::toLf(once)), asQString(once));
}

void ClipboardTextTests::crLfNeverProducesDoubleCr()
{
  QCOMPARE(asQString(deskflow::text::toCrLf("A\r\r\nB")), QStringLiteral("A\r\n\r\nB"));
  QCOMPARE(asQString(deskflow::text::toCrLf("A\r\nB")), QStringLiteral("A\r\nB"));
  QCOMPARE(asQString(deskflow::text::toCrLf("A\rB")), QStringLiteral("A\r\nB"));
}

void ClipboardTextTests::crLfRoundTripsThroughLf()
{
  const std::string input = "one\r\ntwo\rthree\n\rfour";
  QCOMPARE(asQString(deskflow::text::toLf(deskflow::text::toCrLf(input))), asQString(deskflow::text::toLf(input)));
  QCOMPARE(asQString(deskflow::text::toLf(deskflow::text::toCr(input))), asQString(deskflow::text::toLf(input)));
}

void ClipboardTextTests::crFromLf()
{
  QCOMPARE(asQString(deskflow::text::toCr("A\nB")), QStringLiteral("A\rB"));
  QCOMPARE(asQString(deskflow::text::toCr("A\r\nB")), QStringLiteral("A\rB"));
  QCOMPARE(asQString(deskflow::text::toCr("A\r\r\nB")), QStringLiteral("A\r\rB"));
}

void ClipboardTextTests::emptyAndPlainText()
{
  QCOMPARE(asQString(deskflow::text::toLf("")), QString());
  QCOMPARE(asQString(deskflow::text::toCrLf("")), QString());
  QCOMPARE(asQString(deskflow::text::toCr("")), QString());
  QCOMPARE(asQString(deskflow::text::toLf("plain text")), QStringLiteral("plain text"));
  QCOMPARE(asQString(deskflow::text::toCrLf("plain text")), QStringLiteral("plain text"));
}

QTEST_MAIN(ClipboardTextTests)
