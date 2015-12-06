// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/system/routed_raw_channel.h"

#include "base/bind.h"
#include "base/logging.h"
#include "mojo/edk/embedder/embedder_internal.h"
#include "mojo/edk/system/message_pipe_dispatcher.h"

namespace mojo {
namespace edk {

namespace {
const uint64_t kInternalRoutingId = 0;

// These are messages sent over our internal routing id above, meant for the
// other side's RoutedRawChannel to dispatch.
enum InternalMessages {
  ROUTE_CLOSED = 0,
};
}

RoutedRawChannel::PendingMessage::PendingMessage() {
}

RoutedRawChannel::PendingMessage::~PendingMessage() {
}

RoutedRawChannel::RoutedRawChannel(
    ScopedPlatformHandle handle,
    const base::Callback<void(RoutedRawChannel*)>& destruct_callback)
    : channel_(RawChannel::Create(handle.Pass())),
      destruct_callback_(destruct_callback) {
  internal::g_io_thread_task_runner->PostTask(
      FROM_HERE,
      base::Bind(&RawChannel::Init, base::Unretained(channel_), this));
  internal::g_io_thread_task_runner->PostTask(
      FROM_HERE,
      base::Bind(&RawChannel::EnsureLazyInitialized,
                  base::Unretained(channel_)));
}

void RoutedRawChannel::AddRoute(uint64_t pipe_id, MessagePipeDispatcher* pipe) {
  CHECK_NE(pipe_id, kInternalRoutingId) << kInternalRoutingId << " is reserved";
  base::AutoLock auto_lock(lock_);
  CHECK(routes_.find(pipe_id) == routes_.end());
  routes_[pipe_id] = pipe;

  for (size_t i = 0; i < pending_messages_.size();) {
    MessageInTransit::View view(pending_messages_[i]->message.size(),
                                &pending_messages_[i]->message[0]);
    if (view.route_id() == pipe_id) {
      pipe->OnReadMessage(view, pending_messages_[i]->handles.Pass());
      pending_messages_.erase(pending_messages_.begin() + i);
    } else {
      ++i;
    }
  }

  if (close_routes_.find(pipe_id) != close_routes_.end())
    pipe->OnError(ERROR_READ_SHUTDOWN);
}

void RoutedRawChannel::RemoveRoute(uint64_t pipe_id,
                                   MessagePipeDispatcher* pipe) {
  base::AutoLock auto_lock(lock_);
  CHECK(routes_.find(pipe_id) != routes_.end());
  CHECK_EQ(routes_[pipe_id], pipe);
  routes_.erase(pipe_id);

  // Only send a message to the other side to close the route if we hadn't
  // received a close route message. Otherwise they would keep going back and
  // forth.
  if (close_routes_.find(pipe_id) != close_routes_.end()) {
    close_routes_.erase(pipe_id);
  } else if (channel_) {
    // Default route id of 0 to reach the other side's RoutedRawChannel.
    char message_data[sizeof(char) + sizeof(uint64_t)];
    message_data[0] = ROUTE_CLOSED;
    memcpy(&message_data[1], &pipe_id, sizeof(uint64_t));
    scoped_ptr<MessageInTransit> message(new MessageInTransit(
        MessageInTransit::Type::MESSAGE, arraysize(message_data),
          message_data));
    message->set_route_id(kInternalRoutingId);
    channel_->WriteMessage(message.Pass());
  }

  if (!channel_ && routes_.empty()) {
    // PostTask to avoid reentrancy since the broker might be calling us.
    base::MessageLoop::current()->DeleteSoon(FROM_HERE, this);
  }
}

RoutedRawChannel::~RoutedRawChannel() {
  destruct_callback_.Run(this);
}

void RoutedRawChannel::OnReadMessage(
    const MessageInTransit::View& message_view,
    ScopedPlatformHandleVectorPtr platform_handles) {
  DCHECK(internal::g_io_thread_task_runner->RunsTasksOnCurrentThread());
  // Note: normally, when a message arrives here we should find a corresponding
  // entry for the MessagePipeDispatcher with the given route_id. However it is
  // possible that they just connected, and due to race conditions one side has
  // connected and sent a message (and even closed) before the other side had a
  // chance to register with this RoutedRawChannel. In that case, we must buffer
  // all messages.
  base::AutoLock auto_lock(lock_);
  uint64_t route_id = message_view.route_id();
  if (route_id == kInternalRoutingId) {
    if (message_view.num_bytes() != sizeof(char) + sizeof(uint64_t)) {
      NOTREACHED() << "Invalid internal message in RoutedRawChannel.";
      return;
    }
    const char* bytes = static_cast<const char*>(message_view.bytes());
    if (bytes[0] != ROUTE_CLOSED) {
      NOTREACHED() << "Unknown internal message in RoutedRawChannel.";
      return;
    }
    uint64_t closed_route = *reinterpret_cast<const uint64_t*>(&bytes[1]);
    if (close_routes_.find(closed_route) != close_routes_.end()) {
      NOTREACHED() << "Should only receive one ROUTE_CLOSED per route.";
      return;
    }
    close_routes_.insert(closed_route);
    if (routes_.find(closed_route) == routes_.end())
      return;  // This side hasn't connected yet.

    routes_[closed_route]->OnError(ERROR_READ_SHUTDOWN);
    return;
  }

  if (routes_.find(route_id) != routes_.end()) {
    routes_[route_id]->OnReadMessage(message_view, platform_handles.Pass());
  } else {
    scoped_ptr<PendingMessage> msg(new PendingMessage);
    msg->message.resize(message_view.total_size());
    memcpy(&msg->message[0], message_view.main_buffer(),
           message_view.total_size());
    msg->handles = platform_handles.Pass();
    pending_messages_.push_back(msg.Pass());
  }
}

void RoutedRawChannel::OnError(Error error) {
  DCHECK(internal::g_io_thread_task_runner->RunsTasksOnCurrentThread());
  bool destruct = false;
  {
    base::AutoLock auto_lock(lock_);

    channel_->Shutdown();
    channel_ = nullptr;
    if (routes_.empty()) {
      destruct = true;
    } else {
      for (auto it = routes_.begin(); it != routes_.end(); ++it)
        it->second->OnError(error);
    }
  }

  if (destruct)
    delete this;
}

}  // namespace edk
}  // namespace mojo
