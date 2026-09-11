// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "tire_health/service_identity.hpp"
#include "tire_health/runtime/json.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace tire_health::runtime {
struct Metadata {
  std::string unit_system_uid, unit_role, service_version, service_artifact_sha256;
  std::string vdp_contract_version, vdp_contract_sha256;
    // Absent only for explicitly retained legacy records/golden fixtures.
    std::optional<ServiceInstance> service_instance{};
};
inline constexpr std::array<const char*,15> paths = {
 "Vehicle.Speed", "Vehicle.Acceleration.Lateral", "Vehicle.Chassis.Axle.Row1.SteeringAngle",
 "Vehicle.Chassis.Axle.Row1.Wheel.Left.Speed", "Vehicle.Chassis.Axle.Row1.Wheel.Right.Speed",
 "Vehicle.Chassis.Axle.Row2.Wheel.Left.Speed", "Vehicle.Chassis.Axle.Row2.Wheel.Right.Speed",
 "Vehicle.CarlaSimulation.ChaosWheel.Row1.Left.LongitudinalSlip", "Vehicle.CarlaSimulation.ChaosWheel.Row1.Right.LongitudinalSlip",
 "Vehicle.CarlaSimulation.ChaosWheel.Row2.Left.LongitudinalSlip", "Vehicle.CarlaSimulation.ChaosWheel.Row2.Right.LongitudinalSlip",
 "Vehicle.CarlaSimulation.ChaosWheel.Row1.Left.LateralSlipAngle", "Vehicle.CarlaSimulation.ChaosWheel.Row1.Right.LateralSlipAngle",
 "Vehicle.CarlaSimulation.ChaosWheel.Row2.Left.LateralSlipAngle", "Vehicle.CarlaSimulation.ChaosWheel.Row2.Right.LateralSlipAngle"};
inline constexpr const char* request_path="Vehicle.OEM.TireHealth.Advisory.Request";
inline constexpr const char* status_path="Vehicle.OEM.TireHealth.Advisory.GatewayStatus";
struct Signal {double value{}; std::int64_t epoch_ms{}; bool valid{};};
struct Frame {std::array<double,15> values{}; std::int64_t epoch_ms{};};
std::optional<Frame> complete_frame(const std::array<Signal,15>&, std::int64_t wall_ms);
std::string utc_timestamp(std::int64_t epoch_ms);
bool date_time(const std::string&);
std::string random_uuid();
std::string uuid_v5(const std::string&, const std::vector<std::string>&);
std::string read_file(const std::filesystem::path&, std::size_t limit);
void atomic_private_file(const std::filesystem::path&, const std::string&);
void durable_file(const std::filesystem::path&, const std::string&);
std::string canonical(const Json&);
std::string message_key(const Json&);
struct Credential {std::string token; std::int64_t expires{}, renew_after{}; std::string code; bool retryable{};};
Credential parse_credential(const std::string&, std::int64_t now_seconds);
std::string credential_request(const std::string&);
std::string exchange_credential(const std::string&, const std::atomic<bool>&);
struct HttpResponse {int status{}; std::string body; int retry_after{};};
HttpResponse parse_http_response(const std::string&);
HttpResponse post_backend(const std::string& bytes, const std::atomic<bool>& stop, bool demo_mock = false);
bool matches_ack(const std::string&, const HttpResponse&);
bool retryable_http(int);
int retry_delay(unsigned, double jitter, int retry_after=0);
} // namespace tire_health::runtime
