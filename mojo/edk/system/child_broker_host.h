// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_CHILD_BROKER_HOST_H_
#define MOJO_EDK_SYSTEM_CHILD_BROKER_HOST_H_

#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/process/process_handle.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/edk/system/raw_channel.h"
#include "mojo/edk/system/system_impl_export.h"

namespace mojo {
namespace edk {

// Responds to requests from ChildBroker. This is used to handle message pipe
// multiplexing and Windows sandbox messages. There is one object of this class
// per child process host object.
// This object will delete itself when it notices that the pipe is broken.
class MOJO_SYSTEM_IMPL_EXPORT ChildBrokerHost
    : public RawChannel::Delegate
#if defined(OS_WIN)
      , NON_EXPORTED_BASE(public base::MessageLoopForIO::IOHandler) {
#else
    {
#endif
 public:
  // |child_process| is a handle to the child process. It's not owned by this
  // class but is guaranteed to be alive as long as the child process is
  // running. |pipe| is a handle to the communication pipe to the child process,
  // which is generated inside mojo::edk::ChildProcessLaunched. It is owned by
  // this class.
  ChildBrokerHost(base::ProcessHandle child_process, ScopedPlatformHandle pipe);

  base::ProcessId GetProcessId();

  // Sends a message to the child process to connect to |process_id| via |pipe|.
  void ConnectToProcess(base::ProcessId process_id, ScopedPlatformHandle pipe);

  // Sends a message to the child process that |pipe_id|'s other end is in
  // |process_id|.
  void ConnectMessagePipe(uint64_t pipe_id, base::ProcessId process_id);

 private:
  ~ChildBrokerHost() override;

  // RawChannel::Delegate implementation:
  void OnReadMessage(
      const MessageInTransit::View& message_view,
      ScopedPlatformHandleVectorPtr platform_handles) override;
  void OnError(Error error) override;

#if defined(OS_WIN)
  void RegisterIOHandler();
  void BeginRead();

  // base::MessageLoopForIO::IOHandler implementation:
  void OnIOCompleted(base::MessageLoopForIO::IOContext* context,
                     DWORD bytes_transferred,
                     DWORD error) override;

  // Helper wrappers around DuplicateHandle.
  HANDLE DuplicateToChild(HANDLE handle);
  HANDLE DuplicateFromChild(HANDLE handle);
#endif

  base::ProcessId process_id_;

  // Channel used to receive and send multiplexing related messages.
  RawChannel* child_channel_;

#if defined(OS_WIN)
  // Handle to the child process, used for duplication of handles.
  base::ProcessHandle child_process_;

  // Pipe used for synchronous messages from the child. Responses are written to
  // it as well.
  ScopedPlatformHandle sync_channel_;

  base::MessageLoopForIO::IOContext read_context_;
  base::MessageLoopForIO::IOContext write_context_;

  std::vector<char> read_data_;
  // How many bytes in read_data_ we already read.
  uint32_t num_bytes_read_;
  std::vector<char> write_data_;
#endif

  DISALLOW_COPY_AND_ASSIGN(ChildBrokerHost);
};

}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_CHILD_BROKER_HOST_H_
