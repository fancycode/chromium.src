// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/unix_domain_server_socket_posix.h"

#include <vector>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/socket/unix_domain_client_socket_posix.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

const char kSocketFilename[] = "socket_for_testing";
const char kInvalidSocketPath[] = "/invalid/path";

bool UserCanConnectCallback(bool allow_user,
    const UnixDomainServerSocket::Credentials& credentials) {
  // Here peers are running in same process.
#if defined(OS_LINUX) || defined(OS_ANDROID)
  EXPECT_EQ(getpid(), credentials.process_id);
#endif
  EXPECT_EQ(getuid(), credentials.user_id);
  EXPECT_EQ(getgid(), credentials.group_id);
  return allow_user;
}

UnixDomainServerSocket::AuthCallback CreateAuthCallback(bool allow_user) {
  return base::Bind(&UserCanConnectCallback, allow_user);
}

class UnixDomainServerSocketTest : public testing::Test {
 protected:
  UnixDomainServerSocketTest() {
    EXPECT_TRUE(temp_dir_.CreateUniqueTempDir());
    socket_path_ = temp_dir_.path().Append(kSocketFilename).value();
  }

  base::ScopedTempDir temp_dir_;
  std::string socket_path_;
};

TEST_F(UnixDomainServerSocketTest, ListenWithInvalidPath) {
  const bool kUseAbstractNamespace = false;
  UnixDomainServerSocket server_socket(CreateAuthCallback(true),
                                       kUseAbstractNamespace);
  EXPECT_EQ(ERR_FILE_NOT_FOUND,
            server_socket.ListenWithAddressAndPort(kInvalidSocketPath, 0, 1));
}

TEST_F(UnixDomainServerSocketTest, ListenWithInvalidPathWithAbstractNamespace) {
  const bool kUseAbstractNamespace = true;
  UnixDomainServerSocket server_socket(CreateAuthCallback(true),
                                       kUseAbstractNamespace);
#if defined(OS_ANDROID) || defined(OS_LINUX)
  EXPECT_EQ(OK,
            server_socket.ListenWithAddressAndPort(kInvalidSocketPath, 0, 1));
#else
  EXPECT_EQ(ERR_ADDRESS_INVALID,
            server_socket.ListenWithAddressAndPort(kInvalidSocketPath, 0, 1));
#endif
}

TEST_F(UnixDomainServerSocketTest, AcceptWithForbiddenUser) {
  const bool kUseAbstractNamespace = false;

  UnixDomainServerSocket server_socket(CreateAuthCallback(false),
                                       kUseAbstractNamespace);
  EXPECT_EQ(OK, server_socket.ListenWithAddressAndPort(socket_path_, 0, 1));

  scoped_ptr<StreamSocket> accepted_socket;
  TestCompletionCallback accept_callback;
  EXPECT_EQ(ERR_IO_PENDING,
            server_socket.Accept(&accepted_socket, accept_callback.callback()));
  EXPECT_FALSE(accepted_socket);

  UnixDomainClientSocket client_socket(socket_path_, kUseAbstractNamespace);
  EXPECT_FALSE(client_socket.IsConnected());

  // Connect() will return OK before the server rejects the connection.
  TestCompletionCallback connect_callback;
  int rv = connect_callback.GetResult(
      client_socket.Connect(connect_callback.callback()));
  ASSERT_EQ(OK, rv);

  // Try to read from the socket.
  const int read_buffer_size = 10;
  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(read_buffer_size));
  TestCompletionCallback read_callback;
  rv = read_callback.GetResult(client_socket.Read(read_buffer, read_buffer_size,
                                                  read_callback.callback()));

  // The server should have disconnected gracefully, without sending any data.
  ASSERT_EQ(0, rv);
  EXPECT_FALSE(client_socket.IsConnected());

  // The server socket should not have called |accept_callback| or modified
  // |accepted_socket|.
  EXPECT_FALSE(accept_callback.have_result());
  EXPECT_FALSE(accepted_socket);
}

// Normal cases including read/write are tested by UnixDomainClientSocketTest.

}  // namespace
}  // namespace net
