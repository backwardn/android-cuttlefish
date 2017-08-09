/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "host/vadb/usbip/server.h"

#include <glog/logging.h>
#include <netinet/in.h>

#include "common/libs/fs/shared_select.h"

using avd::SharedFD;

namespace vadb {
namespace usbip {
namespace {
// USB-IP server port. USBIP will attempt to connect to this server to attach
// new virtual USB devices to host.
static constexpr int kServerPort = 3240;
}  // namespace

Server::Server(const std::string &name, const DevicePool &devices)
    : name_{name}, device_pool_{devices}, vhci_{name} {}

bool Server::Init() { return CreateServerSocket() && vhci_.Init(); }

// Open new listening server socket.
// Returns false, if listening socket could not be created.
bool Server::CreateServerSocket() {
  LOG(INFO) << "Starting server socket on port " << kServerPort;

  // TODO(ender): would it work if we used PACKET instead?
  server_ = SharedFD::SocketLocalServer(name_.c_str(), true, SOCK_STREAM, 0700);
  if (!server_->IsOpen()) {
    LOG(ERROR) << "Could not create socket: " << server_->StrError();
    return false;
  }
  return true;
}

// Serve incoming USB/IP connections.
void Server::Serve() {
  LOG(INFO) << "Serving USB/IP connections.";
  while (true) {
    avd::SharedFDSet fd_read;
    fd_read.Set(server_);
    for (const auto &client : clients_) fd_read.Set(client.fd());

    int ret = avd::Select(&fd_read, nullptr, nullptr, nullptr);
    if (ret <= 0) continue;

    if (fd_read.IsSet(server_)) HandleIncomingConnection();

    for (auto iter = clients_.begin(); iter != clients_.end();) {
      if (fd_read.IsSet(iter->fd())) {
        // If client conversation failed, hang up.
        if (!iter->HandleIncomingMessage()) {
          iter = clients_.erase(iter);
          vhci_.TriggerAttach();
          continue;
        }
      }
      ++iter;
    }
  }
  LOG(INFO) << "Server exiting.";
}

// Accept new USB/IP connection. Add it to client pool.
void Server::HandleIncomingConnection() {
  SharedFD client = SharedFD::Accept(*server_, nullptr, nullptr);
  if (!client->IsOpen()) {
    LOG(ERROR) << "Client connection failed: " << client->StrError();
    return;
  }

  clients_.emplace_back(device_pool_, client);
}
}  // namespace usbip
}  // namespace vadb