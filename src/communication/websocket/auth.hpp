// Copyright 2021 Memgraph Ltd.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
// License, and you may not use this file except in compliance with the Business Source License.
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0, included in the file
// licenses/APL.txt.

#pragma once

#include <string>

#include "auth/auth.hpp"
#include "utils/spin_lock.hpp"
#include "utils/synchronized.hpp"

namespace communication::websocket {

class SafeAuth {
 public:
  explicit SafeAuth(utils::Synchronized<auth::Auth, utils::WritePrioritizedRWLock> *auth) : auth_{auth} {}

  bool Authenticate(const std::string &username, const std::string &password);

 private:
  utils::Synchronized<auth::Auth, utils::WritePrioritizedRWLock> *auth_;
};
}  // namespace communication::websocket