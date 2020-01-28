// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_THREADING_HANG_WATCHER_H_
#define BASE_THREADING_HANG_WATCHER_H_

#include <exception>
#include <memory>

#include "base/atomicops.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/threading/thread_checker.h"
#include "base/threading/thread_local.h"
#include "base/time/time.h"

namespace base {
class HangWatchScope;
namespace internal {
class HangWatchState;
}  // namespace internal
}  // namespace base

namespace base {

// Instantiate of HangWatchScope in a scope to register it to be
// watched for hangs of more than |timeout| by the HangWatcher.
//
// Example usage:
//
//  void FooBar(){
//    HangWatchScope scope(base::TimeDelta::FromSeconds(5));
//    DoSomeWork();
//  }
//
// If DoSomeWork() takes more than 5s to run and the HangWatcher
// inspects the thread state before Foobar returns a hang will be
// reported. Instances of this object should live on the stack only as they are
// intrinsicaly linked to the execution scopes that contain them.
// Keeping a HangWatchScope alive after the scope in which it was created has
// exited would lead to non-actionnable hang reports.
class BASE_EXPORT HangWatchScope {
 public:
  // Constructing/destructing thread must be the same thread.
  explicit HangWatchScope(TimeDelta timeout);
  ~HangWatchScope();

  HangWatchScope(const HangWatchScope&) = delete;
  HangWatchScope& operator=(const HangWatchScope&) = delete;

 private:
  // This object should always be constructed and destructed on the same thread.
  THREAD_CHECKER(thread_checker_);

  // The deadline set by the previous HangWatchScope created on this thread.
  // Stored so it can be reset when this HangWatchScope is destroyed.
  TimeTicks previous_deadline_;

#if DCHECK_IS_ON()
  // The previous HangWatchScope created on this thread.
  HangWatchScope* previous_scope_;
#endif
};

// Monitors registered threads for hangs by inspecting their associated
// HangWatchStates for deadline overruns. Only one instance of HangWatcher can
// exist at a time.
class BASE_EXPORT HangWatcher {
 public:
  // The first invocation of the constructor will set the global instance
  // accessible through GetInstance(). This means that only one instance can
  // exist at a time.
  explicit HangWatcher(RepeatingClosure on_hang_closure);

  // Clears the global instance for the class.
  ~HangWatcher();

  HangWatcher(const HangWatcher&) = delete;
  HangWatcher& operator=(const HangWatcher&) = delete;

  // Returns a non-owning pointer to the global HangWatcher instance.
  static HangWatcher* GetInstance();

  // Sets up the calling thread to be monitored for threads. Returns a
  // ScopedClosureRunner that unregisters the thread. This closure has to be
  // called from the registered thread before it's joined.
  ScopedClosureRunner RegisterThread() WARN_UNUSED_RESULT;

  // Inspects the state of all registered threads to check if they are hung.
  void Monitor();

 private:
  // Stops hang watching on the calling thread by removing the entry from the
  // watch list.
  void UnregisterThread();

  const RepeatingClosure on_hang_closure_;
  Lock watch_state_lock_;

  std::vector<std::unique_ptr<internal::HangWatchState>> watch_states_
      GUARDED_BY(watch_state_lock_);
};

// Classes here are exposed in the header only for testing. They are not
// intended to be used outside of base.
namespace internal {

// Contains the information necessary for hang watching a specific
// thread. Instances of this class are accessed concurrently by the associated
// thread and the HangWatcher. The HangWatcher owns instances of this
// class and outside of it they are accessed through
// GetHangWatchStateForCurrentThread().
class BASE_EXPORT HangWatchState {
 public:
  HangWatchState();
  ~HangWatchState();

  HangWatchState(const HangWatchState&) = delete;
  HangWatchState& operator=(const HangWatchState&) = delete;

  // Allocates a new state object bound to the calling thread and returns an
  // owning pointer to it.
  static std::unique_ptr<HangWatchState> CreateHangWatchStateForCurrentThread();

  // Retrieves the hang watch state associated with the calling thread.
  // Returns nullptr if no HangWatchState exists for the current thread (see
  // CreateHangWatchStateForCurrentThread()).
  static ThreadLocalPointer<HangWatchState>*
  GetHangWatchStateForCurrentThread();

  // Returns the value of the current deadline. Use this function if you need to
  // store the value. To test if the deadline has expired use IsOverDeadline().
  TimeTicks GetDeadline() const;

  // Atomically sets the deadline to a new value and return the previous value.
  TimeTicks SetDeadline(TimeTicks deadline);

  // Tests whether the associated thread's execution has gone over the deadline.
  bool IsOverDeadline() const;

#if DCHECK_IS_ON()
  // Saves the supplied HangWatchScope as the currently active scope.
  void SetCurrentHangWatchScope(HangWatchScope* scope);

  // Retrieve the currently active scope.
  HangWatchScope* GetCurrentHangWatchScope();
#endif

 private:
  // The thread that creates the instance should be the class that updates
  // the deadline.
  THREAD_CHECKER(thread_checker_);

  // If the deadline fails to be updated before TimeTicks::Now() ever
  // reaches the value contained in it this constistutes a hang.
  std::atomic<TimeTicks> deadline_;

#if DCHECK_IS_ON()
  // Used to keep track of the current HangWatchScope and detect improper usage.
  // Scopes should always be destructed in reverse order from the one they were
  // constructed in. Example of improper use:
  //
  // {
  //   std::unique_ptr<Scope> scope = std::make_unique<Scope>(...);
  //   Scope other_scope;
  //   |scope| gets deallocated first, violating reverse destruction order.
  //   scope.reset();
  // }
  HangWatchScope* current_hang_watch_scope_{nullptr};
#endif
};

}  // namespace internal
}  // namespace base

#endif  // BASE_THREADING_HANG_WATCHER_H_
