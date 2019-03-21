// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/sequence.h"

#include <utility>

#include "base/critical_closure.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/task/task_features.h"
#include "base/time/time.h"

namespace base {
namespace internal {

SequenceAndTransaction::SequenceAndTransaction(
    scoped_refptr<Sequence> sequence_in,
    Sequence::Transaction transaction_in)
    : sequence(std::move(sequence_in)),
      transaction(std::move(transaction_in)) {}

SequenceAndTransaction::SequenceAndTransaction(SequenceAndTransaction&& other) =
    default;

SequenceAndTransaction::~SequenceAndTransaction() = default;

Sequence::Transaction::Transaction(Sequence* sequence)
    : TaskSource::Transaction(sequence) {}

Sequence::Transaction::Transaction(Sequence::Transaction&& other) = default;

Sequence::Transaction::~Transaction() = default;

bool Sequence::Transaction::PushTask(Task task) {
  // Use CHECK instead of DCHECK to crash earlier. See http://crbug.com/711167
  // for details.
  CHECK(task.task);
  DCHECK(task.queue_time.is_null());

  // AddRef() matched by manual Release() when the queue becomes empty again
  // (in DidRunTask() or Clear()).
  if (sequence()->queue_.empty() && sequence()->task_runner())
    sequence()->task_runner()->AddRef();

  task.queue_time = base::TimeTicks::Now();

  task.task = sequence()->traits_.shutdown_behavior() ==
                      TaskShutdownBehavior::BLOCK_SHUTDOWN
                  ? MakeCriticalClosure(std::move(task.task))
                  : std::move(task.task);

  sequence()->queue_.push(std::move(task));

  // If the sequence was empty before |task| was inserted into it and the pool
  // is not running any task from this sequence, it should be queued.
  // Otherwise, one of these must be true:
  // - The Sequence is already scheduled, or,
  // - The pool is running a Task from the Sequence. The pool is expected to
  //   reschedule the Sequence once it's done running the Task.
  return sequence()->queue_.size() == 1 && NeedsWorker();
}

Optional<Task> Sequence::TakeTask() {
  DCHECK(!IsEmpty());
  DCHECK(queue_.front().task);

  auto next_task = std::move(queue_.front());
  queue_.pop();
  return std::move(next_task);
}

bool Sequence::DidRunTask() {
  if (queue_.empty())
    ReleaseTaskRunner();
  return !queue_.empty();
}

SequenceSortKey Sequence::GetSortKey() const {
  DCHECK(!IsEmpty());
  return SequenceSortKey(traits_.priority(), queue_.front().queue_time);
}

bool Sequence::IsEmpty() const {
  return queue_.empty();
}

void Sequence::Clear() {
  bool queue_was_empty = queue_.empty();
  while (!queue_.empty())
    TakeTask();
  if (!queue_was_empty) {
    // No member access after this point, ReleaseTaskRunner() might have deleted
    // |this|.
    ReleaseTaskRunner();
  }
}

void Sequence::ReleaseTaskRunner() {
  if (!task_runner())
    return;
  if (execution_mode() == TaskSourceExecutionMode::kParallel) {
    static_cast<SchedulerParallelTaskRunner*>(task_runner())
        ->UnregisterSequence(this);
  }
  // No member access after this point, releasing |task_runner()| might delete
  // |this|.
  task_runner()->Release();
}

Sequence::Sequence(const TaskTraits& traits,
                   TaskRunner* task_runner,
                   TaskSourceExecutionMode execution_mode)
    : TaskSource(traits, task_runner, execution_mode) {}

Sequence::~Sequence() = default;

Sequence::Transaction Sequence::BeginTransaction() {
  return Transaction(this);
}

ExecutionEnvironment Sequence::GetExecutionEnvironment() {
  return {token_, &sequence_local_storage_};
}

// static
SequenceAndTransaction SequenceAndTransaction::FromSequence(
    scoped_refptr<Sequence> sequence) {
  DCHECK(sequence);
  Sequence::Transaction transaction(sequence->BeginTransaction());
  return SequenceAndTransaction(std::move(sequence), std::move(transaction));
}

}  // namespace internal
}  // namespace base
