/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2011 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXClipboardTests.h"

#include "platform/OSXClipboard.h"
#include "platform/OSXClipboardImageConverter.h"
#include "common/ClipboardFile.h"
#include "platform/OSXClipboardUTF8Converter.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QtEndian>

#include <Carbon/Carbon.h>

void OSXClipboardTests::open()
{
  OSXClipboard clipboard;
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  clipboard.close();
}

void OSXClipboardTests::singleFormat()
{
  using enum IClipboard::Format;

  OSXClipboard clipboard;
  QVERIFY(clipboard.empty());
  clipboard.add(Text, m_testString);
  QVERIFY(clipboard.has(Text));
  QCOMPARE(clipboard.get(Text), m_testString);
}

void OSXClipboardTests::formatConvert_UTF8()
{
  OSXClipboardUTF8Converter converter;
  QCOMPARE(IClipboard::Format::Text, converter.getFormat());
  QCOMPARE(converter.getOSXFormat(), CFSTR("public.utf8-plain-text"));
  QCOMPARE(converter.fromIClipboard("test data\n"), "test data\r");
  QCOMPARE(converter.toIClipboard("test data\r"), "test data\n");
}

namespace {
//! 2x2 32bpp BI_RGB bottom-up DIB: bottom row red/green, top row blue/white.
//! Alpha is deliberately 0 to prove the converters publish opaque pixels.
std::string makeTestDib()
{
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
  return dib;
}

//! pixels with alpha forced opaque, in the same bottom-up order as the input
std::string expectedPixels()
{
  std::string expected = makeTestDib().substr(40);
  for (size_t i = 3; i < expected.size(); i += 4) {
    expected[i] = static_cast<char>(0xFF);
  }
  return expected;
}

void verifyRoundTrip(OSXClipboardImageConverter &converter, const std::string &dib, const std::string &encoded)
{
  QVERIFY(!encoded.empty());
  const std::string roundTrip = converter.toIClipboard(encoded);
  QVERIFY(!roundTrip.empty());
  QCOMPARE(roundTrip.size(), dib.size());

  const auto *rt = reinterpret_cast<const unsigned char *>(roundTrip.data());
  QCOMPARE(qFromLittleEndian<quint32>(rt + 0), 40u);
  QCOMPARE(qFromLittleEndian<qint32>(rt + 4), 2);
  QCOMPARE(qFromLittleEndian<qint32>(rt + 8), 2);
  QCOMPARE(qFromLittleEndian<quint16>(rt + 14), static_cast<quint16>(32));
  QCOMPARE(qFromLittleEndian<quint32>(rt + 16), 0u);
  QCOMPARE(roundTrip.substr(40), expectedPixels());

  // garbage must be rejected instead of producing a header only DIB
  QVERIFY(converter.toIClipboard("not an image").empty());
  QVERIFY(converter.fromIClipboard(std::string(10, '\0')).empty());
}
} // namespace

void OSXClipboardTests::bitmapRoundTrip()
{
  using enum IClipboard::Format;

  // exercises the whole path: add() writes png+tiff+bmp flavors, get() reads the
  // first registered image flavor (png) and decodes it back to the same DIB
  const std::string dib = makeTestDib();
  OSXClipboard clipboard;
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  clipboard.add(Bitmap, dib);
  QVERIFY(clipboard.has(Bitmap));

  const std::string result = clipboard.get(Bitmap);
  clipboard.close();

  QVERIFY(!result.empty());
  QCOMPARE(result.size(), dib.size());
  const auto *rt = reinterpret_cast<const unsigned char *>(result.data());
  QCOMPARE(qFromLittleEndian<quint32>(rt + 0), 40u);
  QCOMPARE(qFromLittleEndian<qint32>(rt + 4), 2);
  QCOMPARE(qFromLittleEndian<qint32>(rt + 8), 2);
  QCOMPARE(qFromLittleEndian<quint16>(rt + 14), static_cast<quint16>(32));
  QCOMPARE(result.substr(40), expectedPixels());
}

void OSXClipboardTests::formatConvert_Png()
{
  OSXClipboardImageConverter converter(CFSTR("public.png"));
  QCOMPARE(IClipboard::Format::Bitmap, converter.getFormat());
  QCOMPARE(converter.getOSXFormat(), CFSTR("public.png"));

  const std::string dib = makeTestDib();
  const std::string png = converter.fromIClipboard(dib);
  QVERIFY(!png.empty());
  QCOMPARE(static_cast<unsigned char>(png[0]), static_cast<unsigned char>(0x89));
  QCOMPARE(png.substr(1, 3), std::string("PNG"));

  verifyRoundTrip(converter, dib, png);
}

void OSXClipboardTests::formatConvert_Tiff()
{
  OSXClipboardImageConverter converter(CFSTR("public.tiff"));
  QCOMPARE(IClipboard::Format::Bitmap, converter.getFormat());
  QCOMPARE(converter.getOSXFormat(), CFSTR("public.tiff"));

  const std::string dib = makeTestDib();
  const std::string tiff = converter.fromIClipboard(dib);
  QVERIFY(!tiff.empty());
  const bool littleEndian = tiff.size() > 4 && tiff[0] == 'I' && tiff[1] == 'I';
  const bool bigEndian = tiff.size() > 4 && tiff[0] == 'M' && tiff[1] == 'M';
  QVERIFY(littleEndian || bigEndian);

  verifyRoundTrip(converter, dib, tiff);
}

void OSXClipboardTests::readTiffOnly()
{
  using enum IClipboard::Format;

  // simulate an application (the macOS screenshot UI does this) that publishes
  // only public.tiff, with no png and no com.microsoft.bmp flavor
  OSXClipboardImageConverter encoder(CFSTR("public.tiff"));
  const std::string dib = makeTestDib();
  const std::string tiff = encoder.fromIClipboard(dib);
  QVERIFY(!tiff.empty());

  PasteboardRef board = nullptr;
  QCOMPARE(PasteboardCreate(kPasteboardClipboard, &board), noErr);
  QVERIFY(board != nullptr);
  PasteboardClear(board);

  CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(tiff.data()), tiff.size());
  QVERIFY(data != nullptr);
  PasteboardItemID itemID = 0;
  const OSStatus put = PasteboardPutItemFlavor(board, itemID, CFSTR("public.tiff"), data, 0);
  CFRelease(data);
  CFRelease(board);
  QCOMPARE(put, noErr);

  OSXClipboard clipboard;
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.has(Bitmap));
  const std::string result = clipboard.get(Bitmap);
  clipboard.close();

  QVERIFY(!result.empty());
  QCOMPARE(result.size(), dib.size());
  const auto *rt = reinterpret_cast<const unsigned char *>(result.data());
  QCOMPARE(qFromLittleEndian<quint32>(rt + 0), 40u);
  QCOMPARE(qFromLittleEndian<qint32>(rt + 4), 2);
  QCOMPARE(qFromLittleEndian<qint32>(rt + 8), 2);
  QCOMPARE(result.substr(40), expectedPixels());
}

void OSXClipboardTests::fileUrlImage()
{
  using enum IClipboard::Format;

  // applications that save a screenshot and copy the file publish public.file-url
  OSXClipboardImageConverter encoder(CFSTR("public.png"));
  const std::string dib = makeTestDib();
  const std::string png = encoder.fromIClipboard(dib);
  QVERIFY(!png.empty());

  const QString path = QDir::temp().filePath(QStringLiteral("deskflow-file-url-test.png"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write(png.data(), static_cast<qint64>(png.size()));
  file.close();

  PasteboardRef board = nullptr;
  QCOMPARE(PasteboardCreate(kPasteboardClipboard, &board), noErr);
  QVERIFY(board != nullptr);
  PasteboardClear(board);

  const QByteArray url = QUrl::fromLocalFile(path).toString().toUtf8();
  CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(url.constData()), url.size());
  QVERIFY(data != nullptr);
  PasteboardItemID itemID = 0;
  const OSStatus put = PasteboardPutItemFlavor(board, itemID, CFSTR("public.file-url"), data, 0);
  CFRelease(data);
  CFRelease(board);
  QCOMPARE(put, noErr);

  OSXClipboard clipboard;
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.has(Bitmap));
  const std::string result = clipboard.get(Bitmap);
  clipboard.close();

  QVERIFY(!result.empty());
  QCOMPARE(result.size(), dib.size());
  const auto *rt = reinterpret_cast<const unsigned char *>(result.data());
  QCOMPARE(qFromLittleEndian<quint32>(rt + 0), 40u);
  QCOMPARE(qFromLittleEndian<qint32>(rt + 4), 2);
  QCOMPARE(qFromLittleEndian<qint32>(rt + 8), 2);
  QCOMPARE(result.substr(40), expectedPixels());
}

// This is where the zero length File format came from: for an image file url
// has(File) is true because public.file-url exists, but get(File) is empty
// because image files are claimed by the bitmap converter. Sending that empty
// File entry made the peer clear its pasteboard to reproduce "present, but
// nothing".
void OSXClipboardTests::marshallImageFileUrl()
{
  using enum IClipboard::Format;

  OSXClipboardImageConverter encoder(CFSTR("public.png"));
  const std::string dib = makeTestDib();
  const std::string png = encoder.fromIClipboard(dib);
  QVERIFY(!png.empty());

  const QString path = QDir::temp().filePath(QStringLiteral("deskflow-marshall-image-url.png"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(png.data(), static_cast<qint64>(png.size()));
  file.close();

  PasteboardRef board = nullptr;
  QCOMPARE(PasteboardCreate(kPasteboardClipboard, &board), noErr);
  QVERIFY(board != nullptr);
  PasteboardClear(board);

  const QByteArray url = QUrl::fromLocalFile(path).toString().toUtf8();
  CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(url.constData()), url.size());
  QVERIFY(data != nullptr);
  PasteboardItemID itemID = 0;
  const OSStatus put = PasteboardPutItemFlavor(board, itemID, CFSTR("public.file-url"), data, 0);
  CFRelease(data);
  CFRelease(board);
  QCOMPARE(put, noErr);

  OSXClipboard clipboard;
  // the premise of the regression: File reports present while yielding no data
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.has(File));
  QVERIFY(clipboard.get(File).empty());
  clipboard.close();

  const std::string frame = IClipboard::marshall(&clipboard);
  QVERIFY(frame.size() > 4);

  // exactly one format may travel: the bitmap. File may not be present with a
  // zero length payload.
  const auto *raw = reinterpret_cast<const unsigned char *>(frame.data());
  QCOMPARE(qFromBigEndian<quint32>(raw), 1u);
  QCOMPARE(qFromBigEndian<quint32>(raw + 4), static_cast<quint32>(Bitmap));
  QCOMPARE(qFromBigEndian<quint32>(raw + 8), static_cast<quint32>(dib.size()));

  // do not leave a file url pointing at a file this test is about to delete
  QCOMPARE(PasteboardCreate(kPasteboardClipboard, &board), noErr);
  QVERIFY(board != nullptr);
  PasteboardClear(board);
  CFRelease(board);
  QFile::remove(path);
}

void OSXClipboardTests::filePayload()
{
  using enum IClipboard::Format;

  // the payload is stored under the received basename in a directory shared by
  // every transfer, so a fixed name collides with the copy left behind by an
  // earlier run and the helper deliberately picks a numbered variant instead.
  // use a unique name and remove the stored copy, otherwise this test only
  // passes the first time it runs.
  const QString fileName = QStringLiteral("deskflow-file-payload-test-%1.txt").arg(QDateTime::currentMSecsSinceEpoch());
  const QString path = QDir::temp().filePath(fileName);
  const QByteArray content("payload from the mac side\n");
  QFile source(path);
  QVERIFY(source.open(QIODevice::WriteOnly));
  source.write(content);
  source.close();

  PasteboardRef board = nullptr;
  QCOMPARE(PasteboardCreate(kPasteboardClipboard, &board), noErr);
  QVERIFY(board != nullptr);
  PasteboardClear(board);

  const QByteArray url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded).toUtf8();
  CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(url.constData()), url.size());
  QVERIFY(data != nullptr);
  PasteboardItemID itemID = 0;
  const OSStatus put = PasteboardPutItemFlavor(board, itemID, CFSTR("public.file-url"), data, 0);
  CFRelease(data);
  CFRelease(board);
  QCOMPARE(put, noErr);

  OSXClipboard clipboard;
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.has(File));
  const std::string payload = clipboard.get(File);
  clipboard.close();
  QVERIFY(!payload.empty());

  // the payload must round trip back into a file with the same content
  QString received;
  QVERIFY(deskflow::files::writeFilePayload(payload, received));
  QVERIFY(received.endsWith(fileName));
  QFile result(received);
  QVERIFY(result.open(QIODevice::ReadOnly));
  QCOMPARE(result.readAll(), content);
  result.close();

  QFile::remove(received);
  QFile::remove(path);
}

void OSXClipboardTests::emptyFilePayload()
{
  const QString path = QDir::temp().filePath(QStringLiteral("deskflow-empty-file-payload.txt"));
  QFile source(path);
  QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
  source.close();

  std::string payload;
  QVERIFY(deskflow::files::readFilePayload(path, payload));
  QString received;
  QVERIFY(deskflow::files::writeFilePayload(payload, received));
  QCOMPARE(QFileInfo(received).size(), 0);

  QFile::remove(received);
  QFile::remove(path);
}

// The Windows peer used to send a 12 byte frame: one format, zero length payload.
// Receiving it must not take ownership of the local pasteboard, or the user's
// screenshot is cleared and replaced with a flavor that holds nothing.
void OSXClipboardTests::emptyPayloadFrameKeepsPasteboard()
{
  const QByteArray marker("peer-fix-marker");

  PasteboardRef board = nullptr;
  QCOMPARE(PasteboardCreate(kPasteboardClipboard, &board), noErr);
  QVERIFY(board != nullptr);
  PasteboardClear(board);
  CFDataRef data =
      CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(marker.constData()), marker.size());
  QVERIFY(data != nullptr);
  PasteboardItemID itemID = 0;
  const OSStatus put = PasteboardPutItemFlavor(board, itemID, CFSTR("public.utf8-plain-text"), data, 0);
  CFRelease(data);
  CFRelease(board);
  QCOMPARE(put, noErr);

  std::string frame;
  frame += (char)0;
  frame += (char)0;
  frame += (char)0;
  frame += (char)1; // 1 format
  frame += (char)0;
  frame += (char)0;
  frame += (char)0;
  frame += (char)IClipboard::Format::File;
  frame += (char)0;
  frame += (char)0;
  frame += (char)0;
  frame += (char)0; // zero length payload
  QCOMPARE(frame.size(), size_t{12}); // the exact size the peer sent

  OSXClipboard clipboard;
  IClipboard::unmarshall(&clipboard, frame, 0);

  // the marker must still be on the pasteboard
  QCOMPARE(PasteboardCreate(kPasteboardClipboard, &board), noErr);
  QVERIFY(board != nullptr);
  QCOMPARE(PasteboardGetItemIdentifier(board, (CFIndex)1, &itemID), noErr);
  PasteboardFlavorFlags flags;
  QCOMPARE(PasteboardGetItemFlavorFlags(board, itemID, CFSTR("public.utf8-plain-text"), &flags), noErr);
  CFDataRef read = nullptr;
  QCOMPARE(PasteboardCopyItemFlavorData(board, itemID, CFSTR("public.utf8-plain-text"), &read), noErr);
  QVERIFY(read != nullptr);
  const QByteArray received(reinterpret_cast<const char *>(CFDataGetBytePtr(read)), CFDataGetLength(read));
  CFRelease(read);
  CFRelease(board);
  QCOMPARE(received, marker);
}

QTEST_MAIN(OSXClipboardTests)
