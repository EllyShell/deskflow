/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2011 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXKeyStateTests.h"

#include "base/EventQueue.h"

#include <QThread>

#include <CoreFoundation/CoreFoundation.h>

namespace {

class ActiveGroupPollThread : public QThread
{
public:
  explicit ActiveGroupPollThread(const OSXKeyState &keyState) : m_keyState(keyState) { }

  int32_t result() const { return m_result; }

private:
  void run() override
  {
    for (int i = 0; i < 100; ++i) {
      m_result = m_keyState.pollActiveGroup();
    }
  }

  const OSXKeyState &m_keyState;
  int32_t m_result = -1;
};

class KeyEventMapThread : public QThread
{
public:
  explicit KeyEventMapThread(const OSXKeyState &keyState) : m_keyState(keyState) { }

  bool eventCreated() const { return m_eventCreated; }
  KeyButton result() const { return m_result; }

private:
  void run() override
  {
    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, kVK_ANSI_A, true);
    if (event == nullptr) {
      return;
    }
    m_eventCreated = true;
    KeyModifierMask mask = 0;
    OSXKeyState::KeyIDs ids;
    for (int i = 0; i < 100; ++i) {
      ids.clear();
      m_result = m_keyState.mapKeyFromEvent(ids, &mask, event);
    }
    CFRelease(event);
  }

  const OSXKeyState &m_keyState;
  bool m_eventCreated = false;
  KeyButton m_result = kKeyNone;
};

class KeyMapUpdateThread : public QThread
{
public:
  explicit KeyMapUpdateThread(OSXKeyState &keyState) : m_keyState(keyState) { }

private:
  void run() override
  {
    for (int i = 0; i < 5; ++i) {
      m_keyState.updateKeyMap();
    }
  }

  OSXKeyState &m_keyState;
};

void runWorkerAndPumpMainQueue(QThread &worker)
{
  worker.start();
  const CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 5.0;
  while (!worker.isFinished() && CFAbsoluteTimeGetCurrent() < deadline) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
  }
  if (!worker.isFinished()) {
    qFatal("input-source operation did not return from the main queue");
  }
  worker.wait();
}

} // namespace

#define SHIFT_ID_L kKeyShift_L
#define SHIFT_ID_R kKeyShift_R
#define SHIFT_BUTTON 57
#define A_CHAR_ID 0x00000061
#define A_CHAR_BUTTON 001

void OSXKeyStateTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Verbose);
}

void OSXKeyStateTests::mapModifiersFromOSX_OSXMask()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  KeyModifierMask outMask = 0;

  uint32_t shiftMask = 0 | kCGEventFlagMaskShift;
  outMask = keyState.mapModifiersFromOSX(shiftMask);
  QCOMPARE(outMask, KeyModifierShift);

  uint32_t ctrlMask = 0 | kCGEventFlagMaskControl;
  outMask = keyState.mapModifiersFromOSX(ctrlMask);
  QCOMPARE(outMask, KeyModifierControl);

  uint32_t altMask = 0 | kCGEventFlagMaskAlternate;
  outMask = keyState.mapModifiersFromOSX(altMask);
  QCOMPARE(outMask, KeyModifierAlt);

  uint32_t cmdMask = 0 | kCGEventFlagMaskCommand;
  outMask = keyState.mapModifiersFromOSX(cmdMask);
  QCOMPARE(outMask, KeyModifierSuper);

  uint32_t capsMask = 0 | kCGEventFlagMaskAlphaShift;
  outMask = keyState.mapModifiersFromOSX(capsMask);
  QCOMPARE(outMask, KeyModifierCapsLock);

  uint32_t numMask = 0 | kCGEventFlagMaskNumericPad;
  outMask = keyState.mapModifiersFromOSX(numMask);
  QCOMPARE(outMask, KeyModifierNumLock);
}

void OSXKeyStateTests::fakePollShift()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(SHIFT_ID_L, 0, 1, "en");
  QVERIFY(isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyDown(SHIFT_ID_R, 0, 2, "en");
  QVERIFY(isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyUp(2);
  QVERIFY(!isKeyPressed(keyState, SHIFT_BUTTON));
}

void OSXKeyStateTests::fakePollChar()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(A_CHAR_ID, 0, 1, "en");
  QVERIFY(isKeyPressed(keyState, A_CHAR_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, A_CHAR_BUTTON));

  // HACK: delete the key in case it was typed into a text editor.
  // we should really set focus to an invisible window.
  keyState.fakeKeyDown(kKeyBackSpace, 0, 2, "en");
  keyState.fakeKeyUp(2);
}

void OSXKeyStateTests::fakePollCharWithModifier()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(A_CHAR_ID, KeyModifierShift, 1, "en");
  QVERIFY(isKeyPressed(keyState, A_CHAR_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, A_CHAR_BUTTON));

  // HACK: delete the key in case it was typed into a text editor.
  // we should really set focus to an invisible window.
  keyState.fakeKeyDown(kKeyBackSpace, 0, 2, "en");
  keyState.fakeKeyUp(2);
}

void OSXKeyStateTests::pollActiveGroupFromWorkerThread()
{
  m_log.setFilter(LogLevel::Level::Error);
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  ActiveGroupPollThread worker(keyState);
  runWorkerAndPumpMainQueue(worker);
  QVERIFY(worker.result() >= 0);
}

void OSXKeyStateTests::mapKeyFromEventFromWorkerThread()
{
  m_log.setFilter(LogLevel::Level::Error);
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  KeyEventMapThread worker(keyState);
  runWorkerAndPumpMainQueue(worker);
  QVERIFY(worker.eventCreated());
  QVERIFY(worker.result() != kKeyNone);
}

void OSXKeyStateTests::updateKeyMapFromWorkerThread()
{
  m_log.setFilter(LogLevel::Level::Error);
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  KeyMapUpdateThread worker(keyState);
  runWorkerAndPumpMainQueue(worker);
}

bool OSXKeyStateTests::isKeyPressed(const OSXKeyState &keyState, KeyButton button)
{
  // HACK: allow os to realize key state changes.
  Arch::sleep(.2);

  IKeyState::KeyButtonSet pressed;
  keyState.pollPressedKeys(pressed);

  IKeyState::KeyButtonSet::const_iterator it;
  for (it = pressed.begin(); it != pressed.end(); ++it) {
    LOG_DEBUG("checking key %d", *it);
    if (*it == button) {
      return true;
    }
  }
  return false;
}

QTEST_MAIN(OSXKeyStateTests)
