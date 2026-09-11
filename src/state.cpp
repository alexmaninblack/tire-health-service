// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#include "tire_health/state.hpp"
#include "tire_health/runtime/application.hpp"
#include "tire_health/runtime/sha256.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace tire_health::runtime {
void durable_file(const std::filesystem::path& path,const std::string& bytes) {
 struct stat parent{};
 if(::lstat(path.parent_path().c_str(),&parent)!=0 || !S_ISDIR(parent.st_mode) || parent.st_uid!=::geteuid() || (parent.st_mode&0777)!=0700)throw std::runtime_error("STATE_DIRECTORY_INVALID");
 const auto temporary=path.string()+".next";
 const int fd=::open(temporary.c_str(),O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd<0)throw std::runtime_error("STATE_WRITE_FAILED");
 try {
  std::size_t at=0;while(at<bytes.size()){const auto n=::write(fd,bytes.data()+at,bytes.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("STATE_WRITE_FAILED");at+=static_cast<std::size_t>(n);}
  if(::fsync(fd)!=0 || ::rename(temporary.c_str(),path.c_str())!=0)throw std::runtime_error("STATE_WRITE_FAILED");
  const int directory=::open(path.parent_path().c_str(),O_RDONLY|O_CLOEXEC);if(directory<0)throw std::runtime_error("STATE_WRITE_FAILED");
  const int synced=::fsync(directory);::close(directory);if(synced!=0)throw std::runtime_error("STATE_WRITE_FAILED");::close(fd);
 } catch(...){::close(fd);::unlink(temporary.c_str());throw;}
}
}
namespace tire_health {
using namespace runtime;
namespace {
void directory(const std::filesystem::path& path) {
 std::filesystem::create_directories(path);
 struct stat st{};if(::lstat(path.c_str(),&st)!=0||!S_ISDIR(st.st_mode)||st.st_uid!=::geteuid())throw std::runtime_error("STATE_DIRECTORY_INVALID");
 if(::chmod(path.c_str(),0700)!=0)throw std::runtime_error("STATE_DIRECTORY_INVALID");
}
void private_file(const std::filesystem::path& path) {
 struct stat st{};if(::lstat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode)||st.st_uid!=::geteuid()||(st.st_mode&0777)!=0600)throw std::runtime_error("STATE_FILE_INVALID");
}
void remove_durable(const std::filesystem::path& path) {
 if(::unlink(path.c_str())!=0 && errno!=ENOENT)throw std::runtime_error("STATE_REMOVE_FAILED");
 const int fd=::open(path.parent_path().c_str(),O_RDONLY|O_CLOEXEC);if(fd<0)throw std::runtime_error("STATE_REMOVE_FAILED");const auto result=::fsync(fd);::close(fd);if(result!=0)throw std::runtime_error("STATE_REMOVE_FAILED");
}
Json initial(const std::string& uid) {
 return Json{Json::Object{{"schemaVersion",Json{std::int64_t{1}}},{"unitSystemUid",Json{uid}},{"producerEpoch",Json{random_uuid()}},{"nextAdvisorySequence",Json{std::int64_t{1}}},{"model",Json{nullptr}},{"lastPublishedAt",Json{std::int64_t{0}}},{"lastRequest",Json{nullptr}},{"gatewayStates",Json{Json::Array{}}}}};
}
}
StateStore::StateStore(std::filesystem::path state,std::filesystem::path outbox,std::string uid):root_(std::move(state)),outbox_(std::move(outbox)),uid_(std::move(uid)) {
 directory(root_);directory(outbox_);
 for(const auto& item:std::filesystem::directory_iterator(root_)) {
  const auto name=item.path().filename().string();
  if(name!="state.json"&&name!="state.sha256"&&name!="transaction.json"&&name!="commit.sha256")throw std::runtime_error("NOT_READY_STATE");private_file(item.path());
 }
 if(std::filesystem::exists(root_/"transaction.json")) {
  const auto txn=parse_json(read_file(root_/"transaction.json",196608),196608);replay(txn);remove_durable(root_/"transaction.json");
 }
 if(std::filesystem::exists(root_/"state.json"))state_=parse_json(read_file(root_/"state.json",131072),131072);
 else {if(!std::filesystem::is_empty(outbox_))throw std::runtime_error("NOT_READY_STATE");state_=initial(uid_);durable_file(root_/"state.json",canonical(state_));durable_file(root_/"state.sha256",sha256_hex(canonical(state_)));}
 if(read_file(root_/"state.sha256",64)!=sha256_hex(canonical(state_)))throw std::runtime_error("NOT_READY_STATE");validate_state(state_);validate_queue();
}
void StateStore::validate_state(const Json& value) const {
 if(value.object().count("lastRequestMetadata")) {
  const auto metadata=parse_metadata_binding(value.at("lastRequestMetadata"));
  const auto& request=value.at("lastRequest");
  if(std::holds_alternative<std::nullptr_t>(request.value) || metadata.unit_system_uid!=uid_ ||
     metadata.service_version!=request.at("serviceVersion").string())throw std::runtime_error("NOT_READY_STATE");
 }
 const auto& states=std::get<Json::Array>(value.at("gatewayStates").value);std::set<std::string> statuses;
 for(const auto& item:states){const auto v=item.string();if(v!="RECEIVED"&&v!="APPLIED"&&v!="CLEARED"&&v!="REJECTED"&&v!="EXPIRED"&&v!="FAILED")throw std::runtime_error("NOT_READY_STATE");if(!statuses.insert(v).second)throw std::runtime_error("NOT_READY_STATE");}
 if(value.object().size()!=(value.object().count("lastRequestMetadata")?9U:8U) || value.at("schemaVersion").integer()!=1 || value.at("unitSystemUid").string()!=uid_ || !is_uuid(value.at("producerEpoch").string()) || value.at("nextAdvisorySequence").integer()<1 || value.at("lastPublishedAt").integer()<0 || canonical(value).size()>131072)throw std::runtime_error("NOT_READY_STATE");
 if(!std::holds_alternative<std::nullptr_t>(value.at("lastRequest").value)) {
  const auto& request=value.at("lastRequest");
  if(request.at("producerEpoch").string()!=value.at("producerEpoch").string() || request.at("sequence").integer()>=value.at("nextAdvisorySequence").integer())throw std::runtime_error("NOT_READY_STATE");
 }
 if(!std::holds_alternative<std::nullptr_t>(value.at("model").value)) {
  const auto& model=value.at("model");
  if(model.object().size()!=13 || model.at("schemaVersion").integer()!=1 || model.at("modelId").string()!="tire-condition-demo-v1" || !is_sha256(model.at("modelConfigSha256").string()) || model.at("producerEpoch").string()!=value.at("producerEpoch").string())throw std::runtime_error("NOT_READY_STATE");
  (void)parse_band(model.at("conditionBand").string());
  for(const auto* field:{"conditionScore","confidencePercent"})if(model.at(field).integer()<0||model.at(field).integer()>100)throw std::runtime_error("NOT_READY_STATE");
  const auto better=model.at("betterBandCandidate").string();if(better!="NONE"&&better!="GOOD"&&better!="INSPECTION_RECOMMENDED")throw std::runtime_error("NOT_READY_STATE");
  if(!is_uuid(model.at("lastAppliedSourceExerciseId").string())||!is_uuid(model.at("lastAssessmentId").string())||model.at("nextAdvisorySequence").integer()!=value.at("nextAdvisorySequence").integer())throw std::runtime_error("NOT_READY_STATE");
  const auto count=model.at("betterBandEpisodeCount").integer();if(count<0||count>3)throw std::runtime_error("NOT_READY_STATE");
  const auto& recent=std::get<Json::Array>(model.at("recentSourceExerciseIds").value);std::set<std::string> unique;
  if(recent.size()>128)throw std::runtime_error("NOT_READY_STATE");for(const auto& id:recent)if(!is_uuid(id.string())||!unique.insert(id.string()).second)throw std::runtime_error("NOT_READY_STATE");
 }
}
void StateStore::validate_queue() const {
 std::size_t count=0,bytes=0;
 for(const auto& item:std::filesystem::directory_iterator(outbox_)) {
  private_file(item.path());auto name=item.path().filename().string();
  const bool blocked=name.size()==72 && name.substr(64)==".blocked";
  const bool message=name.size()==69 && name.substr(64)==".json";
  if((!blocked&&!message)||!is_sha256(name.substr(0,64)))throw std::runtime_error("NOT_READY_STATE");
  if(blocked){if(read_file(item.path(),64)!="DELIVERY_CONFLICT"||!std::filesystem::exists(outbox_/(name.substr(0,64)+".json")))throw std::runtime_error("NOT_READY_STATE");continue;}
  const auto content=read_file(item.path(),16384);const auto msg=parse_json(content,16384);
  if(msg.at("unitSystemUid").string()!=uid_||canonical(msg)!=content||message_key(msg)!=name.substr(0,64)||sha256_hex(canonical(msg.at("content")))!=msg.at("contentSha256").string())throw std::runtime_error("NOT_READY_STATE");
  ++count;bytes+=content.size();
 }if(count>256||bytes>2097152)throw std::runtime_error("NOT_READY_STATE");
}
void StateStore::replay(const Json& txn) {
 if(txn.object().size()!=3||txn.at("schemaVersion").integer()!=1)throw std::runtime_error("NOT_READY_STATE");
 const auto& next=txn.at("state");validate_state(next);
 const auto& messages=std::get<Json::Array>(txn.at("messages").value);if(messages.size()>4)throw std::runtime_error("NOT_READY_STATE");
 for(const auto& msg:messages)if(msg.at("unitSystemUid").string()!=uid_||sha256_hex(canonical(msg.at("content")))!=msg.at("contentSha256").string()||canonical(msg).size()>16384)throw std::runtime_error("NOT_READY_STATE");
 durable_file(root_/"state.json",canonical(next));durable_file(root_/"state.sha256",sha256_hex(canonical(next)));
 for(const auto& msg:messages){const auto path=outbox_/(message_key(msg)+".json");const auto bytes=canonical(msg);if(std::filesystem::exists(path)){if(read_file(path,16384)!=bytes)throw std::runtime_error("NOT_READY_STATE");}else durable_file(path,bytes);}
 durable_file(root_/"commit.sha256",sha256_hex(canonical(txn)));state_=next;
}
bool StateStore::commit(Json next,const std::vector<Json>& messages) {
 validate_state(next);validate_queue();
 Json::Array accepted;std::size_t count=queued(),bytes=0;bool full=false;
 for(const auto& item:std::filesystem::directory_iterator(outbox_))if(item.path().extension()==".json")bytes+=std::filesystem::file_size(item.path());
 for(const auto& msg:messages){const auto encoded=canonical(msg),key=message_key(msg);if(encoded.size()>16384)throw std::runtime_error("MESSAGE_TOO_LARGE");if(std::filesystem::exists(outbox_/(key+".json"))){if(read_file(outbox_/(key+".json"),16384)!=encoded)throw std::runtime_error("DELIVERY_CONFLICT");continue;}if(count>=256||bytes+encoded.size()>2097152){full=true;continue;}accepted.push_back(msg);++count;bytes+=encoded.size();}
 const Json txn{Json::Object{{"schemaVersion",Json{std::int64_t{1}}},{"state",next},{"messages",Json{accepted}}}};
 durable_file(root_/"transaction.json",canonical(txn));replay(txn);remove_durable(root_/"transaction.json");return !full;
}
std::size_t StateStore::queued() const {std::size_t result=0;for(const auto& f:std::filesystem::directory_iterator(outbox_))if(f.path().extension()==".json")++result;return result;}
std::optional<Pending> StateStore::pending() const {
 std::vector<std::filesystem::path> files;for(const auto& f:std::filesystem::directory_iterator(outbox_))if(f.path().extension()==".json")files.push_back(f.path());std::sort(files.begin(),files.end());
 for(const auto& file:files){const auto key=file.stem().string();if(!std::filesystem::exists(outbox_/(key+".blocked")))return Pending{key,read_file(file,16384)};}return std::nullopt;
}
bool StateStore::acknowledge(const Pending& pending,const HttpResponse& response) {
 const auto path=outbox_/(pending.key+".json");if(!is_sha256(pending.key)||!std::filesystem::exists(path)||read_file(path,16384)!=pending.bytes)throw std::runtime_error("OUTBOX_RECORD_MISMATCH");
 if(matches_ack(pending.bytes,response)){remove_durable(path);return true;}
 if(response.status!=0&&response.status!=200&&response.status!=201&&!retryable_http(response.status))durable_file(outbox_/(pending.key+".blocked"),"DELIVERY_CONFLICT");return false;
}
} // namespace tire_health
