/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "KeySequenceTests.h"

#include "common/KeySequence.h"

void KeySequenceTests::toString_controlShiftPlus_usesNamedPlus()
{
  KeySequence sequence;

  sequence.appendKey(Qt::Key_Control, Qt::ControlModifier);
  sequence.appendKey(Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier);
  QVERIFY(sequence.appendKey(Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier));

#if defined(Q_OS_MACOS)
  // Qt maps the Command key to ControlModifier on macOS.
  QCOMPARE(sequence.toString(), QStringLiteral("Super+Shift+Plus"));
#else
  QCOMPARE(sequence.toString(), QStringLiteral("Control+Shift+Plus"));
#endif
}

QTEST_MAIN(KeySequenceTests)
