// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SHELL_PUBLIC_CPP_LIB_CONNECTION_IMPL_H_
#define MOJO_SHELL_PUBLIC_CPP_LIB_CONNECTION_IMPL_H_

#include <stdint.h>

#include <set>
#include <string>

#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/shell/public/cpp/connection.h"
#include "mojo/shell/public/interfaces/connector.mojom.h"
#include "mojo/shell/public/interfaces/interface_provider.mojom.h"

namespace mojo {
namespace internal {

// A ConnectionImpl represents each half of a connection between two
// applications, allowing customization of which interfaces are published to the
// other.
class ConnectionImpl : public Connection {
 public:
  ConnectionImpl();
  // |allowed_interfaces| are the set of interfaces that the shell has allowed
  // an application to expose to another application. If this set contains only
  // the string value "*" all interfaces may be exposed.
  ConnectionImpl(const std::string& connection_name,
                 const std::string& remote_name,
                 uint32_t remote_id,
                 const std::string& remote_user_id,
                 shell::mojom::InterfaceProviderPtr remote_interfaces,
                 shell::mojom::InterfaceProviderRequest local_interfaces,
                 const std::set<std::string>& allowed_interfaces);
  ~ConnectionImpl() override;

  shell::mojom::Connector::ConnectCallback GetConnectCallback();

 private:
  // Connection:
  const std::string& GetConnectionName() override;
  const std::string& GetRemoteApplicationName() override;
  const std::string& GetRemoteUserID() const override;
  void SetConnectionLostClosure(const Closure& handler) override;
  bool GetConnectionResult(shell::mojom::ConnectResult* result) const override;
  bool GetRemoteApplicationID(uint32_t* remote_id) const override;
  void AddConnectionCompletedClosure(const Closure& callback) override;
  bool AllowsInterface(const std::string& interface_name) const override;
  shell::mojom::InterfaceProvider* GetRemoteInterfaces() override;
  InterfaceRegistry* GetLocalRegistry() override;
  base::WeakPtr<Connection> GetWeakPtr() override;

  void OnConnectionCompleted(shell::mojom::ConnectResult result,
                             const std::string& target_user_id,
                             uint32_t target_application_id);

  const std::string connection_name_;
  const std::string remote_name_;

  shell::mojom::ConnectResult result_ = shell::mojom::ConnectResult::OK;
  uint32_t remote_id_ = shell::mojom::Connector::kInvalidApplicationID;
  bool connection_completed_ = false;
  std::vector<Closure> connection_completed_callbacks_;
  std::string remote_user_id_ = shell::mojom::kInheritUserID;

  InterfaceRegistry local_registry_;
  shell::mojom::InterfaceProviderPtr remote_interfaces_;

  const std::set<std::string> allowed_interfaces_;
  const bool allow_all_interfaces_;

  base::WeakPtrFactory<ConnectionImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ConnectionImpl);
};

}  // namespace internal
}  // namespace mojo

#endif  // MOJO_SHELL_PUBLIC_CPP_LIB_CONNECTION_IMPL_H_
