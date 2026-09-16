// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#include "tire_health/model.hpp"
#include "tire_health/runtime/application.hpp"
#include "tire_health/runtime/sha256.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace tire_health {
using namespace runtime;
namespace {
Json number(std::int64_t n){return Json{n};}
Json text(const std::string& s){return Json{s};}
Json wrap(Json::Object message,Json content) {message["content"]=content;message["contentSha256"]=text(sha256_hex(canonical(content)));return Json{message};}
Json::Object base(const Metadata& m,const std::string& type) {
 Json::Object message{{"schemaVersion",number(1)},{"contractVersion",text("1.0.0")},{"messageType",text(type)},
 {"unitSystemUid",text(m.unit_system_uid)},{"unitRole",text(m.unit_role)},{"serviceVersion",text(m.service_version)},
 {"serviceArtifactSha256",text(m.service_artifact_sha256)},{"vdpContractVersion",text(m.vdp_contract_version)},{"vdpContractSha256",text(m.vdp_contract_sha256)}};
 if(m.service_instance) {
  (void)metadata_binding(m);
  message["schemaVersion"]=number(2);message["contractVersion"]=text("2.0.0");
  message.erase("serviceArtifactSha256");
  message.emplace("serviceInstance",parse_json(service_instance_json(*m.service_instance)));
 }
 return message;
}
}
const char* band_name(Band b) {switch(b){case Band::NotEvaluated:return "NOT_EVALUATED";case Band::Good:return "GOOD";case Band::Inspection:return "INSPECTION_RECOMMENDED";case Band::Replacement:return "REPLACEMENT_RECOMMENDED";}throw std::invalid_argument("BAND_INVALID");}
Band parse_band(const std::string& name){for(auto b:{Band::NotEvaluated,Band::Good,Band::Inspection,Band::Replacement})if(name==band_name(b))return b;throw std::invalid_argument("BAND_INVALID");}
std::optional<Features> extract_features(const Episode& episode) {
 if ((episode.terminal!="COMPLETE" && episode.terminal!="TRUNCATED_MAX_DURATION") ||
     episode.samples.size()<20 || episode.samples.size()>120) return {};
 double longitudinal=0, lateral=0, dispersion=0;
 std::size_t slipping=0;
 std::int64_t previous=-1;
 for(const auto& frame:episode.samples) {
  if(frame.epoch_ms<=previous || frame.epoch_ms<episode.started || frame.epoch_ms>episode.ended ||
     !std::all_of(frame.values.begin(),frame.values.end(),[](double v){return std::isfinite(v);}) ||
     frame.values[0]<0) return {};
  previous=frame.epoch_ms;
  const auto wheel=std::minmax_element(frame.values.begin()+3,frame.values.begin()+7);
  if(*wheel.first<0)return {};
  dispersion=std::max(dispersion,(*wheel.second-*wheel.first)/std::max(*wheel.second,5.0));
  bool slip=false;
  for(std::size_t i=7;i<11;++i) {
   longitudinal=std::max(longitudinal,std::abs(frame.values[i]));
   slip=slip || std::abs(frame.values[i])>=0.08;
  }
  for(std::size_t i=11;i<15;++i) {
   lateral=std::max(lateral,std::abs(frame.values[i]));
   slip=slip || std::abs(frame.values[i])>=4.0;
  }
  if(slip)++slipping;
 }
 const auto bps=[](double value,double normalization) {
  return static_cast<int>(std::floor(std::min(1.0,value/normalization)*10000.0+0.5));
 };
 const auto count=episode.samples.size();
 return Features{bps(longitudinal,0.20),bps(lateral,8.0),bps(dispersion,0.15),
     static_cast<int>((slipping*10000+count/2)/count)};
}
std::optional<Assessment> assess(ModelState& state,const Features& f,int samples,const std::string& source) {
 if(samples<20 || samples>120 || !is_uuid(source))return std::nullopt;
 for(auto n:{f.longitudinal,f.lateral,f.dispersion,f.persistence})if(n<0 || n>10000)return std::nullopt;
 if(std::find(state.recent.begin(),state.recent.end(),source)!=state.recent.end())return std::nullopt;
 Assessment a; a.features=f;a.samples=samples;a.previous=state.band;
 a.load=(30*f.longitudinal+30*f.lateral+20*f.dispersion+20*f.persistence+50)/100;
 a.score=100-(a.load+50)/100;a.confidence=std::min(100,(samples*100+20)/40);
 const auto raw=a.score>=70?Band::Good:a.score>=40?Band::Inspection:Band::Replacement;
 if(state.band==Band::NotEvaluated || raw>state.band){state.band=raw;state.better=Band::NotEvaluated;state.better_count=0;}
 else if(raw<state.band){if(state.better!=raw){state.better=raw;state.better_count=1;}else ++state.better_count;if(state.better_count>=3){state.band=raw;state.better=Band::NotEvaluated;state.better_count=0;}}
 else {state.better=Band::NotEvaluated;state.better_count=0;}
 state.score=a.score;state.confidence=a.confidence;state.last_source=source;state.recent.push_back(source);if(state.recent.size()>128)state.recent.erase(state.recent.begin());
 a.current=state.band;a.changed=a.previous!=a.current;return a;
}
std::optional<Episode> EpisodeEngine::abort(const std::string& reason) {
 auto result=current_;if(result)result->terminal=reason;current_.reset();activation_=clear_=retained_=-1;suppressed_=false;previous_=-1;return result;
}
std::optional<Episode> EpisodeEngine::ingest(const Frame& f) {
 if(!std::all_of(f.values.begin(),f.values.end(),[](double v){return std::isfinite(v);}))return abort("INCOMPLETE_SOURCE_GAP");
 if(previous_>=0 && f.epoch_ms==previous_)return std::nullopt;
 if(previous_>=0 && (f.epoch_ms<previous_ || f.epoch_ms-previous_>250)){auto stopped=abort("INCOMPLETE_SOURCE_GAP");previous_=f.epoch_ms;return stopped;}
 previous_=f.epoch_ms;
 const bool activating=f.values[0]>=12 && std::abs(f.values[2])>=3;
 const bool clearing=f.values[0]<=5 || std::abs(f.values[2])<=1;
 if(clearing){if(clear_<0)clear_=f.epoch_ms;}else clear_=-1;
 if(suppressed_){if(clear_>=0 && f.epoch_ms-clear_>=1000){suppressed_=false;activation_=-1;}return std::nullopt;}
 if(!current_){if(!activating){activation_=-1;return std::nullopt;}if(activation_<0)activation_=f.epoch_ms;if(f.epoch_ms-activation_<500)return std::nullopt;current_=Episode{uuid_(),f.epoch_ms,f.epoch_ms,{},""};retained_=-1;}
 current_->ended=f.epoch_ms;
 if((retained_<0 || f.epoch_ms-retained_>=100) && current_->samples.size()<120){current_->samples.push_back(f);retained_=f.epoch_ms;}
 const bool maximum=f.epoch_ms-current_->started>=12000;
 if(maximum || (clear_>=0 && f.epoch_ms-clear_>=1000)){
   auto result=current_;result->terminal=maximum?"TRUNCATED_MAX_DURATION":"COMPLETE";current_.reset();suppressed_=maximum;activation_=-1;return result;
 }return std::nullopt;
}
Json assessment_message(const Metadata& m,const ModelState&,const Assessment& a,const Episode& e,const std::string& model) {
 if(!is_sha256(model)||!is_uuid(e.id))throw std::invalid_argument("MODEL_IDENTITY_INVALID");
 auto msg=base(m,"TIRE_HEALTH_ASSESSMENT");msg["assessmentId"]=text(uuid_v5("17847494-307d-5fb4-a96b-d9425a0e5093",{m.unit_system_uid,e.id,model}));msg["sourceExerciseId"]=text(e.id);msg["sourceEventTime"]=text(utc_timestamp(e.ended));msg["modelConfigSha256"]=text(model);
 Json::Object features{{"longitudinalSlipBps",number(a.features.longitudinal)},{"lateralSlipBps",number(a.features.lateral)},{"wheelDispersionBps",number(a.features.dispersion)},{"slipPersistenceBps",number(a.features.persistence)}};
 return wrap(msg,Json{Json::Object{{"modelId",text("tire-condition-demo-v1")},{"provenance",text("DEMO_SYNTHETIC")},{"validActiveSamples",number(a.samples)},{"loadBps",number(a.load)},{"conditionScore",number(a.score)},{"confidencePercent",number(a.confidence)},{"previousBand",text(band_name(a.previous))},{"currentBand",text(band_name(a.current))},{"features",Json{features}}}});
}
Json event_message(const Json& a) {
 const auto& c=a.at("content");const auto current=c.at("currentBand").string();
 Json::Object message{{"schemaVersion",a.at("schemaVersion")},{"contractVersion",a.at("contractVersion")},{"messageType",text("TIRE_CONDITION_BAND_CHANGED")},{"assessmentId",a.at("assessmentId")},{"unitSystemUid",a.at("unitSystemUid")},{"sourceEventTime",a.at("sourceEventTime")},{"eventId",text(uuid_v5("330cfd3e-d785-51ba-8074-2cb57498b11e",{a.at("assessmentId").string(),"TIRE_CONDITION_BAND_CHANGED",current}))}};
 if(a.at("schemaVersion").integer()==2) {
  for(const auto* field:{"unitRole","serviceVersion","serviceInstance"})message[field]=a.at(field);
 } else if(a.at("schemaVersion").integer()!=1)throw std::invalid_argument("MESSAGE_SCHEMA_INVALID");
 return wrap(message, Json{Json::Object{{"eventType",text("TIRE_CONDITION_BAND_CHANGED")},{"previousBand",c.at("previousBand")},{"currentBand",c.at("currentBand")},{"conditionScore",c.at("conditionScore")},{"confidencePercent",c.at("confidencePercent")}}});
}
Json advisory_request(const Metadata& m,const std::string& epoch,std::int64_t sequence,const std::string& decision,Band band,std::int64_t now) {
 if(!is_uuid(epoch)||!is_uuid(decision)||sequence<1||band==Band::NotEvaluated)throw std::invalid_argument("ADVISORY_INVALID");
 Json::Object r{{"schemaVersion",number(1)},{"requestId",text(uuid_v5(epoch,{std::to_string(sequence)}))},{"producerEpoch",text(epoch)},{"sequence",number(sequence)},{"decisionId",text(decision)},{"serviceVersion",text(m.service_version)},{"modelVersion",text("1.0.0")},{"issuedAt",text(utc_timestamp(now))},{"expiresAt",text(utc_timestamp(now+30000))},{"operation",text(band==Band::Good?"CLEAR":"SET")},{"reasonCode",text(band==Band::Good?"CONDITION_CLEARED":"PREDICTED_TIRE_WEAR")}};
 if(band!=Band::Good)r["recommendation"]=text(band==Band::Inspection?"TIRE_INSPECTION_RECOMMENDED":"TIRE_REPLACEMENT_RECOMMENDED");
 return Json{r};
}
Json advisory_fact(const Metadata& m,const Json& request,const Json& status,std::int64_t now) {
 if(m.service_version!=request.at("serviceVersion").string())throw std::invalid_argument("ADVISORY_PROVENANCE_INVALID");
 for(const auto* field:{"requestId","producerEpoch","sequence"})if(canonical(request.at(field))!=canonical(status.at(field)))throw std::invalid_argument("UNCORRELATED_STATUS");
 if(status.object().size()!=10 || status.at("schemaVersion").integer()!=1)throw std::invalid_argument("STATUS_INVALID");
 const auto state=status.at("state").string(); const std::vector<std::string> states{"RECEIVED","APPLIED","CLEARED","REJECTED","EXPIRED","FAILED"};
 if(std::find(states.begin(),states.end(),state)==states.end())throw std::invalid_argument("STATUS_INVALID");
 const auto recommendation=status.at("activeRecommendation").string(),reason=status.at("activeReasonCode").string();
 const std::vector<std::string> reasons{"NONE","UNAUTHORIZED_SOURCE","UNAUTHORIZED_PATH","INVALID_SCHEMA","INVALID_VALUE","STALE_REQUEST","REPLAY_DETECTED","SEQUENCE_ROLLBACK","RATE_LIMITED","QM_POLICY_DENIED","INTERNAL_ERROR"};
 if(std::find(reasons.begin(),reasons.end(),status.at("reason").string())==reasons.end() || !date_time(status.at("gatewayObservedAt").string()) ||
   (!std::holds_alternative<std::nullptr_t>(status.at("activeUntil").value) && !date_time(status.at("activeUntil").string())) ||
   (state=="APPLIED" && request.at("operation").string()!="SET") || (state=="CLEARED" && request.at("operation").string()!="CLEAR"))throw std::invalid_argument("STATUS_INVALID");
 if((recommendation!="NONE" && recommendation!="TIRE_INSPECTION_RECOMMENDED" && recommendation!="TIRE_REPLACEMENT_RECOMMENDED") || (reason!="NONE" && reason!="PREDICTED_TIRE_WEAR"))throw std::invalid_argument("STATUS_WRONG_PRODUCT");
 auto msg=base(m,"TIRE_ADVISORY_FACT");for(const auto* f:{"requestId","producerEpoch","sequence"})msg[f]=request.at(f);msg["gatewayState"]=text(state);msg["recordedAt"]=text(utc_timestamp(now));
 Json::Object content{{"assessmentId",request.at("decisionId")},{"operation",request.at("operation")},{"reasonCode",request.at("reasonCode")},{"issuedAt",request.at("issuedAt")},{"expiresAt",request.at("expiresAt")},{"gatewayReason",status.at("reason")},{"gatewayObservedAt",status.at("gatewayObservedAt")},{"activeRecommendation",status.at("activeRecommendation")},{"activeReasonCode",status.at("activeReasonCode")},{"activeUntil",status.at("activeUntil")}};
 if(request.object().count("recommendation"))content["recommendation"]=request.at("recommendation");
 return wrap(msg,Json{content});
}
} // namespace tire_health
