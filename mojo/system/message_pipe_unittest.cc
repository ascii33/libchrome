// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/system/message_pipe.h"

#include <limits>

#include "base/memory/ref_counted.h"
#include "base/threading/platform_thread.h"  // For |Sleep()|.
#include "base/time/time.h"
#include "mojo/system/waiter.h"
#include "mojo/system/waiter_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace system {
namespace {

// Tests:
//  - only default flags
//  - reading messages from a port
//    - when there are no/one/two messages available for that port
//    - with buffer size 0 (and null buffer) -- should get size
//    - with too-small buffer -- should get size
//    - also verify that buffers aren't modified when/where they shouldn't be
//  - writing messages to a port
//    - in the obvious scenarios (as above)
//    - to a port that's been closed
//  - writing a message to a port, closing the other (would be the source) port,
//    and reading it
TEST(MessagePipeTest, Basic) {
  scoped_refptr<MessagePipe> mp(new MessagePipe());

  int32_t buffer[2];
  const uint32_t kBufferSize = static_cast<uint32_t>(sizeof(buffer));
  uint32_t buffer_size;

  // Nothing to read yet on port 0.
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_NOT_FOUND,
            mp->ReadMessage(0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));
  EXPECT_EQ(kBufferSize, buffer_size);
  EXPECT_EQ(123, buffer[0]);
  EXPECT_EQ(456, buffer[1]);

  // Ditto for port 1.
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_NOT_FOUND,
            mp->ReadMessage(1,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));

  // Write from port 1 (to port 0).
  buffer[0] = 789012345;
  buffer[1] = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->WriteMessage(1,
                             buffer, static_cast<uint32_t>(sizeof(buffer[0])),
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Read from port 0.
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->ReadMessage(0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));
  EXPECT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  EXPECT_EQ(789012345, buffer[0]);
  EXPECT_EQ(456, buffer[1]);

  // Read again from port 0 -- it should be empty.
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_NOT_FOUND,
            mp->ReadMessage(0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));

  // Write two messages from port 0 (to port 1).
  buffer[0] = 123456789;
  buffer[1] = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->WriteMessage(0,
                             buffer, static_cast<uint32_t>(sizeof(buffer[0])),
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));
  buffer[0] = 234567890;
  buffer[1] = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->WriteMessage(0,
                             buffer, static_cast<uint32_t>(sizeof(buffer[0])),
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Read from port 1 with buffer size 0 (should get the size of next message).
  // Also test that giving a null buffer is okay when the buffer size is 0.
  buffer_size = 0;
  EXPECT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            mp->ReadMessage(1,
                            NULL, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));
  EXPECT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);

  // Read from port 1 with buffer size 1 (too small; should get the size of next
  // message).
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = 1;
  EXPECT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            mp->ReadMessage(1,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));
  EXPECT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  EXPECT_EQ(123, buffer[0]);
  EXPECT_EQ(456, buffer[1]);

  // Read from port 1.
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->ReadMessage(1,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));
  EXPECT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  EXPECT_EQ(123456789, buffer[0]);
  EXPECT_EQ(456, buffer[1]);

  // Read again from port 1.
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->ReadMessage(1,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));
  EXPECT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  EXPECT_EQ(234567890, buffer[0]);
  EXPECT_EQ(456, buffer[1]);

  // Read again from port 1 -- it should be empty.
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_NOT_FOUND,
            mp->ReadMessage(1,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));

  // Write from port 0 (to port 1).
  buffer[0] = 345678901;
  buffer[1] = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->WriteMessage(0,
                             buffer, static_cast<uint32_t>(sizeof(buffer[0])),
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Close port 0.
  mp->Close(0);

  // Try to write from port 1 (to port 0).
  buffer[0] = 456789012;
  buffer[1] = 0;
  EXPECT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            mp->WriteMessage(1,
                             buffer, static_cast<uint32_t>(sizeof(buffer[0])),
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Read from port 1; should still get message (even though port 0 was closed).
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->ReadMessage(1,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));
  EXPECT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  EXPECT_EQ(345678901, buffer[0]);
  EXPECT_EQ(456, buffer[1]);

  // Read again from port 1 -- it should be empty.
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_NOT_FOUND,
            mp->ReadMessage(1,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));

  mp->Close(1);
}

TEST(MessagePipeTest, DiscardMode) {
  scoped_refptr<MessagePipe> mp(new MessagePipe());

  int32_t buffer[2];
  const uint32_t kBufferSize = static_cast<uint32_t>(sizeof(buffer));
  uint32_t buffer_size;

  // Write from port 1 (to port 0).
  buffer[0] = 789012345;
  buffer[1] = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->WriteMessage(1,
                             buffer, static_cast<uint32_t>(sizeof(buffer[0])),
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Read/discard from port 0 (no buffer); get size.
  buffer_size = 0;
  EXPECT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            mp->ReadMessage(0,
                            NULL, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_MAY_DISCARD));
  EXPECT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);

  // Read again from port 0 -- it should be empty.
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_NOT_FOUND,
            mp->ReadMessage(0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_MAY_DISCARD));

  // Write from port 1 (to port 0).
  buffer[0] = 890123456;
  buffer[1] = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->WriteMessage(1,
                             buffer, static_cast<uint32_t>(sizeof(buffer[0])),
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Read from port 0 (buffer big enough).
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->ReadMessage(0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_MAY_DISCARD));
  EXPECT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  EXPECT_EQ(890123456, buffer[0]);
  EXPECT_EQ(456, buffer[1]);

  // Read again from port 0 -- it should be empty.
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_NOT_FOUND,
            mp->ReadMessage(0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_MAY_DISCARD));

  // Write from port 1 (to port 0).
  buffer[0] = 901234567;
  buffer[1] = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->WriteMessage(1,
                             buffer, static_cast<uint32_t>(sizeof(buffer[0])),
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Read/discard from port 0 (buffer too small); get size.
  buffer_size = 1;
  EXPECT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            mp->ReadMessage(0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_MAY_DISCARD));
  EXPECT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);

  // Read again from port 0 -- it should be empty.
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_NOT_FOUND,
            mp->ReadMessage(0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_MAY_DISCARD));

  // Write from port 1 (to port 0).
  buffer[0] = 123456789;
  buffer[1] = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->WriteMessage(1,
                             buffer, static_cast<uint32_t>(sizeof(buffer[0])),
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Discard from port 0.
  buffer_size = 1;
  EXPECT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            mp->ReadMessage(0,
                            NULL, NULL,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_MAY_DISCARD));

  // Read again from port 0 -- it should be empty.
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_NOT_FOUND,
            mp->ReadMessage(0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_MAY_DISCARD));

  mp->Close(0);
  mp->Close(1);
}

TEST(MessagePipeTest, InvalidParams) {
  scoped_refptr<MessagePipe> mp(new MessagePipe());

  char buffer[1];
  MojoHandle handles[1];

  // |WriteMessage|:
  // Null buffer with nonzero buffer size.
  EXPECT_EQ(MOJO_RESULT_INVALID_ARGUMENT,
            mp->WriteMessage(0,
                             NULL, 1,
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));
  // Huge buffer size.
  EXPECT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            mp->WriteMessage(0,
                             buffer, std::numeric_limits<uint32_t>::max(),
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Null handles with nonzero handle count.
  EXPECT_EQ(MOJO_RESULT_INVALID_ARGUMENT,
            mp->WriteMessage(0,
                             buffer, sizeof(buffer),
                             NULL, 1,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));
  // Huge handle count (implausibly big on some systems -- more than can be
  // stored in a 32-bit address space).
  // Note: This may return either |MOJO_RESULT_INVALID_ARGUMENT| or
  // |MOJO_RESULT_RESOURCE_EXHAUSTED|, depending on whether it's plausible or
  // not.
  EXPECT_NE(MOJO_RESULT_OK,
            mp->WriteMessage(0,
                             buffer, sizeof(buffer),
                             handles, std::numeric_limits<uint32_t>::max(),
                             MOJO_WRITE_MESSAGE_FLAG_NONE));
  // Huge handle count (plausibly big).
  EXPECT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            mp->WriteMessage(0,
                             buffer, sizeof(buffer),
                             handles, std::numeric_limits<uint32_t>::max() /
                                 sizeof(handles[0]),
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // |ReadMessage|:
  // Null buffer with nonzero buffer size.
  uint32_t buffer_size = 1;
  EXPECT_EQ(MOJO_RESULT_INVALID_ARGUMENT,
            mp->ReadMessage(0,
                            NULL, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));
  // Null handles with nonzero handle count.
  buffer_size = static_cast<uint32_t>(sizeof(buffer));
  uint32_t handle_count = 1;
  EXPECT_EQ(MOJO_RESULT_INVALID_ARGUMENT,
            mp->ReadMessage(0,
                            buffer, &buffer_size,
                            NULL, &handle_count,
                            MOJO_READ_MESSAGE_FLAG_NONE));

  mp->Close(0);
  mp->Close(1);
}

TEST(MessagePipeTest, BasicWaiting) {
  scoped_refptr<MessagePipe> mp(new MessagePipe());
  Waiter waiter;

  int32_t buffer[1];
  const uint32_t kBufferSize = static_cast<uint32_t>(sizeof(buffer));
  uint32_t buffer_size;

  // Always writable (until the other port is closed).
  EXPECT_EQ(MOJO_RESULT_ALREADY_EXISTS,
            mp->AddWaiter(0, &waiter, MOJO_WAIT_FLAG_WRITABLE, 0));
  EXPECT_EQ(MOJO_RESULT_ALREADY_EXISTS,
            mp->AddWaiter(0,
                          &waiter,
                          MOJO_WAIT_FLAG_READABLE | MOJO_WAIT_FLAG_WRITABLE,
                          0));

  // Not yet readable.
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->AddWaiter(0, &waiter, MOJO_WAIT_FLAG_READABLE, 1));
  EXPECT_EQ(MOJO_RESULT_DEADLINE_EXCEEDED, waiter.Wait(0));
  mp->RemoveWaiter(0, &waiter);

  // Write from port 0 (to port 1), to make port 1 readable.
  buffer[0] = 123456789;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->WriteMessage(0,
                             buffer, kBufferSize,
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // Port 1 should already be readable now.
  EXPECT_EQ(MOJO_RESULT_ALREADY_EXISTS,
            mp->AddWaiter(1, &waiter, MOJO_WAIT_FLAG_READABLE, 2));
  EXPECT_EQ(MOJO_RESULT_ALREADY_EXISTS,
            mp->AddWaiter(1,
                          &waiter,
                          MOJO_WAIT_FLAG_READABLE | MOJO_WAIT_FLAG_WRITABLE,
                          0));
  // ... and still writable.
  EXPECT_EQ(MOJO_RESULT_ALREADY_EXISTS,
            mp->AddWaiter(1, &waiter, MOJO_WAIT_FLAG_WRITABLE, 3));

  // Close port 0.
  mp->Close(0);

  // Now port 1 should not be writable.
  EXPECT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            mp->AddWaiter(1, &waiter, MOJO_WAIT_FLAG_WRITABLE, 4));

  // But it should still be readable.
  EXPECT_EQ(MOJO_RESULT_ALREADY_EXISTS,
            mp->AddWaiter(1, &waiter, MOJO_WAIT_FLAG_READABLE, 5));

  // Read from port 1.
  buffer[0] = 0;
  buffer_size = kBufferSize;
  EXPECT_EQ(MOJO_RESULT_OK,
            mp->ReadMessage(1,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));
  EXPECT_EQ(123456789, buffer[0]);

  // Now port 1 should no longer be readable.
  EXPECT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            mp->AddWaiter(1, &waiter, MOJO_WAIT_FLAG_READABLE, 6));

  mp->Close(1);
}

TEST(MessagePipeTest, ThreadedWaiting) {
  int32_t buffer[1];
  const uint32_t kBufferSize = static_cast<uint32_t>(sizeof(buffer));

  MojoResult result;

  // Write to wake up waiter waiting for read.
  {
    scoped_refptr<MessagePipe> mp(new MessagePipe());
    test::SimpleWaiterThread thread(&result);

    EXPECT_EQ(MOJO_RESULT_OK,
              mp->AddWaiter(1, thread.waiter(), MOJO_WAIT_FLAG_READABLE, 0));
    thread.Start();

    buffer[0] = 123456789;
    // Write from port 0 (to port 1), which should wake up the waiter.
    EXPECT_EQ(MOJO_RESULT_OK,
              mp->WriteMessage(0,
                               buffer, kBufferSize,
                               NULL, 0,
                               MOJO_WRITE_MESSAGE_FLAG_NONE));

    mp->RemoveWaiter(1, thread.waiter());

    mp->Close(0);
    mp->Close(1);
  }  // Joins |thread|.
  // The waiter should have woken up successfully.
  EXPECT_EQ(0, result);

  // Close to cancel waiter.
  {
    scoped_refptr<MessagePipe> mp(new MessagePipe());
    test::SimpleWaiterThread thread(&result);

    EXPECT_EQ(MOJO_RESULT_OK,
              mp->AddWaiter(1, thread.waiter(), MOJO_WAIT_FLAG_READABLE, 0));
    thread.Start();

    // Close port 1 first -- this should result in the waiter being cancelled.
    mp->CancelAllWaiters(1);
    mp->Close(1);

    // Port 1 is closed, so |Dispatcher::RemoveWaiter()| wouldn't call into the
    // |MessagePipe| to remove any waiter.

    mp->Close(0);
  }  // Joins |thread|.
  EXPECT_EQ(MOJO_RESULT_CANCELLED, result);

  // Close to make waiter un-wake-up-able.
  {
    scoped_refptr<MessagePipe> mp(new MessagePipe());
    test::SimpleWaiterThread thread(&result);

    EXPECT_EQ(MOJO_RESULT_OK,
              mp->AddWaiter(1, thread.waiter(), MOJO_WAIT_FLAG_READABLE, 0));
    thread.Start();

    // Close port 0 first -- this should wake the waiter up, since port 1 will
    // never be readable.
    mp->CancelAllWaiters(0);
    mp->Close(0);

    mp->RemoveWaiter(1, thread.waiter());

    mp->CancelAllWaiters(1);
    mp->Close(1);
  }  // Joins |thread|.
  EXPECT_EQ(MOJO_RESULT_FAILED_PRECONDITION, result);
}

}  // namespace
}  // namespace system
}  // namespace mojo
