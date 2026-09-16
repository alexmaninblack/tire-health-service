// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "tire_health/state.hpp"
#include <memory>
namespace tire_health {
class Runtime {
public:
 Runtime(std::filesystem::path state,std::filesystem::path outbox,runtime::Metadata);
 std::optional<Episode> ingest(const runtime::Frame&);
 void disconnect();
 void stop();
 void update_vdp_metadata(const runtime::Metadata&);
 void function_status(const std::string& reason,std::int64_t now,const std::vector<std::string>& missing={});
 std::optional<Pending> next_message();
 bool accept(const Pending&,const runtime::HttpResponse&);
 // Internal domain seam only, never a CLI/HTTP override.
 bool apply_episode(const Features&,const Episode&,const std::string& model_digest);
 std::optional<std::string> next_advisory(std::int64_t now);
 void gateway_status(const std::string&,std::int64_t now);
 std::optional<std::string> demo_control_poll();
 void demo_control_command(const std::string& response,std::int64_t now);
 std::optional<std::string> demo_control_ack(std::int64_t now);
 void demo_control_accepted(const std::string& response);
 std::string advisory_readiness(std::int64_t now);
 bool state_ready() const {return static_cast<bool>(store_);}
private:
 std::unique_ptr<StateStore> store_; runtime::Metadata metadata_; EpisodeEngine engine_; std::mutex mutex_;
 std::string reason_;std::int64_t status_at_=-1,refresh_at_=-1,last_write_=-1;bool advisory_sent_{};
 std::int64_t telemetry_at_=-1;
};
} // namespace tire_health
