// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#include "tire_health/service.hpp"
#include "tire_health/runtime/application.hpp"
#include "tire_health/runtime/sha256.hpp"
#include <algorithm>
#include <stdexcept>
namespace tire_health {
using namespace runtime;
namespace {
Json n(std::int64_t v){return Json{v};}Json s(const std::string& v){return Json{v};}
bool is_null(const Json& value){return std::holds_alternative<std::nullptr_t>(value.value);}
Json control_binding(const Metadata& metadata,const Json& state) {
 if(!metadata.service_instance)throw std::runtime_error("RESET_NATIVE_IDENTITY_REQUIRED");
 return Json{Json::Object{{"schemaVersion",n(1)},{"unitSystemUid",s(metadata.unit_system_uid)},
  {"serviceVersion",s(metadata.service_version)},{"serviceInstance",parse_json(service_instance_json(*metadata.service_instance))},
  {"producerEpoch",state.at("producerEpoch")}}};
}
bool reset_pending(const Json& state) {
 return state.object().count("demoReset") && is_null(state.at("demoReset").at("ack"));
}
ModelState read_model(const Json& value) {
 ModelState state;if(std::holds_alternative<std::nullptr_t>(value.value))return state;
 state.band=parse_band(value.at("conditionBand").string());state.better=value.at("betterBandCandidate").string()=="NONE"?Band::NotEvaluated:parse_band(value.at("betterBandCandidate").string());
 state.score=static_cast<int>(value.at("conditionScore").integer());state.confidence=static_cast<int>(value.at("confidencePercent").integer());state.better_count=static_cast<int>(value.at("betterBandEpisodeCount").integer());state.last_source=value.at("lastAppliedSourceExerciseId").string();state.last_assessment=value.at("lastAssessmentId").string();
 for(const auto& id:std::get<Json::Array>(value.at("recentSourceExerciseIds").value))state.recent.push_back(id.string());
 return state;
}
Json write_model(const ModelState& m,const std::string& digest,const Json& state) {
 Json::Array recent;for(const auto& id:m.recent)recent.push_back(s(id));
 return Json{Json::Object{{"schemaVersion",n(1)},{"modelId",s("tire-condition-demo-v1")},{"modelConfigSha256",s(digest)},{"conditionScore",n(m.score)},{"conditionBand",s(band_name(m.band))},{"confidencePercent",n(m.confidence)},{"betterBandCandidate",s(m.better==Band::NotEvaluated?"NONE":band_name(m.better))},{"betterBandEpisodeCount",n(m.better_count)},{"lastAppliedSourceExerciseId",s(m.last_source)},{"lastAssessmentId",s(m.last_assessment)},{"recentSourceExerciseIds",Json{recent}},{"producerEpoch",state.at("producerEpoch")},{"nextAdvisorySequence",state.at("nextAdvisorySequence")}}};
}
}
Runtime::Runtime(std::filesystem::path state,std::filesystem::path outbox,Metadata metadata):metadata_(std::move(metadata)) {
 try {store_=std::make_unique<StateStore>(std::move(state),std::move(outbox),metadata_.unit_system_uid);}catch(...){reason_="NOT_READY_STATE";}
 if(store_&&reset_pending(store_->state())) {
  const auto binding=control_binding(metadata_,store_->state());auto next=store_->state().object();
  auto reset=next.at("demoReset").object();const auto& command=reset.at("command");bool matches=true;
  for(const auto& field:binding.object())if(canonical(command.at(field.first))!=canonical(field.second))matches=false;
  if(!matches) {
   auto ack=command.object();ack.erase("operation");ack.erase("issuedAt");ack.erase("expiresAt");
   ack["result"]=s("REJECTED");ack["clearRequest"]=Json{nullptr};ack["gatewayStatus"]=Json{nullptr};
   reset["ack"]=Json{ack};next["demoReset"]=Json{reset};store_->commit(Json{next},{});
  }
 }
}
std::optional<Episode> Runtime::ingest(const Frame& frame){
 std::lock_guard<std::mutex> lock(mutex_);function_.input("CONNECTED","RECEIVING","NONE");
 if(store_&&reset_pending(store_->state())){function_.activity("WAITING","RESET");return std::nullopt;}
 const auto episode=engine_.ingest(frame);
 if(episode)function_.activity("SKIPPED","INSUFFICIENT_SAMPLES",episode->id);
 else function_.activity(engine_.active()?"ACTIVE":"WAITING",engine_.active()?"NONE":"NOT_QUALIFIED",engine_.activity_id());
 return episode;
}
void Runtime::disconnect(){std::lock_guard<std::mutex> lock(mutex_);telemetry_at_=-1;engine_.abort("INCOMPLETE_SOURCE_GAP");function_.interruption("SOURCE_DISCONTINUITY");function_.input("DISCONNECTED","DISCONNECTED","TRANSPORT_LOST");}
void Runtime::stop(){std::lock_guard<std::mutex> lock(mutex_);telemetry_at_=-1;engine_.abort("ABORTED_SERVICE_STOP");}
void Runtime::update_vdp_metadata(const Metadata& m) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(m.unit_system_uid!=metadata_.unit_system_uid||m.unit_role!=metadata_.unit_role||m.service_version!=metadata_.service_version||m.service_artifact_sha256!=metadata_.service_artifact_sha256||m.service_instance!=metadata_.service_instance)throw std::invalid_argument("IMMUTABLE_IDENTITY_CHANGED");
 if(m.vdp_contract_version!=metadata_.vdp_contract_version||m.vdp_contract_sha256!=metadata_.vdp_contract_sha256){engine_.abort("INCOMPLETE_SOURCE_GAP");metadata_=m;}
}
void Runtime::function_status(const std::string& reason,std::int64_t now,const std::vector<std::string>& missing) {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_)return;
 const std::vector<std::string> allowed{"READY","BACKEND_SYNC_PENDING","BACKEND_DISCONNECTED","OUTBOX_FULL","INCOMPATIBLE_VDP","TELEMETRY_STALE","TELEMETRY_DISCONNECTED","SERVICE_ACCESS_DENIED","NOT_READY_STATE"};
 if(std::find(allowed.begin(),allowed.end(),reason)==allowed.end()||missing.size()>32)throw std::invalid_argument("STATUS_INVALID");
 if(reason=="READY")telemetry_at_=now;
 if(reason==reason_&&status_at_>=0&&now-status_at_<30000)return;
 Json::Array paths_json;for(const auto& path:missing)paths_json.push_back(s(path));
 const bool degraded=reason=="BACKEND_DISCONNECTED"||reason=="BACKEND_SYNC_PENDING"||reason=="OUTBOX_FULL";
 const Json content{Json::Object{{"functionalState",s(reason=="READY"?"OPERATIONAL":degraded?"DEGRADED":"NOT_READY")},{"reason",s(reason)},{"requiredVdpContractVersion",s("3.0.0")},{"missingPaths",Json{paths_json}},{"missingCapabilities",Json{Json::Array{}}}}};
 const auto observed=utc_timestamp(now);
 Json::Object msg{{"schemaVersion",n(1)},{"contractVersion",s("1.0.0")},{"messageType",s("TIRE_FUNCTION_STATUS")},{"statusId",s(uuid_v5(store_->state().at("producerEpoch").string(),{observed,reason,metadata_.service_version,metadata_.vdp_contract_version,canonical(content)}))},{"unitSystemUid",s(metadata_.unit_system_uid)},{"unitRole",s(metadata_.unit_role)},{"serviceVersion",s(metadata_.service_version)},{"serviceArtifactSha256",s(metadata_.service_artifact_sha256)},{"actualVdpContractVersion",s(metadata_.vdp_contract_version)},{"actualVdpContractSha256",s(metadata_.vdp_contract_sha256)},{"observedAt",s(observed)},{"content",content},{"contentSha256",s(sha256_hex(canonical(content)))}};
 if(metadata_.service_instance) {
  (void)metadata_binding(metadata_);
  msg["schemaVersion"]=n(2);msg["contractVersion"]=s("2.0.0");msg.erase("serviceArtifactSha256");
  msg.emplace("serviceInstance",parse_json(service_instance_json(*metadata_.service_instance)));
 }
 store_->commit(store_->state(),{Json{msg}});reason_=reason;status_at_=now;
}
std::optional<Pending> Runtime::next_message(){std::lock_guard<std::mutex> lock(mutex_);return store_?store_->pending():std::nullopt;}
bool Runtime::accept(const Pending& p,const HttpResponse& r){std::lock_guard<std::mutex> lock(mutex_);const bool accepted=store_&&store_->acknowledge(p,r);function_.delivery(accepted,r.status,r.body);return accepted;}
bool Runtime::apply_episode(const Features& features,const Episode& episode,const std::string& model_digest) {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_||reset_pending(store_->state())||!is_sha256(model_digest)||(episode.terminal!="COMPLETE"&&episode.terminal!="TRUNCATED_MAX_DURATION"))return false;
 auto next=store_->state().object();auto model=read_model(next.at("model"));
 // A reset never makes already consumed exercises new inputs.
 if(next.count("demoReset")&&!is_null(next.at("demoReset").at("previousModel"))) {
  const auto& recent=std::get<Json::Array>(next.at("demoReset").at("previousModel").at("recentSourceExerciseIds").value);
  if(std::any_of(recent.begin(),recent.end(),[&](const Json& id){return id.string()==episode.id;}))return false;
  if(is_null(next.at("model")))for(const auto& id:recent)model.recent.push_back(id.string());
 }
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
  next["lastRequestMetadata"]=metadata_binding(metadata_);
  next["nextAdvisorySequence"]=n(sequence+1);next["model"]=write_model(model,model_digest,Json{next});next["gatewayStates"]=Json{Json::Array{}};advisory_sent_=false;refresh_at_=episode.ended;last_write_=-1;
 }
 const bool committed=store_->commit(Json{next},messages);
 function_.activity(committed?"COMPLETED":"SKIPPED",committed?"NONE":"STORAGE_UNAVAILABLE",episode.id);
 if(committed&&!messages.empty())function_.result("ASSESSMENT",message.at("assessmentId").string(),message.at("sourceEventTime").string(),metadata_.service_version);
 return committed;
}
std::optional<std::string> Runtime::next_advisory(std::int64_t now) {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_)return std::nullopt;
 if(reset_pending(store_->state())) {
  auto next=store_->state().object();
  const auto& command=next.at("demoReset").at("command");
  if(utc_timestamp(now)>=command.at("expiresAt").string())return std::nullopt;
  if(last_write_>=0&&now-last_write_<1000)return std::nullopt;
  // A recovered/late request receives a fresh sequence, not a stale replay.
  if(refresh_at_<0||now-refresh_at_>=5000) {
   const auto sequence=next.at("nextAdvisorySequence").integer();
   next["lastRequest"]=advisory_request(metadata_,next.at("producerEpoch").string(),sequence,command.at("commandId").string(),Band::Good,now);
   next["lastRequestMetadata"]=metadata_binding(metadata_);next["nextAdvisorySequence"]=n(sequence+1);
   next["gatewayStates"]=Json{Json::Array{}};store_->commit(Json{next},{});refresh_at_=now;
  }
  last_write_=now;function_.advisory("WAITING",next.at("lastRequest").at("requestId").string());return canonical(next.at("lastRequest"));
 }
 auto next=store_->state().object();const auto model=read_model(next.at("model"));if(model.band==Band::NotEvaluated)return std::nullopt;
 if(!std::holds_alternative<std::nullptr_t>(next.at("lastRequest").value)&&refresh_at_>=0&&now-refresh_at_<20000){if(!advisory_sent_ && (last_write_<0 || now-last_write_>=1000)){last_write_=now;function_.advisory("WAITING",next.at("lastRequest").at("requestId").string());return canonical(next.at("lastRequest"));}return std::nullopt;}
 if(model.band==Band::Good && (advisory_sent_ || std::holds_alternative<std::nullptr_t>(next.at("lastRequest").value)))return std::nullopt;
 const auto sequence=next.at("nextAdvisorySequence").integer();
 const auto request=advisory_request(metadata_,next.at("producerEpoch").string(),sequence,model.last_assessment,model.band,now);
 next["lastRequestMetadata"]=metadata_binding(metadata_);
 next["nextAdvisorySequence"]=n(sequence+1);next["lastRequest"]=request;next["gatewayStates"]=Json{Json::Array{}};next["model"].value=write_model(model,next.at("model").at("modelConfigSha256").string(),Json{next}).value;
 store_->commit(Json{next},{});refresh_at_=now;last_write_=now;advisory_sent_=false;function_.advisory("WAITING",request.at("requestId").string());return canonical(request);
}
void Runtime::gateway_status(const std::string& bytes,std::int64_t now) {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_||std::holds_alternative<std::nullptr_t>(store_->state().at("lastRequest").value))return;
 // Old state may contain a request without durable provenance. Retain it, but
 // never invent a fact by attaching the current package/instance to that request.
 // Normal next_advisory() creates a newly bound request using the same epoch.
 if(!store_->state().object().count("lastRequestMetadata"))return;
 const auto status=parse_json(bytes,4096),request=store_->state().at("lastRequest");
 validate_gateway_status(status);
 // A retained or superseded ACK is not a reply to the current request. It
 // cannot alter the model, readiness, reset outcome, outbox or sequence.
 for(const auto* field:{"requestId","producerEpoch","sequence"})
  if(canonical(request.at(field))!=canonical(status.at(field)))return;
 const auto fact=advisory_fact(parse_metadata_binding(store_->state().at("lastRequestMetadata")),request,status,now);
 if(store_->state().object().count("demoReset")&&request.at("decisionId").string()==store_->state().at("demoReset").at("command").at("commandId").string()) {
  if(!reset_pending(store_->state()))return;
  if(status.at("state").string()!="CLEARED")return;
  if(status.at("reason").string()!="NONE"||status.at("activeRecommendation").string()!="NONE"||
     status.at("activeReasonCode").string()!="NONE"||!is_null(status.at("activeUntil")))throw std::runtime_error("RESET_CLEAR_NOT_CONFIRMED");
  auto next=store_->state().object();auto reset=next.at("demoReset").object();
  auto ack=control_binding(metadata_,store_->state()).object();
  ack["commandId"]=reset.at("command").at("commandId");ack["result"]=s("CLEARED");
  ack["clearRequest"]=request;ack["gatewayStatus"]=status;reset["ack"]=Json{ack};
  next["demoReset"]=Json{reset};next["gatewayStates"]=Json{Json::Array{s("CLEARED")}};
  store_->commit(Json{next},{});advisory_sent_=true;function_.advisory("CONFIRMED",request.at("requestId").string());return;
 }
 auto next=store_->state().object();auto states=std::get<Json::Array>(next.at("gatewayStates").value);const auto current=status.at("state").string();
 if(std::any_of(states.begin(),states.end(),[&](const Json& v){return v.string()==current;}))return;
 states.push_back(s(current));next["gatewayStates"]=Json{states};store_->commit(Json{next},{fact});
 function_.advisory(current=="APPLIED"||current=="CLEARED"?"CONFIRMED":current=="RECEIVED"?"WAITING":"UNAVAILABLE",request.at("requestId").string());
 if(current=="APPLIED"||current=="CLEARED"||current=="REJECTED"||current=="FAILED")advisory_sent_=true;
}
std::optional<std::string> Runtime::demo_control_poll() {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_||!metadata_.service_instance)return std::nullopt;
 return canonical(control_binding(metadata_,store_->state()));
}
void Runtime::demo_control_command(const std::string& bytes,std::int64_t now) {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_)throw std::runtime_error("RESET_STATE_UNAVAILABLE");
 const auto response=parse_json(bytes,4096);
 if(response.object().size()!=2||response.at("schemaVersion").integer()!=1)throw std::runtime_error("RESET_INVALID_RESPONSE");
 const auto& command=response.at("command");if(is_null(command))return;
 if(command.object().size()!=9||command.at("operation").string()!="RESET_DEMO_SCENARIO"||
    !is_uuid(command.at("commandId").string()))throw std::runtime_error("RESET_INVALID_COMMAND");
 const auto binding=control_binding(metadata_,store_->state());
 for(const auto& entry:binding.object()) {
  if(canonical(command.at(entry.first))!=canonical(entry.second))throw std::runtime_error("RESET_BINDING_MISMATCH");
 }
 auto next=store_->state().object();
 if(next.count("demoReset")&&next.at("demoReset").at("command").at("commandId").string()==command.at("commandId").string()) {
  if(canonical(next.at("demoReset").at("command"))!=canonical(command))throw std::runtime_error("RESET_COMMAND_CONFLICT");
  return;
 }
 const auto issued=command.at("issuedAt").string(),expires=command.at("expiresAt").string();
 if(!date_time(issued)||!date_time(expires)||issued.size()!=24||expires.size()!=24||issued.back()!='Z'||expires.back()!='Z'||
    issued>utc_timestamp(now)||expires<=utc_timestamp(now)||expires>utc_timestamp(now+60000)||issued>=expires)
  throw std::runtime_error("RESET_COMMAND_EXPIRED");
 if(reset_pending(store_->state()))throw std::runtime_error("RESET_ALREADY_PENDING");
 const auto sequence=next.at("nextAdvisorySequence").integer();
 next["demoReset"]=Json{Json::Object{{"command",command},{"previousModel",next.at("model")},{"ack",Json{nullptr}},{"delivered",Json{false}}}};
 next["model"]=Json{nullptr};next["lastPublishedAt"]=n(0);
 next["lastRequest"]=advisory_request(metadata_,next.at("producerEpoch").string(),sequence,command.at("commandId").string(),Band::Good,now);
 next["lastRequestMetadata"]=metadata_binding(metadata_);next["nextAdvisorySequence"]=n(sequence+1);next["gatewayStates"]=Json{Json::Array{}};
 store_->commit(Json{next},{});engine_=EpisodeEngine{};refresh_at_=now;last_write_=-1;advisory_sent_=false;
 function_.activity("WAITING","RESET");
}
std::optional<std::string> Runtime::demo_control_ack(std::int64_t now) {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_||!store_->state().object().count("demoReset"))return std::nullopt;
 auto next=store_->state().object();auto reset=next.at("demoReset").object();
 if(reset.at("delivered").boolean())return std::nullopt;
 if(is_null(reset.at("ack"))) {
  if(utc_timestamp(now)<reset.at("command").at("expiresAt").string())return std::nullopt;
  auto ack=reset.at("command").object();ack.erase("operation");ack.erase("issuedAt");ack.erase("expiresAt");
  ack["result"]=s("FAILED");ack["clearRequest"]=Json{nullptr};ack["gatewayStatus"]=Json{nullptr};
  reset["ack"]=Json{ack};next["demoReset"]=Json{reset};store_->commit(Json{next},{});
 }
 return canonical(reset.at("ack"));
}
void Runtime::demo_control_accepted(const std::string& bytes) {
 std::lock_guard<std::mutex> lock(mutex_);if(!store_||!store_->state().object().count("demoReset"))return;
 auto next=store_->state().object();auto reset=next.at("demoReset").object();const auto response=parse_json(bytes,4096);
 if(is_null(reset.at("ack"))||response.object().size()!=3||response.at("schemaVersion").integer()!=1||
    response.at("commandId").string()!=reset.at("command").at("commandId").string()||
    response.at("state").string()!=reset.at("ack").at("result").string())throw std::runtime_error("RESET_ACK_INVALID");
 reset["delivered"]=Json{true};next["demoReset"]=Json{reset};store_->commit(Json{next},{});
}
std::string Runtime::advisory_readiness(std::int64_t now) {
 std::lock_guard<std::mutex> lock(mutex_);
 return canonical(Json{Json::Object{{"schemaVersion",n(1)},{"ready",Json{static_cast<bool>(store_)&&telemetry_at_>=0&&now>=telemetry_at_&&now-telemetry_at_<=5000}},{"observedAt",s(utc_timestamp(now))}}});
}
std::optional<Json> Runtime::observation_binding() {
 std::lock_guard<std::mutex> lock(mutex_);if(!metadata_.service_instance)return std::nullopt;
 return Json{Json::Object{{"messageType",s("TIRE_FUNCTION_OBSERVATION")},{"unitSystemUid",s(metadata_.unit_system_uid)},
  {"unitRole",s(metadata_.unit_role)},{"serviceVersion",s(metadata_.service_version)},{"serviceProfile",s("v1")},
  {"serviceInstance",parse_json(service_instance_json(*metadata_.service_instance))}}};
}
Json Runtime::function_observation(){
 std::lock_guard<std::mutex> lock(mutex_);return function_.snapshot(store_?store_->queued():0,!store_);
}
void Runtime::input_observation(const std::string& connection,const std::string& state,const std::string& reason) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(state!="RECEIVING")function_.interruption(reason=="REAUTHENTICATING"?"REAUTHENTICATING":"SOURCE_DISCONTINUITY");
 function_.input(connection,state,reason);
}
void Runtime::advisory_observation(const std::string& state){
 std::lock_guard<std::mutex> lock(mutex_);function_.advisory(state);
}
} // namespace tire_health
