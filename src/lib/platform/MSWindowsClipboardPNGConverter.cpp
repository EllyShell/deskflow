/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardPNGConverter.h"

#include "base/Log.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t kDibHeaderSize = 40;
constexpr uint32_t kBiRgb = 0;
constexpr size_t kMaxDimension = 32768;

bool ensureCom()
{
  static const bool ready = []() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
  }();
  return ready;
}

bool createFactory(ComPtr<IWICImagingFactory> &factory)
{
  if (!ensureCom()) {
    return false;
  }
  return SUCCEEDED(CoCreateInstance(
      CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())
  ));
}

void putU16(uint8_t *out, size_t offset, uint16_t value)
{
  std::memcpy(out + offset, &value, sizeof(value));
}

void putU32(uint8_t *out, size_t offset, uint32_t value)
{
  std::memcpy(out + offset, &value, sizeof(value));
}

bool decodePngToDib(const std::string &png, std::string &dib)
{
  if (png.empty() || png.size() > std::numeric_limits<DWORD>::max()) {
    return false;
  }

  ComPtr<IWICImagingFactory> factory;
  if (!createFactory(factory)) {
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=wic-factory");
    return false;
  }

  ComPtr<IWICStream> stream;
  if (FAILED(factory->CreateStream(stream.GetAddressOf())) ||
      FAILED(stream->InitializeFromMemory(
          reinterpret_cast<BYTE *>(const_cast<char *>(png.data())), static_cast<DWORD>(png.size())
      ))) {
    return false;
  }

  ComPtr<IWICBitmapDecoder> decoder;
  if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf()))) {
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=png-decode-failed");
    return false;
  }

  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) {
    return false;
  }

  ComPtr<IWICFormatConverter> converter;
  if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
      FAILED(converter->Initialize(
          frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom
      ))) {
    return false;
  }

  UINT width = 0;
  UINT height = 0;
  if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 || width > kMaxDimension ||
      height > kMaxDimension) {
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=unsupported-size");
    return false;
  }

  const size_t stride = static_cast<size_t>(width) * 4;
  if (static_cast<size_t>(height) > std::numeric_limits<size_t>::max() / stride) {
    return false;
  }
  const size_t pixelBytes = stride * height;
  if (pixelBytes > std::numeric_limits<UINT>::max()) {
    return false;
  }

  std::vector<BYTE> pixels(pixelBytes);
  if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(pixelBytes), pixels.data()))) {
    return false;
  }

  dib.assign(kDibHeaderSize + pixelBytes, '\0');
  auto *out = reinterpret_cast<uint8_t *>(dib.data());
  putU32(out, 0, kDibHeaderSize);
  putU32(out, 4, width);
  putU32(out, 8, height);
  putU16(out, 12, 1);
  putU16(out, 14, 32);
  putU32(out, 16, kBiRgb);
  putU32(out, 20, static_cast<uint32_t>(pixelBytes));

  // WIC rows are top-down, the internal DIB is bottom-up
  for (UINT row = 0; row < height; ++row) {
    const uint8_t *src = pixels.data() + static_cast<size_t>(row) * stride;
    uint8_t *dst = out + kDibHeaderSize + static_cast<size_t>(height - 1 - row) * stride;
    std::memcpy(dst, src, stride);
    for (UINT col = 0; col < width; ++col) {
      dst[col * 4 + 3] = 0xFF;
    }
  }

  LOG_DEBUG("clipboard: decoded png %ux%u into dib %zu bytes", width, height, dib.size());
  return true;
}

bool encodeDibToPng(const std::string &dib, std::string &png)
{
  if (dib.size() < kDibHeaderSize) {
    return false;
  }

  const auto *raw = reinterpret_cast<const uint8_t *>(dib.data());
  uint32_t headerSize = 0;
  int32_t width = 0;
  int32_t height = 0;
  uint16_t bitCount = 0;
  uint32_t compression = 0;
  std::memcpy(&headerSize, raw + 0, 4);
  std::memcpy(&width, raw + 4, 4);
  std::memcpy(&height, raw + 8, 4);
  std::memcpy(&bitCount, raw + 14, 2);
  std::memcpy(&compression, raw + 16, 4);

  if (headerSize != kDibHeaderSize || width <= 0 || height == 0 || compression != kBiRgb ||
      (bitCount != 24 && bitCount != 32) || static_cast<size_t>(width) > kMaxDimension) {
    LOG_DEBUG("clipboard: dropped reason=unsupported format=bitmap detail=invalid-dib size=%zu", dib.size());
    return false;
  }

  const size_t columns = static_cast<size_t>(width);
  const size_t rows = static_cast<size_t>(std::abs(static_cast<int64_t>(height)));
  if (rows == 0 || rows > kMaxDimension) {
    return false;
  }

  const size_t srcStride = ((columns * bitCount + 31) / 32) * 4;
  if (dib.size() < kDibHeaderSize + srcStride * rows) {
    return false;
  }

  std::vector<BYTE> bgra(columns * rows * 4);
  for (size_t row = 0; row < rows; ++row) {
    const size_t srcRow = height < 0 ? row : rows - 1 - row;
    const uint8_t *src = raw + kDibHeaderSize + srcRow * srcStride;
    uint8_t *dst = bgra.data() + row * columns * 4;
    for (size_t col = 0; col < columns; ++col) {
      const uint8_t *px = src + col * (bitCount / 8);
      dst[col * 4 + 0] = px[0];
      dst[col * 4 + 1] = px[1];
      dst[col * 4 + 2] = px[2];
      dst[col * 4 + 3] = 0xFF;
    }
  }

  ComPtr<IWICImagingFactory> factory;
  if (!createFactory(factory)) {
    return false;
  }

  ComPtr<IStream> memStream;
  if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, memStream.GetAddressOf()))) {
    return false;
  }

  ComPtr<IWICBitmapEncoder> encoder;
  if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
      FAILED(encoder->Initialize(memStream.Get(), WICBitmapEncoderNoCache))) {
    return false;
  }

  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> properties;
  if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf())) ||
      FAILED(frame->Initialize(properties.Get()))) {
    return false;
  }

  frame->SetSize(static_cast<UINT>(columns), static_cast<UINT>(rows));
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  if (FAILED(frame->SetPixelFormat(&format))) {
    return false;
  }

  const size_t pixelBytes = bgra.size();
  if (pixelBytes > std::numeric_limits<UINT>::max()) {
    return false;
  }
  if (FAILED(frame->WritePixels(static_cast<UINT>(rows), static_cast<UINT>(columns * 4), static_cast<UINT>(pixelBytes), bgra.data())) ||
      FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
    return false;
  }

  STATSTG stat{};
  if (FAILED(memStream->Stat(&stat, STATFLAG_NONAME))) {
    return false;
  }
  const ULONG size = stat.cbSize.LowPart;
  LARGE_INTEGER start{};
  if (FAILED(memStream->Seek(start, STREAM_SEEK_SET, nullptr))) {
    return false;
  }

  png.resize(size);
  ULONG read = 0;
  if (FAILED(memStream->Read(png.data(), size, &read))) {
    return false;
  }
  png.resize(read);
  return !png.empty();
}

} // namespace

MSWindowsClipboardPNGConverter::MSWindowsClipboardPNGConverter()
    : m_format(RegisterClipboardFormatW(L"PNG"))
{
}

IClipboard::Format MSWindowsClipboardPNGConverter::getFormat() const
{
  return IClipboard::Format::Bitmap;
}

UINT MSWindowsClipboardPNGConverter::getWin32Format() const
{
  return m_format;
}

HANDLE
MSWindowsClipboardPNGConverter::fromIClipboard(const std::string &data) const
{
  std::string png;
  if (!encodeDibToPng(data, png) || png.empty()) {
    return nullptr;
  }

  HGLOBAL gData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, png.size());
  if (gData == nullptr) {
    return nullptr;
  }

  auto *dst = static_cast<char *>(GlobalLock(gData));
  if (dst == nullptr) {
    GlobalFree(gData);
    return nullptr;
  }
  std::memcpy(dst, png.data(), png.size());
  GlobalUnlock(gData);
  return gData;
}

std::string MSWindowsClipboardPNGConverter::toIClipboard(HANDLE data) const
{
  const auto *src = static_cast<const char *>(GlobalLock(data));
  if (src == nullptr) {
    return std::string();
  }

  const auto size = static_cast<size_t>(GlobalSize(data));
  std::string result;
  if (decodePngToDib(std::string(src, size), result)) {
    GlobalUnlock(data);
    return result;
  }

  GlobalUnlock(data);
  return std::string();
}
