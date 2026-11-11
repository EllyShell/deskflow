/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboardFileImageConverter.h"

#include "base/Log.h"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <QtEndian>

#include <cmath>
#include <cstring>
#include <vector>

namespace {

constexpr quint32 kDibHeaderSize = 40;
constexpr quint32 kBiRgb = 0;
constexpr size_t kMaxDimension = 32768;

void putU16(quint8 *out, size_t offset, quint16 value)
{
  qToLittleEndian<quint16>(value, out + offset);
}

void putU32(quint8 *out, size_t offset, quint32 value)
{
  qToLittleEndian<quint32>(value, out + offset);
}

//! Render the first image of the file into the canonical bottom-up 32bpp DIB
bool fileUrlToDib(const std::string &url, std::string &dib)
{
  CFURLRef fileUrl =
      CFURLCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(url.data()), url.size(), kCFStringEncodingUTF8, nullptr);
  if (fileUrl == nullptr) {
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=invalid-file-url");
    return false;
  }

  CGImageSourceRef source = CGImageSourceCreateWithURL(fileUrl, nullptr);
  CFRelease(fileUrl);
  if (source == nullptr) {
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=not-an-image-file");
    return false;
  }

  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (image == nullptr) {
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=file-decode-failed");
    return false;
  }

  const size_t width = CGImageGetWidth(image);
  const size_t height = CGImageGetHeight(image);
  if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension) {
    CGImageRelease(image);
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=unsupported-size %zux%zu", width, height);
    return false;
  }

  const size_t stride = width * 4;
  std::vector<quint8> pixels(stride * height, 0);

  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  // kCGBitmapByteOrder32Little + premultipliedFirst is BGRA in memory; the
  // context has its origin at the bottom left, so row 0 is the bottom row,
  // which is exactly the bottom-up order the internal DIB uses.
  CGContextRef context = CGBitmapContextCreate(
      pixels.data(), width, height, 8, stride, colorSpace,
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little
  );
  CGColorSpaceRelease(colorSpace);
  if (context == nullptr) {
    CGImageRelease(image);
    return false;
  }

  CGContextDrawImage(context, CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)), image);
  CGContextRelease(context);
  CGImageRelease(image);

  dib.assign(kDibHeaderSize + pixels.size(), '\0');
  auto *out = reinterpret_cast<quint8 *>(dib.data());
  putU32(out, 0, kDibHeaderSize);
  putU32(out, 4, static_cast<quint32>(width));
  putU32(out, 8, static_cast<quint32>(height));
  putU16(out, 12, 1);
  putU16(out, 14, 32);
  putU32(out, 16, kBiRgb);
  putU32(out, 20, static_cast<quint32>(pixels.size()));

  auto *dst = out + kDibHeaderSize;
  // CGBitmapContext keeps row 0 at the top of the image, the internal DIB is
  // bottom-up, so the rows have to be reversed
  for (size_t row = 0; row < height; ++row) {
    const auto *src = pixels.data() + (height - 1 - row) * stride;
    std::memcpy(dst + row * stride, src, stride);
  }
  for (size_t i = 3; i < pixels.size(); i += 4) {
    dst[i] = 0xFF;
  }

  LOG_DEBUG("clipboard: decoded image file %zux%zu into dib %zu bytes", width, height, dib.size());
  return true;
}

} // namespace

IClipboard::Format OSXClipboardFileImageConverter::getFormat() const
{
  return IClipboard::Format::Bitmap;
}

CFStringRef OSXClipboardFileImageConverter::getOSXFormat() const
{
  return CFSTR("public.file-url");
}

std::string OSXClipboardFileImageConverter::fromIClipboard(const std::string &) const
{
  // the internal clipboard never asks for a file url
  return std::string();
}

std::string OSXClipboardFileImageConverter::toIClipboard(const std::string &url) const
{
  if (url.empty()) {
    return std::string();
  }

  std::string dib;
  if (!fileUrlToDib(url, dib)) {
    return std::string();
  }
  return dib;
}
