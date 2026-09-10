// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "tire_health/model.hpp"
namespace tire_health {
struct Pending {std::string key, bytes;};
class StateStore {
public:
 StateStore(std::filesystem::path state,std::filesystem::path outbox,std::string unit_uid);
 const runtime::Json& state() const {return state_;}
 // Journal/state/message/commit order is one serialized operation. Network
 // delivery starts only after it returns and never holds a storage lock.
 bool commit(runtime::Json state,const std::vector<runtime::Json>& messages);
 std::optional<Pending> pending() const;
 bool acknowledge(const Pending&,const runtime::HttpResponse&);
 std::size_t queued() const;
private:
 std::filesystem::path root_,outbox_; std::string uid_; runtime::Json state_;
 void replay(const runtime::Json&);
 void validate_state(const runtime::Json&) const;
 void validate_queue() const;
};
} // namespace tire_health
