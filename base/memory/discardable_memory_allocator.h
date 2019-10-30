// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_DISCARDABLE_MEMORY_ALLOCATOR_H_
#define BASE_MEMORY_DISCARDABLE_MEMORY_ALLOCATOR_H_

#include <stddef.h>

#include <memory>

#include "base/base_export.h"
#include "base/macros.h"

namespace base {
class DiscardableMemory;

// An allocator which creates and manages DiscardableMemory. The allocator
// itself should be created via CreateDiscardableMemoryAllocator, which
// selects an appropriate implementation depending on platform support.
class BASE_EXPORT DiscardableMemoryAllocator {
 public:
  DiscardableMemoryAllocator() = default;
  virtual ~DiscardableMemoryAllocator() = default;

  // Returns the allocator instance.
  static DiscardableMemoryAllocator* GetInstance();

  // Sets the allocator instance. Can only be called once, e.g. on startup.
  // Ownership of |instance| remains with the caller.
  static void SetInstance(DiscardableMemoryAllocator* allocator);

  // Creates an initially-locked instance of discardable memory.
  // If the platform supports Android ashmem or madvise(MADV_FREE),
  // platform-specific techniques will be used to discard memory under pressure.
  // Otherwise, discardable memory is emulated and manually discarded
  // heuristicly (via memory pressure notifications).
  virtual std::unique_ptr<DiscardableMemory> AllocateLockedDiscardableMemory(
      size_t size) = 0;

  // Gets the total number of bytes allocated by this allocator which have not
  // been discarded.
  virtual size_t GetBytesAllocated() const = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(DiscardableMemoryAllocator);
};

}  // namespace base

#endif  // BASE_MEMORY_DISCARDABLE_MEMORY_ALLOCATOR_H_
