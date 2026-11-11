/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboardImageConverter.h"

#include "base/Log.h"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <QtEndian>

#include <cstring>
#include <vector>

namespace {

constexpr quint32 kDibHeaderSize = 40;
constexpr quint32 kBiRgb = 0;
constexpr size_t kMaxDimension = 32768;

struct DibInfo
{
  qint32 width = 0;
  qint32 height = 0; // negative means top-down rows
  quint16 bitCount = 0;
  quint32 compression = 0;
  const quint8 *pixels = nullptr;
  size_t pixelBytes = 0;
  size_t srcStride = 0;
  size_t rows = 0;
};

bool parseDib(const std::string &dib, DibInfo &info)
{
  if (dib.size() < kDibHeaderSize) {
    return false;
  }

  const auto *raw = reinterpret_cast<const quint8 *>(dib.data());
  // only the classic BITMAPINFOHEADER that the internal clipboard contract uses
  if (qFromLittleEndian<quint32>(raw) != kDibHeaderSize) {
    return false;
  }

  info.width = qFromLittleEndian<qint32>(raw + 4);
  info.height = qFromLittleEndian<qint32>(raw + 8);
  info.bitCount = qFromLittleEndian<quint16>(raw + 14);
  info.compression = qFromLittleEndian<quint32>(raw + 16);

  if (info.width <= 0 || info.height == 0 || info.compression != kBiRgb ||
      (info.bitCount != 24 && info.bitCount != 32) || static_cast<size_t>(info.width) > kMaxDimension) {
    return false;
  }

  info.rows = static_cast<size_t>(info.height < 0 ? -static_cast<qint64>(info.height) : info.height);
  if (info.rows == 0 || info.rows > kMaxDimension) {
    return false;
  }

  info.srcStride = ((static_cast<size_t>(info.width) * info.bitCount + 31) / 32) * 4;
  info.pixelBytes = dib.size() - kDibHeaderSize;
  if (info.srcStride > info.pixelBytes / info.rows) {
    return false;
  }

  info.pixels = raw + kDibHeaderSize;
  return true;
}

//! Expand a DIB into a top-down BGRA buffer (CGImage row order), force opaque alpha
std::vector<quint8> dibToBgra(const DibInfo &info)
{
  const auto width = static_cast<size_t>(info.width);
  std::vector<quint8> bgra(width * info.rows * 4);

  for (size_t row = 0; row < info.rows; ++row) {
    const size_t srcRow = info.height < 0 ? row : info.rows - 1 - row;
    const auto *src = info.pixels + srcRow * info.srcStride;
    auto *dst = bgra.data() + row * width * 4;
    for (size_t col = 0; col < width; ++col) {
      const auto *px = src + col * (info.bitCount / 8);
      dst[col * 4 + 0] = px[0];
      dst[col * 4 + 1] = px[1];
      dst[col * 4 + 2] = px[2];
      dst[col * 4 + 3] = 0xFF;
    }
  }

  return bgra;
}

CGImageRef createImageFromBgra(std::vector<quint8> bgra, size_t width, size_t height)
{
  // keep the buffer alive for the lifetime of the provider
  auto *storage = new std::vector<quint8>(std::move(bgra));
  CGDataProviderRef provider = CGDataProviderCreateWithData(
      storage, storage->data(), storage->size(),
      [](void *info, const void * /*data*/, size_t /*size*/) { delete static_cast<std::vector<quint8> *>(info); }
  );
  if (provider == nullptr) {
    delete storage;
    return nullptr;
  }

  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  CGImageRef image = CGImageCreate(
      width, height, 8, 32, width * 4, colorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, provider,
      nullptr, false, kCGRenderingIntentDefault
  );
  CGColorSpaceRelease(colorSpace);
  CGDataProviderRelease(provider);
  return image;
}

} // namespace

OSXClipboardImageConverter::OSXClipboardImageConverter(CFStringRef type) : m_type(type)
{
  // CFSTR constants live for the lifetime of the process, no retain needed
}

IClipboard::Format OSXClipboardImageConverter::getFormat() const
{
  return IClipboard::Format::Bitmap;
}

CFStringRef OSXClipboardImageConverter::getOSXFormat() const
{
  return m_type;
}

std::string OSXClipboardImageConverter::fromIClipboard(const std::string &dib) const
{
  DibInfo info;
  if (!parseDib(dib, info)) {
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=invalid-dib size=%zu", dib.size());
    return std::string();
  }

  const auto width = static_cast<size_t>(info.width);
  auto bgra = dibToBgra(info);
  CGImageRef image = createImageFromBgra(std::move(bgra), width, info.rows);
  if (image == nullptr) {
    LOG_WARN("clipboard: failed to create image for png encoding");
    return std::string();
  }

  CFMutableDataRef png = CFDataCreateMutable(kCFAllocatorDefault, 0);
  CGImageDestinationRef destination = CGImageDestinationCreateWithData(png, m_type, 1, nullptr);
  if (destination == nullptr) {
    CFRelease(png);
    CGImageRelease(image);
    LOG_WARN("clipboard: failed to create png destination");
    return std::string();
  }

  CGImageDestinationAddImage(destination, image, nullptr);
  const bool encoded = CGImageDestinationFinalize(destination);
  CFRelease(destination);
  CGImageRelease(image);

  std::string result;
  if (encoded) {
    result.assign(reinterpret_cast<const char *>(CFDataGetBytePtr(png)), static_cast<size_t>(CFDataGetLength(png)));
  }
  CFRelease(png);

  if (!encoded) {
    LOG_WARN("clipboard: failed to encode png");
  }
  return result;
}

std::string OSXClipboardImageConverter::toIClipboard(const std::string &png) const
{
  if (png.empty()) {
    return std::string();
  }

  CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(png.data()), png.size());
  if (data == nullptr) {
    return std::string();
  }

  CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
  CFRelease(data);
  if (source == nullptr) {
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=invalid-png size=%zu", png.size());
    return std::string();
  }

  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (image == nullptr) {
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=png-decode-failed");
    return std::string();
  }

  const size_t width = CGImageGetWidth(image);
  const size_t height = CGImageGetHeight(image);
  if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension) {
    CGImageRelease(image);
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=unsupported-size %zux%zu", width, height);
    return std::string();
  }

  std::vector<quint8> bgra(width * height * 4);
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(
      bgra.data(), width, height, 8, width * 4, colorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little
  );
  CGColorSpaceRelease(colorSpace);
  if (context == nullptr) {
    CGImageRelease(image);
    LOG_WARN("clipboard: failed to create bitmap context for png decoding");
    return std::string();
  }

  CGContextDrawImage(context, CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)), image);
  CGContextRelease(context);
  CGImageRelease(image);

  // build the canonical DIB: BITMAPINFOHEADER, 32bpp BI_RGB, bottom-up rows
  std::string dib(kDibHeaderSize + bgra.size(), '\0');
  auto *out = reinterpret_cast<quint8 *>(dib.data());
  qToLittleEndian<quint32>(kDibHeaderSize, out + 0);
  qToLittleEndian<qint32>(static_cast<qint32>(width), out + 4);
  qToLittleEndian<qint32>(static_cast<qint32>(height), out + 8);
  qToLittleEndian<quint16>(1, out + 12);
  qToLittleEndian<quint16>(32, out + 14);
  qToLittleEndian<quint32>(kBiRgb, out + 16);
  qToLittleEndian<quint32>(static_cast<quint32>(bgra.size()), out + 20);

  for (size_t row = 0; row < height; ++row) {
    const auto *src = bgra.data() + row * width * 4;
    auto *dst = out + kDibHeaderSize + (height - 1 - row) * width * 4;
    std::memcpy(dst, src, width * 4);
    for (size_t col = 0; col < width; ++col) {
      dst[col * 4 + 3] = 0xFF;
    }
  }

  LOG_DEBUG("clipboard: decoded png %zux%zu into dib %zu bytes", width, height, dib.size());
  return dib;
}
