// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <map>
#include "tire_health/runtime/json.hpp"
#include "tire_health/runtime/runtime.hpp"
#include "tire_health/runtime/token_session.hpp"

namespace tire_health::runtime {
struct NativeServiceInputs { std::string service_version; ServiceInstance instance; };
struct ApplicationInputs { std::filesystem::path metadata_file; std::filesystem::path ca_file; std::optional<NativeServiceInputs> native{}; };
ApplicationInputs parse_arguments(int argc, char** argv);
Metadata parse_legacy_metadata(const std::string& bytes);
NativeServiceInputs parse_service_inputs(const std::string& release_bytes, const std::map<std::string, std::string>& environment);
void initialize_service_inputs(ApplicationInputs& inputs);
Metadata parse_metadata(const std::string& bytes, const NativeServiceInputs& native);
Metadata runtime_metadata(const ApplicationInputs& inputs, const std::string& bytes);
// Missing initial public inputs are retryable; malformed inputs/identity are not.
std::optional<Metadata> initial_runtime_metadata(const ApplicationInputs& inputs);
ServiceInstance parse_service_instance(const Json& value);
Json metadata_binding(const Metadata& metadata);
Metadata parse_metadata_binding(const Json& value);
std::string read_private_token(const std::filesystem::path& path);
std::int64_t boot_milliseconds();
std::int64_t wall_milliseconds();
// A successful KAC response is translated to BOOTTIME deadlines. A wall-clock
// jump forward can revoke earlier; a backward jump must not extend the lease.
struct Lease {
    std::int64_t expires_wall{};
    std::int64_t expires_boot{};
    std::int64_t renew_boot{};
    void issued(const Credential& credential, std::int64_t wall_seconds, std::int64_t boot_ms);
    bool expired(std::int64_t wall_seconds, std::int64_t boot_ms) const;
};
}  // namespace tire_health::runtime
