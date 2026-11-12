/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientProxyTests.h"

#include "../deskflow/MockEventQueue.h"
#include "base/EventQueueTimer.h"
#include "deskflow/AppUtil.h"
#include "deskflow/Clipboard.h"
#include "io/IStream.h"
#include "server/ClientProxy1_0.h"
#include "server/ClientProxy1_1.h"
#include "server/ClientProxy1_6.h"
#include "server/ClientProxy1_7.h"
#include "server/ClientProxy1_8.h"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QByteArray>
#include <QTest>

namespace {

class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }

  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }

  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

class CapturingStream : public deskflow::IStream
{
public:
  void setPendingOutputSize(uint32_t size)
  {
    m_pendingOutputSize = size;
  }

  uint32_t getPendingOutputSize() const override
  {
    return m_pendingOutputSize;
  }

  QByteArray take()
  {
    auto bytes = m_buffer;
    m_buffer.clear();
    return bytes;
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_buffer.append(static_cast<const char *>(buffer), n);
  }

  void close() override
  {
  }

  uint32_t read(void *, uint32_t) override
  {
    return 0;
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<CapturingStream *>(this);
  }

  bool isReady() const override
  {
    return false;
  }

  uint32_t getSize() const override
  {
    return 0;
  }

private:
  QByteArray m_buffer;
  uint32_t m_pendingOutputSize = 0;
};

class ManualTimerEventQueue : public MockEventQueue
{
public:
  EventQueueTimer *newOneShotTimer(double, void *) override
  {
    auto *timer = new EventQueueTimer;
    m_timers.push_back(timer);
    return timer;
  }

  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    if (type == EventTypes::Timer) {
      m_timerHandlers[target] = handler;
    }
  }

  void removeHandler(EventTypes type, void *target) override
  {
    if (type == EventTypes::Timer) {
      m_timerHandlers.erase(target);
    }
  }

  void deleteTimer(EventQueueTimer *timer) override
  {
    m_timers.erase(std::remove(m_timers.begin(), m_timers.end(), timer), m_timers.end());
    delete timer;
  }

  bool fireNextTimer()
  {
    if (m_timers.empty()) {
      return false;
    }
    auto *timer = m_timers.front();
    m_timers.pop_front();
    auto handler = m_timerHandlers.find(timer);
    if (handler == m_timerHandlers.end()) {
      return false;
    }
    const EventHandler callback = handler->second;
    callback(Event(EventTypes::Timer, timer));
    return true;
  }

private:
  std::deque<EventQueueTimer *> m_timers;
  std::map<void *, EventHandler> m_timerHandlers;
};

std::unique_ptr<ClientProxy> makeProxy(int minor, deskflow::IStream *stream, IEventQueue *events)
{
  // the 1.4 and later constructors assert the server pointer is non-null but only store it
  auto *server = reinterpret_cast<Server *>(0x1);

  std::unique_ptr<ClientProxy> proxy;
  switch (minor) {
  case 0:
    proxy = std::make_unique<ClientProxy1_0>("client", stream, events);
    break;
  case 1:
    proxy = std::make_unique<ClientProxy1_1>("client", stream, events);
    break;
  case 6:
    proxy = std::make_unique<ClientProxy1_6>("client", stream, server, events);
    break;
  case 7:
    proxy = std::make_unique<ClientProxy1_7>("client", stream, server, events);
    break;
  case 8:
    proxy = std::make_unique<ClientProxy1_8>("client", stream, server, events);
    break;
  default:
    break;
  }
  return proxy;
}

struct ProxyUnderTest
{
  MockEventQueue events;
  CapturingStream *stream = new CapturingStream;
  std::unique_ptr<ClientProxy> proxy;

  explicit ProxyUnderTest(int minor) : proxy(makeProxy(minor, stream, &events))
  {
    // drop the query-info and layout-sync messages the constructors send
    stream->take();
  }
};

const KeyID kKey = 0x61;
const KeyModifierMask kMask = 0;
const KeyButton kButton = 0x1e;
const int32_t kCount = 3;
const std::string kLang = "en";

} // namespace

void ClientProxyTests::initTestCase()
{
  // the 1.8 constructor reads the keyboard layouts through AppUtil::instance()
  static TestAppUtil appUtil;
}

// These formats are frozen because shipped third-party clients parse them byte
// for byte: Synergy 1.4 through 1.14.1 negotiate 1.4 through 1.7, Barrier and
// Input Leap negotiate 1.6, and Synergy 1.14.2 onwards and Deskflow negotiate 1.8.
void ClientProxyTests::keyDown_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKDN" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKDL" + QByteArray::fromHex("0061 0000 001e 00000002") + "en";
}

void ClientProxyTests::keyDown()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyDown(kKey, kMask, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyRepeat_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKRP" + QByteArray::fromHex("0061 0000 0003");
  QTest::newRow("1.1") << 1 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.6") << 6 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.7") << 7 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.8") << 8 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e 00000002") + "en";
}

void ClientProxyTests::keyRepeat()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyRepeat(kKey, kMask, kCount, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyUp_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKUP" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
}

void ClientProxyTests::keyUp()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyUp(kKey, kMask, kButton);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::pacedClipboardKeepsMouseResponsive()
{
  ManualTimerEventQueue events;
  auto *stream = new CapturingStream;
  auto proxy = makeProxy(6, stream, &events);
  stream->take(); // constructor's query-info message

  Clipboard clipboard;
  QVERIFY(clipboard.open(0));
  clipboard.add(IClipboard::Format::Text, std::string(128 * 1024, 'x'));
  clipboard.close();
  proxy->setClipboardDirty(kClipboardClipboard, true);
  proxy->setClipboard(kClipboardClipboard, &clipboard);

  stream->setPendingOutputSize(64 * 1024);
  QVERIFY(events.fireNextTimer());
  QVERIFY(stream->take().isEmpty());

  stream->setPendingOutputSize(0);
  QVERIFY(events.fireNextTimer()); // clipboard start
  proxy->mouseMove(123, 456);
  QVERIFY(events.fireNextTimer()); // first data chunk
  const QByteArray output = stream->take();
  const auto start = output.indexOf("DCLP");
  const auto mouse = output.indexOf("DMMV");
  const auto data = output.indexOf("DCLP", start + 4);
  QVERIFY(start >= 0);
  QVERIFY(mouse > start);
  QVERIFY(data > mouse);

  for (int i = 0; i < 20 && events.fireNextTimer(); ++i) {
  }
  QVERIFY(!events.fireNextTimer());
}

QTEST_MAIN(ClientProxyTests)
