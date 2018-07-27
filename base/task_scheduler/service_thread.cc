// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/service_thread.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/debug/alias.h"
#include "base/rand_util.h"
#include "base/stl_util.h"
#include "base/task_scheduler/post_task.h"
#include "base/task_scheduler/task_scheduler.h"
#include "base/task_scheduler/task_tracker.h"
#include "base/task_scheduler/task_traits.h"
#include "build/build_config.h"

namespace base {
namespace internal {

namespace {

// TODO(gab): Verify whether this attribute addresses https://crbug.com/848255
// and consider a way to expose it more widely.
#if defined(__clang__) && !defined(OS_NACL)
[[clang::require_constant_initialization]]
#endif
    TimeDelta g_heartbeat_for_testing = TimeDelta();

}  // namespace

ServiceThread::ServiceThread(const TaskTracker* task_tracker)
    : Thread("TaskSchedulerServiceThread"), task_tracker_(task_tracker) {}

// static
void ServiceThread::SetHeartbeatIntervalForTesting(TimeDelta heartbeat) {
  g_heartbeat_for_testing = heartbeat;
}

void ServiceThread::Init() {
  // In unit tests we sometimes do not have a fully functional TaskScheduler
  // environment, do not perform the heartbeat report in that case since it
  // relies on such an environment.
  if (task_tracker_ && TaskScheduler::GetInstance()) {
    // Compute the histogram every hour (with a slight offset to drift if that
    // hour tick happens to line up with specific events). Once per hour per
    // user was deemed sufficient to gather a reliable metric.
    constexpr TimeDelta kHeartbeat = TimeDelta::FromMinutes(59);

    heartbeat_latency_timer_.Start(
        FROM_HERE,
        g_heartbeat_for_testing.is_zero() ? kHeartbeat
                                          : g_heartbeat_for_testing,
        BindRepeating(&ServiceThread::PerformHeartbeatLatencyReport,
                      Unretained(this)));
  }
}

NOINLINE void ServiceThread::Run(RunLoop* run_loop) {
  const int line_number = __LINE__;
  Thread::Run(run_loop);
  base::debug::Alias(&line_number);
}

void ServiceThread::PerformHeartbeatLatencyReport() const {
  static constexpr TaskTraits kReportedTraits[] = {
      {TaskPriority::BEST_EFFORT},   {TaskPriority::BEST_EFFORT, MayBlock()},
      {TaskPriority::USER_VISIBLE},  {TaskPriority::USER_VISIBLE, MayBlock()},
      {TaskPriority::USER_BLOCKING}, {TaskPriority::USER_BLOCKING, MayBlock()}};

  // Only record latency for one set of TaskTraits per report to avoid bias in
  // the order in which tasks are posted (should we record all at once) as well
  // as to avoid spinning up many worker threads to process this report if the
  // scheduler is currently idle (each pool keeps at least one idle thread so a
  // single task isn't an issue).

  // Invoke RandInt() out-of-line to ensure it's obtained before
  // TimeTicks::Now().
  const TaskTraits& profiled_traits =
      kReportedTraits[RandInt(0, base::size(kReportedTraits) - 1)];

  // Post through the static API to time the full stack. Use a new Now() for
  // every set of traits in case PostTaskWithTraits() itself is slow.
  // Bonus: this appraoch also includes the overhead of Bind() in the reported
  // latency).
  base::PostTaskWithTraits(
      FROM_HERE, profiled_traits,
      BindOnce(&TaskTracker::RecordLatencyHistogram, Unretained(task_tracker_),
               TaskTracker::LatencyHistogramType::HEARTBEAT_LATENCY,
               profiled_traits, TimeTicks::Now()));
}

}  // namespace internal
}  // namespace base
