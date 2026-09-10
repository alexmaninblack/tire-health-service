// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#include "tire_health/runtime/runtime.hpp"
#include "tire_health/runtime/json.hpp"
#include "tire_health/runtime/sha256.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace tire_health::runtime {
bool date_time(const std::string& text) {
    // ACK schema accepts RFC3339, not only the millisecond-UTC source profile.
    if (text.size() < 20 || text.size() > 128 || text[4] != '-' || text[7] != '-' ||
        (text[10] != 'T' && text[10] != 't') || text[13] != ':' || text[16] != ':') return false;
    const auto number = [&](std::size_t start, std::size_t length) {
        int n = 0;
        for (std::size_t i = start; i < start + length; ++i) {
            if (text[i] < '0' || text[i] > '9') return -1;
            n = n * 10 + text[i] - '0';
        }
        return n;
    };
    const int year = number(0, 4), month = number(5, 2), day = number(8, 2);
    const int hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
    constexpr int days[]{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 0 || month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 60) return false;
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day > days[month] + (month == 2 && leap ? 1 : 0)) return false;
    std::size_t p = 19;
    if (text[p] == '.') {
        const auto start = ++p;
        while (p < text.size() && text[p] >= '0' && text[p] <= '9') ++p;
        if (p == start || p == text.size()) return false;
    }
    if (text[p] == 'Z' || text[p] == 'z') return p + 1 == text.size();
    if ((text[p] != '+' && text[p] != '-') || p + 6 != text.size() || text[p + 3] != ':') return false;
    const int zone_hour = number(p + 1, 2), zone_minute = number(p + 4, 2);
    return zone_hour >= 0 && zone_hour <= 23 && zone_minute >= 0 && zone_minute <= 59;
}
std::string utc_timestamp(std::int64_t epoch_ms) {
    if (epoch_ms < 0) throw std::invalid_argument("TIME_INVALID");
    auto epoch = static_cast<std::time_t>(epoch_ms / 1000);
    std::tm tm{};
    if (!gmtime_r(&epoch, &tm)) throw std::invalid_argument("TIME_INVALID");
    char out[32];
    if (std::strftime(out, sizeof(out), "%Y-%m-%dT%H:%M:%S", &tm) == 0) throw std::invalid_argument("TIME_INVALID");
    std::ostringstream result; result << out << '.' << std::setfill('0') << std::setw(3) << epoch_ms % 1000 << 'Z';
    return result.str();
}
std::string credential_request(const std::string& secret) {
    if (secret.empty()) throw std::invalid_argument("AOS_SECRET_UNAVAILABLE");
    auto request = "{\"protocol\":\"aos-kuksa-auth-compat/v1\",\"operation\":\"issue\",\"aosSecret\":" + quote_json(secret) + "}\n";
    if (request.size() > 16384) throw std::invalid_argument("AOS_SECRET_INVALID");
    return request;
}
Credential parse_credential(const std::string& response, std::int64_t now) {
    if (response.empty() || response.back() != '\n' || response.size() > 32768) throw std::invalid_argument("KAC_RESPONSE_INVALID");
    const auto json = parse_json(response, 32768);
    if (json.at("protocol").string() != "aos-kuksa-auth-compat/v1" || json.at("correlationId").string().empty()) throw std::invalid_argument("KAC_RESPONSE_INVALID");
    Credential result;
    if (json.at("status").string() == "rejected") {
        if (json.object().size() != 5) throw std::invalid_argument("KAC_RESPONSE_INVALID");
        result.code = json.at("code").string(); result.retryable = json.at("retryable").boolean();
        const std::vector<std::string> retryable{"IAM_UNAVAILABLE", "SIGNER_UNAVAILABLE", "TIME_UNTRUSTED", "BUSY"};
        const std::vector<std::string> terminal{"INVALID_REQUEST", "DENIED", "POLICY_UNSUPPORTED", "INTERNAL_ERROR"};
        const bool retry = std::find(retryable.begin(), retryable.end(), result.code) != retryable.end();
        if (result.retryable != retry || (!retry && std::find(terminal.begin(), terminal.end(), result.code) == terminal.end())) throw std::invalid_argument("KAC_RESPONSE_INVALID");
        return result;
    }
    if (json.at("status").string() != "issued" || json.object().size() != 6) throw std::invalid_argument("KAC_RESPONSE_INVALID");
    result.token = json.at("token").string(); result.expires = json.at("expiresAtUnixSeconds").integer(); result.renew_after = json.at("renewAfterUnixSeconds").integer();
    if (result.token.empty() || result.token.size() > 16384 || result.token.front() == '.' || result.token.back() == '.' ||
        result.token.find("..") != std::string::npos || result.expires <= now || result.expires > now + 300 ||
        result.expires - result.renew_after != 120 || result.renew_after <= now ||
        std::count(result.token.begin(), result.token.end(), '.') != 2 ||
        !std::all_of(result.token.begin(), result.token.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'; })) throw std::invalid_argument("KAC_RESPONSE_INVALID");
    return result;
}
bool retryable_http(int status) {
    return status == 0 || status == 408 || status == 425 || status == 429 || status == 500 || status == 502 || status == 503 || status == 504;
}
int retry_delay(unsigned attempt, double jitter, int retry_after) {
    if (!std::isfinite(jitter) || jitter < -0.2 || jitter > 0.2 || retry_after < 0) throw std::invalid_argument("RETRY_INVALID");
    int base = std::min(30, 1 << std::min(attempt, 5U));
    return std::max(retry_after, std::clamp(static_cast<int>(std::ceil(base * (1.0 + jitter))), 1, 30));
}

std::string canonical(const Json& json) {
    if (std::holds_alternative<std::nullptr_t>(json.value)) return "null";
    if (const auto* v=std::get_if<bool>(&json.value)) return *v ? "true" : "false";
    if (const auto* v=std::get_if<std::int64_t>(&json.value)) {
        if (*v > 9007199254740991LL || *v < -9007199254740991LL) throw std::invalid_argument("INTEGER_RANGE");
        return std::to_string(*v);
    }
    if (const auto* v=std::get_if<std::string>(&json.value)) return quote_json(*v);
    if (const auto* v=std::get_if<Json::Array>(&json.value)) {
        std::string result="["; for (const auto& item:*v) {if(result.size()>1)result+=','; result+=canonical(item);} return result+']';
    }
    if (const auto* v=std::get_if<Json::Object>(&json.value)) {
        std::string result="{"; for (const auto& [key,item]:*v) {
            // Tire's closed schemas use ASCII property names and integer fields.
            if (!std::all_of(key.begin(),key.end(),[](unsigned char c){return c<128;})) throw std::invalid_argument("NON_SCHEMA_KEY");
            if(result.size()>1)result+=','; result+=quote_json(key)+':'+canonical(item);
        } return result+'}';
    }
    throw std::invalid_argument("NON_SCHEMA_NUMBER");
}
std::string message_key(const Json& msg) {
    const auto kind=msg.at("messageType").string();
    const auto field=kind=="TIRE_HEALTH_ASSESSMENT" ? "assessmentId" : kind=="TIRE_CONDITION_BAND_CHANGED" ? "eventId" : kind=="TIRE_ADVISORY_FACT" ? "requestId" : kind=="TIRE_FUNCTION_STATUS" ? "statusId" : "";
    if (!*field) throw std::invalid_argument("MESSAGE_KIND_INVALID");
    Json::Array key{msg.at("unitSystemUid"), Json{kind}, msg.at(field)};
    if(kind=="TIRE_ADVISORY_FACT")key.push_back(msg.at("gatewayState"));
    return sha256_hex(canonical(Json{key}));
}
bool matches_ack(const std::string& bytes,const HttpResponse& response) {
    if(response.status!=200 && response.status!=201)return false;
    try {
        const auto msg=parse_json(bytes,16384),ack=parse_json(response.body,8192);
        return ack.object().size()==7 && ack.at("schemaVersion").integer()==1 && ack.at("contractVersion").string()=="1.0.0" &&
          is_uuid(ack.at("receiptId").string()) && date_time(ack.at("receivedAt").string()) &&
          (ack.at("state").string()=="DURABLE_ACCEPTED" || ack.at("state").string()=="DUPLICATE_ACCEPTED") &&
          ack.at("messageKeySha256").string()==message_key(msg) && ack.at("contentSha256").string()==msg.at("contentSha256").string();
    } catch(...){return false;}
}
std::optional<Frame> complete_frame(const std::array<Signal,15>& values,std::int64_t wall_ms) {
    Frame result; result.epoch_ms=values[0].epoch_ms;
    for(std::size_t i=0;i<values.size();++i) {
        if(!values[i].valid || !std::isfinite(values[i].value) || values[i].epoch_ms!=result.epoch_ms || wall_ms<result.epoch_ms || wall_ms-result.epoch_ms>250)return std::nullopt;
        result.values[i]=values[i].value;
    }
    if(result.values[0]<0 || std::any_of(result.values.begin()+3,result.values.begin()+7,[](double v){return v<0;}))return std::nullopt;
    return result;
}
} // namespace tire_health::runtime
