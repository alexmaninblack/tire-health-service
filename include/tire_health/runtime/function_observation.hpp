// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "tire_health/runtime/runtime.hpp"
#include "tire_health/runtime/json.hpp"
#include "tire_health/runtime/sha256.hpp"
#include "aosedge/function_observation.hpp"
namespace tire_health::runtime {
struct ObservationCodec {
 using Json=tire_health::runtime::Json;
 static Json parse(const std::string& text,std::size_t limit){return parse_json(text,limit);}
 static std::string encode(const Json& value){return canonical(value);}
 static std::string digest(const std::string& value){return sha256_hex(value);}
 static std::string timestamp(std::int64_t value){return utc_timestamp(value);}
 static bool uuid(const std::string& value){return is_uuid(value);}
};
using ObservationStream=aosedge::FunctionObservation<ObservationCodec>;
}

