// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#include "tire_health/service.hpp"
#include "tire_health/runtime/application.hpp"
#include "tire_health/runtime/sha256.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <limits>
#include <unistd.h>
using namespace tire_health;
using namespace tire_health::runtime;
namespace {
template<typename F>void rejects(F operation){bool failed=false;try{operation();}catch(...){failed=true;}assert(failed);}
bool native_fixture=false;
Metadata metadata(){
 if(native_fixture) {
  const auto inputs=parse_service_inputs(R"({"schemaVersion":1,"serviceVersion":"21.0.0"})",
   {{"AOS_ITEM_ID","tire-service"},{"AOS_SUBJECT_ID","group-subject"},{"AOS_INSTANCE_INDEX","0"},{"AOS_INSTANCE_ID","tire-instance"}});
  return parse_metadata(std::string(R"({"schemaVersion":2,"unitSystemUid":"test-fixture-unit","unitRole":"validation","vdpContractVersion":"1.0.1","vdpContractSha256":")")+std::string(64,'b')+"\"}",inputs);
 }
 return {"test-fixture-unit","VALIDATION","21.0.0",std::string(64,'a'),"1.0.1",std::string(64,'b')};
}
struct Temp {std::filesystem::path root;Temp(){char path[]="/tmp/tire-native-XXXXXX";const auto* created=::mkdtemp(path);assert(created);root=created;}~Temp(){std::filesystem::remove_all(root);}};
Episode episode(std::int64_t end=1788000000000LL){Episode e{random_uuid(),end-3000,end,{},"COMPLETE"};e.samples.resize(30);return e;}
Json message(){ModelState state;const auto e=episode();const auto result=assess(state,{7000,6000,5000,5500},30,e.id);assert(result);return assessment_message(metadata(),state,*result,e,std::string(64,'c'));}
HttpResponse ack(const std::string& bytes,bool wrong=false){const auto msg=parse_json(bytes);return {201,canonical(Json{Json::Object{{"schemaVersion",Json{std::int64_t{1}}},{"contractVersion",Json{std::string("1.0.0")}},{"receiptId",Json{random_uuid()}},{"messageKeySha256",Json{wrong?std::string(64,'0'):message_key(msg)}},{"contentSha256",msg.at("contentSha256")},{"state",Json{std::string("DURABLE_ACCEPTED")}},{"receivedAt",Json{std::string("2026-09-10T10:00:00.000Z")}}}}),0};}
void protocol_tests(){
 assert(sha256_hex("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
 assert(uuid_v5("6ba7b810-9dad-11d1-80b4-00c04fd430c8",{"www.widgets.com"})=="21f7f8de-8051-5b89-8680-0195ef798b6a");
 rejects([]{parse_json("{\"x\":1,\"x\":2}");});rejects([]{parse_json("{\"x\":NaN}");});
 const auto m=metadata();const auto text="{\"schemaVersion\":1,\"unitSystemUid\":\"test-fixture-unit\",\"unitRole\":\"validation\",\"serviceVersion\":\"21.0.0\",\"serviceArtifactSha256\":\""+m.service_artifact_sha256+"\",\"vdpContractVersion\":\"1.0.1\",\"vdpContractSha256\":\""+m.vdp_contract_sha256+"\"}";
 assert(parse_legacy_metadata(text).unit_role=="VALIDATION");rejects([&]{parse_legacy_metadata(text.substr(0,text.size()-1)+",\"unknown\":1}");});
 for(const auto* invalid:{"021.0.0","21.0.0-beta","21.0.0+build","999999999999999999999999999999.0.0"}) {auto changed=text;changed.replace(changed.find("21.0.0"),6,invalid);rejects([&]{parse_legacy_metadata(changed);});}
 assert(retry_delay(0,0)==1&&retry_delay(10,0)==30&&retry_delay(0,0,60)==60);
 assert(parse_credential("{\"protocol\":\"aos-kuksa-auth-compat/v1\",\"status\":\"rejected\",\"correlationId\":\"fixture\",\"code\":\"DENIED\",\"retryable\":false}\n",1000).retryable==false);
 Lease lease;lease.issued(Credential{"",1300,1180,"",false},1000,10000);assert(!lease.expired(999,309999));assert(lease.expired(999,310000));
 const auto bytes=canonical(message());assert(matches_ack(bytes,ack(bytes)));assert(!matches_ack(bytes,ack(bytes,true)));
}
void model_tests(){
 ModelState state;const auto first=assess(state,{10000,10000,10000,10000},20,random_uuid());assert(first&&first->score==0&&first->confidence==50&&state.band==Band::Replacement);
 assert(assess(state,{0,0,0,0},40,random_uuid()));assert(state.band==Band::Replacement&&state.better_count==1);
 assert(!assess(state,{0,0,0,0},19,random_uuid()));assert(state.better_count==1);
 assert(assess(state,{0,0,0,0},40,random_uuid()));assert(state.band==Band::Replacement);
 const auto id=random_uuid();assert(assess(state,{0,0,0,0},40,id));assert(state.band==Band::Good);
 const auto recent=state.recent;assert(!assess(state,{10000,10000,10000,10000},40,id));assert(state.recent==recent&&state.band==Band::Good);
 auto current=assess(state,{7000,6000,5000,5500},30,random_uuid());assert(current&&current->load==6000&&current->score==40&&current->current==Band::Inspection);
 assert(!assess(state,{-1,0,0,0},30,random_uuid()));
}
void input_episode_tests(){
 {
  Episode raw{random_uuid(),1000,4900,{},"COMPLETE"};
  for(int i=0;i<40;++i) {
   Frame frame;frame.epoch_ms=1000+i*100;frame.values[0]=50;
   for(int wheel=3;wheel<7;++wheel)frame.values[wheel]=50;
   frame.values[3]=48;
   if(i<5)frame.values[7]=-0.08;
   if(i>=5&&i<10)frame.values[14]=-4.0;
   raw.samples.push_back(frame);
  }
  auto f=extract_features(raw);assert(f);
  assert(f->longitudinal==4000&&f->lateral==5000&&f->dispersion==2667&&f->persistence==2500);
  // Multiple wheels/thresholds in one sample count once; equality is included.
  raw.samples[0].values[8]=0.08;raw.samples[0].values[12]=4.0;
  assert(extract_features(raw)->persistence==2500);
  for(auto& frame:raw.samples) {
   for(int wheel=3;wheel<7;++wheel)frame.values[wheel]=0;
  }
  assert(extract_features(raw)->dispersion==0);
  raw.samples[0].values[3]=1;assert(extract_features(raw)->dispersion==10000);
  auto invalid=raw;invalid.samples[0].values[7]=std::numeric_limits<double>::quiet_NaN();assert(!extract_features(invalid));
  invalid=raw;invalid.samples[0].values[3]=-1;assert(!extract_features(invalid));
  invalid=raw;invalid.samples[1].epoch_ms=invalid.samples[0].epoch_ms;assert(!extract_features(invalid));
  invalid=raw;invalid.terminal="INCOMPLETE_SOURCE_GAP";assert(!extract_features(invalid));
  invalid=raw;invalid.samples.resize(19);assert(!extract_features(invalid));
  Temp t;Runtime product(t.root/"state",t.root/"outbox",metadata());
  assert(product.apply_episode(*f,raw,kModelConfigSha256));
  bool assessment_seen=false;
  while(const auto message=product.next_message()) {
   const auto record=parse_json(message->bytes);
   if(record.at("messageType").string()=="TIRE_HEALTH_ASSESSMENT") {
    assert(record.at("modelConfigSha256").string()==kModelConfigSha256);assessment_seen=true;
   }
   assert(product.accept(*message,ack(message->bytes)));
  }
  assert(assessment_seen);
 }
 std::array<Signal,15> signals;for(auto& value:signals)value={0,1000,true};assert(complete_frame(signals,1250));assert(!complete_frame(signals,1251));signals[5].epoch_ms=999;assert(!complete_frame(signals,1000));
 EpisodeEngine engine;Frame frame;frame.values[0]=20;frame.values[2]=5;std::optional<Episode> completed;
 for(int i=0;i<=130;i++){frame.epoch_ms=1000+i*100;auto result=engine.ingest(frame);if(result)completed=result;}
 assert(completed&&completed->terminal=="TRUNCATED_MAX_DURATION"&&completed->samples.size()==120);assert(!engine.active());
 for(int i=131;i<150;i++){frame.epoch_ms=1000+i*100;assert(!engine.ingest(frame));}assert(!engine.active());
 frame.values[0]=0;for(int i=150;i<162;i++){frame.epoch_ms=1000+i*100;engine.ingest(frame);}frame.values[0]=20;
 for(int i=162;i<169;i++){frame.epoch_ms=1000+i*100;engine.ingest(frame);}assert(engine.active());
 frame.epoch_ms=1;auto gap=engine.ingest(frame);assert(gap&&gap->terminal=="INCOMPLETE_SOURCE_GAP"&&!engine.active());
}
void store_tests(){
 Temp t;std::string epoch;{
 StateStore store(t.root/"state",t.root/"outbox","test-fixture-unit");epoch=store.state().at("producerEpoch").string();
 const auto msg=message();assert(store.commit(store.state(),{msg}));assert(store.queued()==1);assert(store.commit(store.state(),{msg}));assert(store.queued()==1);
 auto pending=store.pending();assert(pending);assert(!store.acknowledge(*pending,ack(pending->bytes,true)));assert(store.queued()==1);
 }{
 StateStore store(t.root/"state",t.root/"outbox","test-fixture-unit");assert(store.state().at("producerEpoch").string()==epoch);const auto p=store.pending();assert(p);assert(store.acknowledge(*p,ack(p->bytes)));assert(store.queued()==0);
 // A known complete journal replays once and preserves exact message bytes.
 const auto msg=message();Json txn{Json::Object{{"schemaVersion",Json{std::int64_t{1}}},{"state",store.state()},{"messages",Json{Json::Array{msg}}}}};durable_file(t.root/"state"/"transaction.json",canonical(txn));
 }{
 StateStore store(t.root/"state",t.root/"outbox","test-fixture-unit");assert(store.queued()==1);assert(!std::filesystem::exists(t.root/"state"/"transaction.json"));auto p=store.pending();assert(p);assert(!store.acknowledge(*p,{409,"",0}));assert(!store.pending());assert(store.queued()==1);
 }rejects([&]{StateStore wrong(t.root/"state",t.root/"outbox","other-unit");});
}
void runtime_tests(){
 Temp t;Runtime runtime(t.root/"state",t.root/"outbox",metadata());assert(runtime.state_ready());const auto e=episode();assert(runtime.apply_episode({10000,10000,10000,10000},e,std::string(64,'c')));
 const auto request=runtime.next_advisory(e.ended);assert(request);const auto parsed=parse_json(*request);assert(parsed.at("recommendation").string()=="TIRE_REPLACEMENT_RECOMMENDED");
 assert(!runtime.apply_episode({0,0,0,0},e,std::string(64,'c')));
 auto changed=metadata();changed.unit_system_uid="foreign";rejects([&]{runtime.update_vdp_metadata(changed);});changed=metadata();changed.vdp_contract_sha256=std::string(64,'d');runtime.update_vdp_metadata(changed);
 const auto queued=runtime.next_message();assert(queued);const auto msg=parse_json(queued->bytes);if(msg.object().count("vdpContractSha256"))assert(msg.at("vdpContractSha256").string()==std::string(64,'b'));
 runtime.stop();
}
void advisory_tests(){
 Temp t;std::string epoch;std::int64_t sequence;
 {
  Runtime runtime(t.root/"state",t.root/"outbox",metadata());auto e=episode();
  assert(runtime.apply_episode({0,0,0,0},e,std::string(64,'c')));assert(!runtime.next_advisory(e.ended));
  e.id=random_uuid();e.ended+=5000;assert(runtime.apply_episode({10000,10000,10000,10000},e,std::string(64,'c')));
  const auto request=runtime.next_advisory(e.ended);assert(request);const auto r=parse_json(*request);epoch=r.at("producerEpoch").string();sequence=r.at("sequence").integer();
  assert(!runtime.next_advisory(e.ended+500));assert(runtime.next_advisory(e.ended+1000)==request);
  Json::Object status{{"schemaVersion",Json{std::int64_t{1}}},{"requestId",r.at("requestId")},{"producerEpoch",r.at("producerEpoch")},{"sequence",r.at("sequence")},{"state",Json{std::string("APPLIED")}},{"reason",Json{std::string("NONE")}},{"gatewayObservedAt",Json{utc_timestamp(e.ended+1000)}},{"activeRecommendation",r.at("recommendation")},{"activeReasonCode",Json{std::string("PREDICTED_TIRE_WEAR")}},{"activeUntil",r.at("expiresAt")}};
  auto wrong=status;wrong["producerEpoch"]=Json{random_uuid()};rejects([&]{runtime.gateway_status(canonical(Json{wrong}),e.ended+1000);});
  runtime.gateway_status(canonical(Json{status}),e.ended+1000);runtime.gateway_status(canonical(Json{status}),e.ended+2000);
  assert(!runtime.next_advisory(e.ended+3000));
  for(int i=0;i<3;++i){e.id=random_uuid();e.ended+=5000;runtime.apply_episode({0,0,0,0},e,std::string(64,'c'));}
  const auto clear=runtime.next_advisory(e.ended);assert(clear);assert(parse_json(*clear).at("operation").string()=="CLEAR");
 }
 StateStore recovered(t.root/"state",t.root/"outbox","test-fixture-unit");assert(recovered.state().at("producerEpoch").string()==epoch);assert(recovered.state().at("nextAdvisorySequence").integer()>sequence);
}
void corruption_capacity_tests(){
 Temp t;StateStore store(t.root/"state",t.root/"outbox","test-fixture-unit");
 const auto base=message();
 for(int i=0;i<256;i++){auto msg=base.object();msg["assessmentId"]=Json{uuid_v5("17847494-307d-5fb4-a96b-d9425a0e5093",{std::to_string(i)})};assert(store.commit(store.state(),{Json{msg}}));}
 assert(store.queued()==256);assert(!store.commit(store.state(),{message()}));assert(store.queued()==256);
 auto broken=store.state().object();broken["producerEpoch"]=Json{random_uuid()};durable_file(t.root/"state"/"state.json",canonical(Json{broken}));
 Runtime rejected(t.root/"state",t.root/"outbox",metadata());assert(!rejected.state_ready());assert(!rejected.next_message());
}

void bound_request_recovery_tests() {
 for(const bool previous_native:{false,true}) {
  Temp t;native_fixture=previous_native;
  const auto old=metadata();Json request;std::string retained;
  const auto e=episode();
  {
   Runtime runtime(t.root/"state",t.root/"outbox",old);
   assert(runtime.apply_episode({10000,10000,10000,10000},e,std::string(64,'c')));
   request=parse_json(*runtime.next_advisory(e.ended));
   retained=runtime.next_message()->bytes;
  }
  native_fixture=true;auto current=metadata();current.service_version="22.0.0";current.service_instance->instance_id="new-instance";
  Runtime runtime(t.root/"state",t.root/"outbox",current);
  assert(runtime.state_ready() && runtime.next_message()->bytes==retained);
  const Json status{Json::Object{{"schemaVersion",Json{std::int64_t{1}}},{"requestId",request.at("requestId")},{"producerEpoch",request.at("producerEpoch")},{"sequence",request.at("sequence")},{"state",Json{std::string("APPLIED")}},{"reason",Json{std::string("NONE")}},{"gatewayObservedAt",Json{utc_timestamp(e.ended+1000)}},{"activeRecommendation",request.at("recommendation")},{"activeReasonCode",Json{std::string("PREDICTED_TIRE_WEAR")}},{"activeUntil",request.at("expiresAt")}}};
  runtime.gateway_status(canonical(status),e.ended+1000);
  unsigned count=0;bool fact_seen=false;
  while(const auto pending=runtime.next_message()) {
   const auto msg=parse_json(pending->bytes);
   assert(msg.at("schemaVersion").integer()==(previous_native?2:1));
   if(msg.object().count("serviceVersion"))assert(msg.at("serviceVersion").string()==old.service_version);
   if(previous_native)assert(parse_service_instance(msg.at("serviceInstance"))==*old.service_instance);
   if(msg.at("messageType").string()=="TIRE_ADVISORY_FACT")fact_seen=true;
   assert(runtime.accept(*pending,ack(pending->bytes)));++count;
  }
  assert(fact_seen && count==3);
  const auto next=parse_json(*runtime.next_advisory(e.ended+20000));
  assert(next.at("serviceVersion").string()=="22.0.0");
  assert(next.at("producerEpoch").string()==request.at("producerEpoch").string());
  assert(next.at("sequence").integer()>request.at("sequence").integer());
 }
 // Historic wrapper state has no provenance binding: do not fabricate a fact.
 Temp t;native_fixture=false;const auto e=episode();Json request;
 {
  Runtime runtime(t.root/"state",t.root/"outbox",metadata());
  assert(runtime.apply_episode({10000,10000,10000,10000},e,std::string(64,'c')));
  request=parse_json(*runtime.next_advisory(e.ended));
 }
 {
  StateStore store(t.root/"state",t.root/"outbox","test-fixture-unit");
  auto state=store.state().object();state.erase("lastRequestMetadata");assert(store.commit(Json{state},{}));
 }
 native_fixture=true;
 Runtime runtime(t.root/"state",t.root/"outbox",metadata());assert(runtime.state_ready());
 const auto first=runtime.next_message()->bytes;
 runtime.gateway_status("{}",e.ended+1); // Unbound legacy request has no reportable fact.
 assert(runtime.next_message()->bytes==first);
 const auto renewed=parse_json(*runtime.next_advisory(e.ended+20000));
 assert(renewed.at("producerEpoch").string()==request.at("producerEpoch").string());
 assert(renewed.at("sequence").integer()>request.at("sequence").integer());
 native_fixture=false;
}

void demo_reset_tests(){
 native_fixture=true;Temp t;const auto m=metadata();const auto e=episode();const auto now=e.ended+100;
 std::string command_bytes,ack_bytes,epoch;Json clear;
 {
  Runtime runtime(t.root/"state",t.root/"outbox",m);
  assert(runtime.apply_episode({10000,10000,10000,10000},e,kModelConfigSha256));
  const auto warning=parse_json(*runtime.next_advisory(e.ended));epoch=warning.at("producerEpoch").string();
  auto command=parse_json(*runtime.demo_control_poll()).object();
  command["commandId"]=Json{random_uuid()};command["operation"]=Json{std::string("RESET_DEMO_SCENARIO")};
  command["issuedAt"]=Json{utc_timestamp(now)};command["expiresAt"]=Json{utc_timestamp(now+60000)};
  const auto envelope=[&](const Json::Object& c){return canonical(Json{Json::Object{{"schemaVersion",Json{std::int64_t{1}}},{"command",Json{c}}}});};
  command_bytes=envelope(command);
  auto foreign=command;foreign["unitSystemUid"]=Json{std::string("production")};
  rejects([&]{runtime.demo_control_command(envelope(foreign),now);});
  auto expired=command;expired["expiresAt"]=Json{utc_timestamp(now)};
  rejects([&]{runtime.demo_control_command(envelope(expired),now);});
  runtime.demo_control_command(command_bytes,now);
  clear=parse_json(*runtime.next_advisory(now));
  assert(clear.at("operation").string()=="CLEAR"&&clear.at("producerEpoch").string()==epoch);
  assert(clear.at("sequence").integer()>warning.at("sequence").integer());
  StateStore state(t.root/"state",t.root/"outbox",m.unit_system_uid);
  const auto snapshot=canonical(state.state());const auto queued=state.queued();
  assert(queued==2);assert(std::holds_alternative<std::nullptr_t>(state.state().at("model").value));
  runtime.demo_control_command(command_bytes,now+10);
  assert(!runtime.demo_control_ack(now+10));assert(!runtime.next_advisory(now+10));
  StateStore same(t.root/"state",t.root/"outbox",m.unit_system_uid);
  assert(canonical(same.state())==snapshot&&same.queued()==queued);
  assert(!runtime.apply_episode({10000,10000,10000,10000},episode(now+500),kModelConfigSha256));
 }
 {
  Runtime runtime(t.root/"state",t.root/"outbox",m);assert(runtime.state_ready());
  const auto recovered=parse_json(*runtime.next_advisory(now+1500));
  assert(recovered.at("sequence").integer()>clear.at("sequence").integer());clear=recovered;
  Json::Object status{{"schemaVersion",Json{std::int64_t{1}}},{"requestId",clear.at("requestId")},
   {"producerEpoch",clear.at("producerEpoch")},{"sequence",clear.at("sequence")},{"state",Json{std::string("CLEARED")}},
   {"reason",Json{std::string("NONE")}},{"gatewayObservedAt",Json{utc_timestamp(now+1600)}},
   {"activeRecommendation",Json{std::string("NONE")}},{"activeReasonCode",Json{std::string("NONE")}},{"activeUntil",Json{nullptr}}};
  auto wrong=status;wrong["requestId"]=Json{random_uuid()};
  rejects([&]{runtime.gateway_status(canonical(Json{wrong}),now+1600);});
  assert(!runtime.demo_control_ack(now+1600));
  runtime.gateway_status(canonical(Json{status}),now+1600);
  ack_bytes=*runtime.demo_control_ack(now+1700);
  assert(parse_json(ack_bytes).at("result").string()=="CLEARED");
  assert(!runtime.next_advisory(now+2000));
  runtime.demo_control_command(command_bytes,now+1800);
  assert(runtime.demo_control_ack(now+1800)==ack_bytes);
  assert(!runtime.apply_episode({10000,10000,10000,10000},e,kModelConfigSha256));
  assert(runtime.apply_episode({10000,10000,10000,10000},episode(now+3000),kModelConfigSha256));
  const auto renewed=parse_json(*runtime.next_advisory(now+3000));
  assert(renewed.at("operation").string()=="SET"&&renewed.at("sequence").integer()>clear.at("sequence").integer());
  assert(!parse_json(runtime.advisory_readiness(now)).at("ready").boolean());
  runtime.function_status("READY",now+3000);
  assert(parse_json(runtime.advisory_readiness(now+5000)).at("ready").boolean());
  assert(!parse_json(runtime.advisory_readiness(now+9000)).at("ready").boolean());
  runtime.disconnect();assert(!parse_json(runtime.advisory_readiness(now+5000)).at("ready").boolean());
 }
 {
  Runtime runtime(t.root/"state",t.root/"outbox",m);assert(runtime.demo_control_ack(now+4000)==ack_bytes);
  const auto command=parse_json(command_bytes).at("command");
  runtime.demo_control_accepted(canonical(Json{Json::Object{{"schemaVersion",Json{std::int64_t{1}}},{"commandId",command.at("commandId")},{"state",Json{std::string("CLEARED")}}}}));
  assert(!runtime.demo_control_ack(now+4000));
  auto next=command.object();next["commandId"]=Json{random_uuid()};
  next["issuedAt"]=Json{utc_timestamp(now+5000)};next["expiresAt"]=Json{utc_timestamp(now+65000)};
  runtime.demo_control_command(canonical(Json{Json::Object{{"schemaVersion",Json{std::int64_t{1}}},{"command",Json{next}}}}),now+5000);
  assert(!runtime.next_advisory(now+65000));
  assert(parse_json(*runtime.demo_control_ack(now+65000)).at("result").string()=="FAILED");
 }
 native_fixture=false;
}
void emit_conformance(){
 Temp t;Runtime runtime(t.root/"state",t.root/"outbox",metadata());const auto e=episode();
 assert(runtime.apply_episode({10000,10000,10000,10000},e,std::string(64,'c')));
 runtime.function_status("READY",e.ended);
 const auto request=runtime.next_advisory(e.ended);assert(request);const auto r=parse_json(*request);
 const Json status{Json::Object{{"schemaVersion",Json{std::int64_t{1}}},{"requestId",r.at("requestId")},{"producerEpoch",r.at("producerEpoch")},{"sequence",r.at("sequence")},{"state",Json{std::string("APPLIED")}},{"reason",Json{std::string("NONE")}},{"gatewayObservedAt",Json{utc_timestamp(e.ended+1000)}},{"activeRecommendation",r.at("recommendation")},{"activeReasonCode",Json{std::string("PREDICTED_TIRE_WEAR")}},{"activeUntil",r.at("expiresAt")}}};
 runtime.gateway_status(canonical(status),e.ended+1000);
 unsigned count=0;while(const auto pending=runtime.next_message()){if(native_fixture){const auto msg=parse_json(pending->bytes);assert(msg.at("schemaVersion").integer()==2 && !msg.object().count("serviceArtifactSha256") && !msg.object().count("modelArtifactSha256"));assert(parse_service_instance(msg.at("serviceInstance"))==*metadata().service_instance);}std::cout<<pending->bytes<<'\n';assert(runtime.accept(*pending,ack(pending->bytes)));++count;}assert(count==4);
}
}
int main(int argc,char**argv){if(argc==2&&std::string(argv[1])=="--emit-native-conformance"){native_fixture=true;emit_conformance();return 0;}if(argc==2&&std::string(argv[1])=="--emit-conformance"){emit_conformance();return 0;}protocol_tests();model_tests();input_episode_tests();store_tests();runtime_tests();advisory_tests();corruption_capacity_tests();native_fixture=true;store_tests();runtime_tests();advisory_tests();native_fixture=false;bound_request_recovery_tests();demo_reset_tests();std::cout<<"PASS Tire protocol, normalized model, episode, persistent outbox, advisory and runtime contracts\n";}
