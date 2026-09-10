// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#include "tire_health/service.hpp"
#include "tire_health/runtime/sha256.hpp"
#include <algorithm>
#include <stdexcept>
namespace tire_health {
using namespace runtime;
namespace {
Json n(std::int64_t v){return Json{v};}Json s(const std::string& v){return Json{v};}
ModelState read_model(const Json& value) {
 ModelState state;if(std::holds_alternative<std::nullptr_t>(value.value))return state;
 state.band=parse_band(value.at("conditionBand").string());state.better=value.at("betterBandCandidate").string()=="NONE"?Band::NotEvaluated:parse_band(value.at("betterBandCandidate").string());
 state.score=static_cast<int>(value.at("conditionScore").integer());state.confidence=static_cast<int>(value.at("confidencePercent").integer());state.better_count=static_cast<int>(value.at("betterBandEpisodeCount").integer());state.last_source=value.at("lastAppliedSourceExerciseId").string();state.last_assessment=value.at("lastAssessmentId").string();
 for(const auto& id:std::get<Json::Array>(value.at("recentSourceExerciseIds").value))state.recent.push_back(id.string());return state;
}
Json write_model(const ModelState& m,const std::string& digest,const Json& state) {
 Json::Array recent;for(const auto& id:m.recent)recent.push_back(s(id));
 return Json{Json::Object{{"schemaVersion",n(1)},{"modelId",s("tire-condition-demo-v1")},{"modelConfigSha256",s(digest)},{"conditionScore",n(m.score)},{"conditionBand",s(band_name(m.band))},{"confidencePercent",n(m.confidence)},{"betterBandCandidate",s(m.better==Band::NotEvaluated?"NONE":band_name(m.better))},{"betterBandEpisodeCount",n(m.better_count)},{"lastAppliedSourceExerciseId",s(m.last_source)},{"lastAssessmentId",s(m.last_assessment)},{"recentSourceExerciseIds",Json{recent}},{"producerEpoch",state.at("producerEpoch")},{"nextAdvisorySequence",state.at("nextAdvisorySequence")}}};
}
}
Runtime::Runtime(std::filesystem::path state,std::filesystem::path outbox,Metadata metadata):metadata_(std::move(metadata)) {
 try {store_=std::make_unique<StateStore>(std::move(state),std::move(outbox),metadata_.unit_system_uid);}catch(...){reason_="NOT_READY_STATE";}
}
std::optional<Episode> Runtime::ingest(const Frame& frame){std::lock_guard<std::mutex> lock(mutex_);return engine_.ingest(frame);}
void Runtime::disconnect(){std::lock_guard<std::mutex> lock(mutex_);engine_.abort("INCOMPLETE_SOURCE_GAP");}
void Runtime::stop(){std::lock_guard<std::mutex> lock(mutex_);engine_.abort("ABORTED_SERVICE_STOP");}
void Runtime::update_vdp_metadata(const Metadata& m) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(m.unit_system_uid!=metadata_.unit_system_uid||m.unit_role!=metadata_.unit_role||m.service_version!=metadata_.service_version||m.service_artifact_sha256!=metadata_.service_artifact_sha256)throw std::invalid_argument("IMMUTABLE_IDENTITY_CHANGED");
 if(m.vdp_contract_version!=metadata_.vdp_contract_version||m.vdp_contract_sha256!=metadata_.vdp_contract_sha256){engine_.abort("INCOMPLETE_SOURCE_GAP");metadata_=m;}
}
void Runtime::function_status(const std::string& reason,std::int64_t now,const std::vector<std::string>& missing) {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_)return;
 const std::vector<std::string> allowed{"READY","BACKEND_SYNC_PENDING","BACKEND_DISCONNECTED","OUTBOX_FULL","INCOMPATIBLE_VDP","TELEMETRY_STALE","TELEMETRY_DISCONNECTED","SERVICE_ACCESS_DENIED","NOT_READY_STATE"};
 if(std::find(allowed.begin(),allowed.end(),reason)==allowed.end()||missing.size()>32)throw std::invalid_argument("STATUS_INVALID");
 if(reason==reason_&&status_at_>=0&&now-status_at_<30000)return;
 Json::Array paths_json;for(const auto& path:missing)paths_json.push_back(s(path));
 const bool degraded=reason=="BACKEND_DISCONNECTED"||reason=="BACKEND_SYNC_PENDING"||reason=="OUTBOX_FULL";
 const Json content{Json::Object{{"functionalState",s(reason=="READY"?"OPERATIONAL":degraded?"DEGRADED":"NOT_READY")},{"reason",s(reason)},{"requiredVdpContractVersion",s("3.0.0")},{"missingPaths",Json{paths_json}},{"missingCapabilities",Json{Json::Array{}}}}};
 const auto observed=utc_timestamp(now);
 Json::Object msg{{"schemaVersion",n(1)},{"contractVersion",s("1.0.0")},{"messageType",s("TIRE_FUNCTION_STATUS")},{"statusId",s(uuid_v5(store_->state().at("producerEpoch").string(),{observed,reason,metadata_.service_version,metadata_.vdp_contract_version,canonical(content)}))},{"unitSystemUid",s(metadata_.unit_system_uid)},{"unitRole",s(metadata_.unit_role)},{"serviceVersion",s(metadata_.service_version)},{"serviceArtifactSha256",s(metadata_.service_artifact_sha256)},{"actualVdpContractVersion",s(metadata_.vdp_contract_version)},{"actualVdpContractSha256",s(metadata_.vdp_contract_sha256)},{"observedAt",s(observed)},{"content",content},{"contentSha256",s(sha256_hex(canonical(content)))}};
 store_->commit(store_->state(),{Json{msg}});reason_=reason;status_at_=now;
}
std::optional<Pending> Runtime::next_message(){std::lock_guard<std::mutex> lock(mutex_);return store_?store_->pending():std::nullopt;}
bool Runtime::accept(const Pending& p,const HttpResponse& r){std::lock_guard<std::mutex> lock(mutex_);return store_&&store_->acknowledge(p,r);}
bool Runtime::apply_episode(const Features& features,const Episode& episode,const std::string& model_digest) {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_||!is_sha256(model_digest)||(episode.terminal!="COMPLETE"&&episode.terminal!="TRUNCATED_MAX_DURATION"))return false;
 auto next=store_->state().object();auto model=read_model(next.at("model"));
 if(!std::holds_alternative<std::nullptr_t>(next.at("model").value)&&next.at("model").at("modelConfigSha256").string()!=model_digest)throw std::runtime_error("MODEL_STATE_MISMATCH");
 const auto result=assess(model,features,static_cast<int>(episode.samples.size()),episode.id);if(!result)return false;
 const auto message=assessment_message(metadata_,model,*result,episode,model_digest);model.last_assessment=message.at("assessmentId").string();
 std::vector<Json> messages;
 if(result->changed||episode.ended-next.at("lastPublishedAt").integer()>=30000){messages.push_back(message);if(result->changed)messages.push_back(event_message(message));next["lastPublishedAt"]=n(episode.ended);}
 next["model"]=write_model(model,model_digest,Json{next});
 // A changed accepted band schedules a new lease; no KUKSA side effect is
 // performed until the new model and producer sequence are durable.
 if(result->changed && (result->current!=Band::Good || result->previous!=Band::NotEvaluated)) {
  const auto sequence=next.at("nextAdvisorySequence").integer();next["lastRequest"]=advisory_request(metadata_,next.at("producerEpoch").string(),sequence,model.last_assessment,model.band,episode.ended);
  next["nextAdvisorySequence"]=n(sequence+1);next["model"]=write_model(model,model_digest,Json{next});next["gatewayStates"]=Json{Json::Array{}};advisory_sent_=false;refresh_at_=episode.ended;last_write_=-1;
 }
 return store_->commit(Json{next},messages);
}
std::optional<std::string> Runtime::next_advisory(std::int64_t now) {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_)return std::nullopt;
 auto next=store_->state().object();const auto model=read_model(next.at("model"));if(model.band==Band::NotEvaluated)return std::nullopt;
 if(!std::holds_alternative<std::nullptr_t>(next.at("lastRequest").value)&&refresh_at_>=0&&now-refresh_at_<20000){if(!advisory_sent_ && (last_write_<0 || now-last_write_>=1000)){last_write_=now;return canonical(next.at("lastRequest"));}return std::nullopt;}
 if(model.band==Band::Good && (advisory_sent_ || std::holds_alternative<std::nullptr_t>(next.at("lastRequest").value)))return std::nullopt;
 const auto sequence=next.at("nextAdvisorySequence").integer();
 const auto request=advisory_request(metadata_,next.at("producerEpoch").string(),sequence,model.last_assessment,model.band,now);
 next["nextAdvisorySequence"]=n(sequence+1);next["lastRequest"]=request;next["gatewayStates"]=Json{Json::Array{}};next["model"].value=write_model(model,next.at("model").at("modelConfigSha256").string(),Json{next}).value;
 store_->commit(Json{next},{});refresh_at_=now;last_write_=now;advisory_sent_=false;return canonical(request);
}
void Runtime::gateway_status(const std::string& bytes,std::int64_t now) {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_||std::holds_alternative<std::nullptr_t>(store_->state().at("lastRequest").value))return;
 const auto status=parse_json(bytes,4096),request=store_->state().at("lastRequest");const auto fact=advisory_fact(metadata_,request,status,now);
 auto next=store_->state().object();auto states=std::get<Json::Array>(next.at("gatewayStates").value);const auto current=status.at("state").string();
 if(std::any_of(states.begin(),states.end(),[&](const Json& v){return v.string()==current;}))return;
 states.push_back(s(current));next["gatewayStates"]=Json{states};store_->commit(Json{next},{fact});
 if(current=="APPLIED"||current=="CLEARED"||current=="REJECTED"||current=="FAILED")advisory_sent_=true;
}
} // namespace tire_health
