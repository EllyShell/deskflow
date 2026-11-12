/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2022 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <Carbon/Carbon.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <memory>
#include <mutex>
#include <type_traits>

using CFDeallocator = decltype(&CFRelease);
using AutoCFArray = std::unique_ptr<const __CFArray, CFDeallocator>;
using AutoCFDictionary = std::unique_ptr<const __CFDictionary, CFDeallocator>;
using AutoTISInputSourceRef = std::unique_ptr<__TISInputSource, CFDeallocator>;

inline std::mutex g_tisMutex;

namespace deskflow::macos {

// HIToolbox may assert on macOS 26 when a TIS input-source query runs outside
// the main queue. Keep the call synchronous so callers retain their current
// input-source selection and don't need to share mutable state across threads.
template <typename Callback> void runTISOnMainQueue(Callback &&callback)
{
  if (pthread_main_np() != 0) {
    callback();
    return;
  }

  using CallbackType = std::remove_reference_t<Callback>;
  dispatch_sync_f(dispatch_get_main_queue(), std::addressof(callback), [](void *context) {
    (*static_cast<CallbackType *>(context))();
  });
}

} // namespace deskflow::macos
