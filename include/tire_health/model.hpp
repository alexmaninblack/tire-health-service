// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "tire_health/runtime/runtime.hpp"
#include <functional>

namespace tire_health {
enum class Band {NotEvaluated=-1, Good=0, Inspection=1, Replacement=2};
const char* band_name(Band);
Band parse_band(const std::string&);
struct Features {int longitudinal{}, lateral{}, dispersion{}, persistence{};};
struct Assessment {Features features; int samples{}, load{}, score{}, confidence{}; Band previous{}, current{}; bool changed{};};
struct ModelState {Band band=Band::NotEvaluated, better=Band::NotEvaluated; int score{}, confidence{}, better_count{}; std::string last_source, last_assessment; std::vector<std::string> recent;};
std::optional<Assessment> assess(ModelState&, const Features&, int samples, const std::string& source_id);
// Source timing and retention are specified independently from the unresolved
// dispersion/persistence extraction. This engine never guesses feature values.
struct Episode {std::string id; std::int64_t started{}, ended{}; std::vector<runtime::Frame> samples; std::string terminal;};
class EpisodeEngine {
 public:
  explicit EpisodeEngine(std::function<std::string()> uuid=runtime::random_uuid): uuid_(std::move(uuid)) {}
  std::optional<Episode> ingest(const runtime::Frame&);
  std::optional<Episode> abort(const std::string& reason);
  bool active() const {return current_.has_value();}
 private:
  std::function<std::string()> uuid_; std::optional<Episode> current_;
  std::int64_t activation_=-1, clear_=-1, previous_=-1, retained_=-1; bool suppressed_{};
};
runtime::Json assessment_message(const runtime::Metadata&, const ModelState&, const Assessment&, const Episode&, const std::string& model_digest);
runtime::Json event_message(const runtime::Json& assessment);
runtime::Json advisory_request(const runtime::Metadata&, const std::string& epoch, std::int64_t sequence, const std::string& assessment_id, Band, std::int64_t now);
runtime::Json advisory_fact(const runtime::Metadata&, const runtime::Json& request, const runtime::Json& status, std::int64_t now);
} // namespace tire_health
