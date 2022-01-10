/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <mutex>
#include "include/core/SkTypes.h"
#include "include/private/SkMacros.h"

namespace pk {
class SkMutex : public std::mutex {
 public:
  constexpr SkMutex() = default;

  void acquire() {
    lock();
  }

  void release() {
    unlock();
  }
};

class SkAutoMutexExclusive {
 public:
  SkAutoMutexExclusive(SkMutex& mutex) : fMutex(mutex) {
    fMutex.acquire();
  }
  ~SkAutoMutexExclusive() {
    fMutex.release();
  }

  SkAutoMutexExclusive(const SkAutoMutexExclusive&) = delete;
  SkAutoMutexExclusive(SkAutoMutexExclusive&&) = delete;

  SkAutoMutexExclusive& operator=(const SkAutoMutexExclusive&) = delete;
  SkAutoMutexExclusive& operator=(SkAutoMutexExclusive&&) = delete;

 private:
  SkMutex& fMutex;
};
}  // namespace pk
