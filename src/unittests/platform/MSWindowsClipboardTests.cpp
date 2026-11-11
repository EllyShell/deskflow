/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsClipboardTests.h"

#include "platform/MSWindowsClipboard.h"
#include "platform/MSWindowsClipboardBitmapConverter.h"
#include "platform/MSWindowsClipboardFileConverter.h"
#include "platform/MSWindowsClipboardPNGConverter.h"

#include <QtEndian>

#include <QDir>
#include <QFile>

#include <shellapi.h>
#include <shlobj.h>

#include <cstring>
#include <string>
#include <vector>

void MSWindowsClipboardTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Verbose);

  MSWindowsClipboard clipboard(NULL);

  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
}

void MSWindowsClipboardTests::cleanupTestCase()
{
  initTestCase();
}

void MSWindowsClipboardTests::emptyUnusedClipboard()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.emptyUnowned());
}

void MSWindowsClipboardTests::emptyOpenCalled()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
}

void MSWindowsClipboardTests::emptySingleFormat()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));

  clipboard.add(IClipboard::Format::Text, m_testString);
  QVERIFY(clipboard.empty());
  QVERIFY(!clipboard.has(IClipboard::Format::Text));
}

void MSWindowsClipboardTests::addValue()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));

  clipboard.add(IClipboard::Format::Text, m_testString);
  QCOMPARE(clipboard.get(IClipboard::Format::Text), m_testString);
}

void MSWindowsClipboardTests::replaceValue()
{
  using enum IClipboard::Format;

  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));

  clipboard.add(Text, m_testString);
  clipboard.add(Text, m_testString2);

  QCOMPARE(clipboard.get(Text), m_testString2);
}

void MSWindowsClipboardTests::openTimeIsOne()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
}

void MSWindowsClipboardTests::closeIsOpen()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
  clipboard.close();
}

void MSWindowsClipboardTests::getTimeOpenWithNoEmpty()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
  // this behavior is different to that of Clipboard which only
  // returns the value passed into open(t) after empty() is called.
  QCOMPARE(clipboard.getTime(), 1);
}

void MSWindowsClipboardTests::getTimeOpenAndEmpty()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
  QVERIFY(clipboard.empty());
  QCOMPARE(clipboard.getTime(), 1);
}

void MSWindowsClipboardTests::has_withFormatAdded()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());

  clipboard.add(IClipboard::Format::Text, m_testString);
  QVERIFY(clipboard.has(IClipboard::Format::Text));
}

void MSWindowsClipboardTests::has_withNoFormatAdded()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  QCOMPARE(clipboard.get(IClipboard::Format::Text), "");
}

void MSWindowsClipboardTests::getNonEmptyText()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());

  clipboard.add(IClipboard::Format::Text, m_testString);
  QCOMPARE(clipboard.get(IClipboard::Format::Text), m_testString);
}

void MSWindowsClipboardTests::isOwnedByDeskflow()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.isOwnedByDeskflow());
}

void MSWindowsClipboardTests::normalisesMalformedMacBitmap()
{
  // A 1x1 top-down macOS DIB that incorrectly declares a V5 header.
  constexpr qsizetype headerSize = sizeof(BITMAPINFOHEADER);
  std::string dib(headerSize + 4, '\0');
  auto *raw = reinterpret_cast<quint8 *>(&dib[0]);
  qToLittleEndian<quint32>(sizeof(BITMAPV5HEADER), raw); // claims V5 but only has an INFOHEADER
  qToLittleEndian<quint32>(1, raw + 4);
  qToLittleEndian<quint32>(-1, raw + 8);
  qToLittleEndian<quint16>(1, raw + 12);
  qToLittleEndian<quint16>(32, raw + 14);
  qToLittleEndian<quint32>(BI_BITFIELDS, raw + 16);
  raw[headerSize + 3] = 0xff;

  MSWindowsClipboardBitmapConverter converter;
  const auto handle = converter.fromIClipboard(dib);
  QVERIFY(handle != nullptr);
  QCOMPARE(GlobalSize(handle), SIZE_T(headerSize + 4));
  const auto *result = static_cast<const quint8 *>(GlobalLock(handle));
  QVERIFY(result != nullptr);
  QCOMPARE(qFromLittleEndian<quint32>(result), quint32(headerSize));
  QCOMPARE(qFromLittleEndian<quint32>(result + 16), quint32(BI_RGB));
  QCOMPARE(result[headerSize + 3], quint8(0xff));
  GlobalUnlock(handle);
  GlobalFree(handle);
}

void MSWindowsClipboardTests::preservesHealthyMacV5Bitmap()
{
  // A complete 1x1 top-down macOS V5 DIB with BGRA colour masks and one pixel.
  constexpr qsizetype headerSize = sizeof(BITMAPV5HEADER);
  std::string dib(headerSize + 4, '\0');
  auto *raw = reinterpret_cast<quint8 *>(&dib[0]);
  qToLittleEndian<quint32>(headerSize, raw);
  qToLittleEndian<quint32>(1, raw + 4);
  qToLittleEndian<quint32>(-1, raw + 8);
  qToLittleEndian<quint16>(1, raw + 12);
  qToLittleEndian<quint16>(32, raw + 14);
  qToLittleEndian<quint32>(BI_BITFIELDS, raw + 16);
  qToLittleEndian<quint32>(0x00ff0000, raw + 40);
  qToLittleEndian<quint32>(0x0000ff00, raw + 44);
  qToLittleEndian<quint32>(0x000000ff, raw + 48);
  qToLittleEndian<quint32>(0xff000000, raw + 52);
  raw[headerSize] = 0x12;
  raw[headerSize + 1] = 0x34;
  raw[headerSize + 2] = 0x56;
  raw[headerSize + 3] = 0x78;

  MSWindowsClipboardBitmapConverter converter;
  const auto handle = converter.fromIClipboard(dib);
  QVERIFY(handle != nullptr);
  QCOMPARE(GlobalSize(handle), SIZE_T(dib.size()));
  const auto *result = static_cast<const char *>(GlobalLock(handle));
  QVERIFY(result != nullptr);
  QCOMPARE(std::string(result, GlobalSize(handle)), dib);
  GlobalUnlock(handle);
  GlobalFree(handle);
}

void MSWindowsClipboardTests::pngRoundTrip()
{
  MSWindowsClipboardPNGConverter converter;
  QCOMPARE(IClipboard::Format::Bitmap, converter.getFormat());

  // 2x2 32bpp BI_RGB bottom-up DIB: bottom row red/green, top row blue/white.
  // Alpha is deliberately 0 to prove the converter publishes opaque pixels.
  const unsigned char bottomRow[] = {0, 0, 255, 0, 0, 255, 0, 0};
  const unsigned char topRow[] = {255, 0, 0, 0, 255, 255, 255, 0};
  std::string dib(40, '\0');
  auto *header = reinterpret_cast<unsigned char *>(dib.data());
  qToLittleEndian<quint32>(40, header + 0);
  qToLittleEndian<qint32>(2, header + 4);
  qToLittleEndian<qint32>(2, header + 8);
  qToLittleEndian<quint16>(1, header + 12);
  qToLittleEndian<quint16>(32, header + 14);
  qToLittleEndian<quint32>(0, header + 16);
  qToLittleEndian<quint32>(16, header + 20);
  dib.append(reinterpret_cast<const char *>(bottomRow), sizeof(bottomRow));
  dib.append(reinterpret_cast<const char *>(topRow), sizeof(topRow));

  HANDLE pngHandle = converter.fromIClipboard(dib);
  QVERIFY(pngHandle != nullptr);

  std::string png;
  {
    const auto *data = static_cast<const char *>(GlobalLock(pngHandle));
    QVERIFY(data != nullptr);
    png.assign(data, static_cast<size_t>(GlobalSize(pngHandle)));
    GlobalUnlock(pngHandle);
  }
  QVERIFY(png.size() > 8);
  QCOMPARE(static_cast<int>(static_cast<unsigned char>(png[0])), 0x89);
  QCOMPARE(png.substr(1, 3), std::string("PNG"));

  const std::string roundTrip = converter.toIClipboard(pngHandle);
  GlobalFree(pngHandle);

  QVERIFY(!roundTrip.empty());
  QCOMPARE(roundTrip.size(), dib.size());

  const auto *rt = reinterpret_cast<const unsigned char *>(roundTrip.data());
  QCOMPARE(qFromLittleEndian<quint32>(rt + 0), 40u);
  QCOMPARE(qFromLittleEndian<qint32>(rt + 4), 2);
  QCOMPARE(qFromLittleEndian<qint32>(rt + 8), 2);
  QCOMPARE(qFromLittleEndian<quint16>(rt + 14), static_cast<quint16>(32));
  QCOMPARE(qFromLittleEndian<quint32>(rt + 16), 0u);

  std::string expected = dib.substr(40);
  for (size_t i = 3; i < expected.size(); i += 4) {
    expected[i] = static_cast<char>(0xFF);
  }
  QCOMPARE(roundTrip.substr(40), expected);

  // garbage must be rejected instead of producing a header only DIB
  HGLOBAL junk = GlobalAlloc(GMEM_MOVEABLE, 11);
  QVERIFY(junk != nullptr);
  {
    auto *dst = static_cast<char *>(GlobalLock(junk));
    memcpy(dst, "not an imge", 11);
    GlobalUnlock(junk);
  }
  QVERIFY(converter.toIClipboard(junk).empty());
  GlobalFree(junk);
}

void MSWindowsClipboardTests::filePayloadRoundTrip()
{
  MSWindowsClipboardFileConverter converter;
  QCOMPARE(IClipboard::Format::File, converter.getFormat());
  QCOMPARE(static_cast<UINT>(CF_HDROP), converter.getWin32Format());

  const QString path = QDir::temp().filePath(QStringLiteral("deskflow-win-file-payload.txt"));
  const QByteArray content("payload from the windows side\n");
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write(content);
  file.close();

  // build a CF_HDROP the same way explorer does
  const std::wstring wide = QDir::toNativeSeparators(path).toStdWString();
  const size_t headerSize = sizeof(DROPFILES);
  const size_t dataSize = (wide.size() + 2) * sizeof(wchar_t);
  HGLOBAL dropHandle = GlobalAlloc(GMEM_MOVEABLE, headerSize + dataSize);
  QVERIFY(dropHandle != nullptr);
  {
    auto *drop = static_cast<DROPFILES *>(GlobalLock(dropHandle));
    QVERIFY(drop != nullptr);
    memset(drop, 0, headerSize);
    drop->pFiles = static_cast<DWORD>(headerSize);
    drop->fWide = TRUE;
    auto *target = reinterpret_cast<wchar_t *>(reinterpret_cast<BYTE *>(drop) + headerSize);
    memcpy(target, wide.c_str(), wide.size() * sizeof(wchar_t));
    target[wide.size()] = L'\0';
    target[wide.size() + 1] = L'\0';
    GlobalUnlock(dropHandle);
  }

  const std::string payload = converter.toIClipboard(dropHandle);
  GlobalFree(dropHandle);
  QVERIFY(!payload.empty());

  HANDLE result = converter.fromIClipboard(payload);
  QVERIFY(result != nullptr);

  QCOMPARE(DragQueryFileW(static_cast<HDROP>(result), 0xFFFFFFFF, nullptr, 0), static_cast<UINT>(1));
  std::vector<wchar_t> buffer(32768);
  const UINT length = DragQueryFileW(static_cast<HDROP>(result), 0, buffer.data(), 32768);
  QVERIFY(length > 0);

  const QString received = QString::fromWCharArray(buffer.data(), static_cast<int>(length));
  QVERIFY(received.endsWith(QStringLiteral("deskflow-win-file-payload.txt")));
  QFile roundTrip(received);
  QVERIFY(roundTrip.open(QIODevice::ReadOnly));
  QCOMPARE(roundTrip.readAll(), content);
  GlobalFree(result);
}

QTEST_MAIN(MSWindowsClipboardTests)
