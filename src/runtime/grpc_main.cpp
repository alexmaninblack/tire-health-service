// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#include "tire_health/runtime/application.hpp"
#include "tire_health/runtime/session_reconnect.hpp"
#include "tire_health/runtime/json.hpp"
#include "tire_health/service.hpp"
#include "kuksa/val/v1/val.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <thread>

namespace {
using namespace tire_health::runtime;
using tire_health::Runtime;
namespace val = kuksa::val::v1;
volatile std::sig_atomic_t interrupted = 0;
void signal_handler(int) { interrupted = 1; }
class Log {
    std::mutex mutex_;
    std::map<std::string, std::string> previous_;
    std::int64_t minute_{};
    unsigned emitted_{}, suppressed_{};
public:
    void state(const std::string& event, const std::string& state, const std::string& reason, const std::string& source_event = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto key = state + ':' + reason;
        if (event != "EXERCISE_COMPLETED" && previous_[event] == key) return;
        const auto minute = boot_milliseconds() / 60000;
        if (minute != minute_) { minute_ = minute; emitted_ = 0; }
        if (emitted_ >= 60) { ++suppressed_; return; }
        previous_[event] = key; ++emitted_;
        std::cout << "{\"schemaVersion\":1,\"eventType\":" << quote_json(event)
                  << ",\"severity\":\"INFO\",\"observedAt\":" << quote_json(utc_timestamp(wall_milliseconds()))
                  << ",\"currentState\":" << quote_json(state) << ",\"reasonCode\":" << quote_json(reason)
                  << ",\"count\":" << 1 + suppressed_;
        if (!source_event.empty()) std::cout << ",\"sourceEventId\":" << quote_json(source_event);
        std::cout << '}' << std::endl;
        suppressed_ = 0;
    }
};
void pause(const std::atomic<bool>& stop, std::int64_t milliseconds) {
    const auto end = boot_milliseconds() + milliseconds;
    while (!stop && !interrupted && boot_milliseconds() < end) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
std::size_t path_index(const std::string& path) {
    const auto position = std::find(paths.begin(), paths.end(), path);
    if (position == paths.end()) throw std::runtime_error("KUKSA_UNREQUESTED_PATH");
    return static_cast<std::size_t>(position - paths.begin());
}
void verify_metadata(const val::GetResponse& response) {
    if (response.has_error() || response.errors_size() || response.entries_size() != 18) throw std::runtime_error("VDP_INCOMPATIBLE");
    std::set<std::size_t> seen;
    for (const auto& entry : response.entries()) {
        const auto& metadata = entry.metadata();
        if(entry.path()==request_path || entry.path()==status_path || entry.path()==readiness_path) {
            const auto index=entry.path()==request_path?15U:entry.path()==status_path?16U:17U;
            if(!seen.insert(index).second || !entry.has_metadata() || metadata.data_type()!=val::DATA_TYPE_STRING || metadata.entry_type()!=(index==16?val::ENTRY_TYPE_SENSOR:val::ENTRY_TYPE_ACTUATOR)) throw std::runtime_error("VDP_INCOMPATIBLE");
            continue;
        }
        const auto index = path_index(entry.path());
        const std::string unit = index==0 || (index>=3 && index<7) ? "km/h" : index==1 ? "m/s^2" : index==2 || index>=11 ? "degrees" : "";
        if (!seen.insert(index).second || !entry.has_metadata() || metadata.entry_type() != val::ENTRY_TYPE_SENSOR ||
            metadata.data_type() != val::DATA_TYPE_FLOAT ||
            (unit.empty() ? metadata.has_unit() : !metadata.has_unit() || metadata.unit() != unit)) throw std::runtime_error("VDP_INCOMPATIBLE");
    }
}
Signal signal(const val::DataEntry& entry, std::size_t index) {
    (void)index;
    const auto& value = entry.value();
    if (!entry.has_value() || !value.has_timestamp() || value.timestamp().seconds() < 0 ||
        value.timestamp().seconds() > 253402300799LL || value.timestamp().nanos() < 0 || value.timestamp().nanos() >= 1000000000) return {};
    // The pinned VDP KUKSA schema uses float for all fifteen Tire inputs.
    if (value.value_case() != val::Datapoint::kFloat) return {};
    return {static_cast<double>(value.float_()),
            value.timestamp().seconds() * 1000 + value.timestamp().nanos() / 1000000, true};
}
void deliver(Runtime& runtime, std::atomic<bool>& stop, Log& log) {
    std::mt19937 random(std::random_device{}());
    std::uniform_real_distribution<double> jitter(-0.2, 0.2);
    unsigned attempt = 0;
    std::int64_t next_control=0,next_delivery=0;
    std::unique_ptr<ObservationStream> observations;
    try {
        if(const auto binding=runtime.observation_binding())
            observations=std::make_unique<ObservationStream>("/storage/tire-health/function-observations/v3",*binding);
    } catch(...) {log.state("FUNCTION_OBSERVATION_CHANGED","UNAVAILABLE","OBSERVATION_STORAGE_UNAVAILABLE");}
    unsigned observation_attempt=0;
    std::int64_t next_observation=0,next_observation_delivery=0;
    while (!stop && !interrupted) {
        // Accepted function-observation v3 uses only the existing private
        // Mac-hosted team backend. Its receipt never confirms product delivery.
        if(observations)try {
            if(boot_milliseconds()>=next_observation) {
                observations->observe(runtime.function_observation(),wall_milliseconds(),boot_milliseconds());
                next_observation=boot_milliseconds()+5000;
            }
            if(boot_milliseconds()>=next_observation_delivery) {
                if(const auto pending=observations->next()) {
                    HttpResponse response;
                    try {response=post_backend(*pending,stop);}catch(...){}
                    if(observations->accept(*pending,response.status,response.body)) {
                        observation_attempt=0;next_observation_delivery=boot_milliseconds()+100;
                    } else next_observation_delivery=boot_milliseconds()+retry_delay(observation_attempt++,jitter(random),response.retry_after)*1000LL;
                }
            }
        }catch(...) {
            observations.reset();
            log.state("FUNCTION_OBSERVATION_CHANGED","UNAVAILABLE","OBSERVATION_STORAGE_UNAVAILABLE");
        }
        if(boot_milliseconds()>=next_control) {
            try {
                if(const auto ack=runtime.demo_control_ack(wall_milliseconds())) {
                    const auto response=post_demo_control(*ack,stop,true);
                    if(response.status==200)runtime.demo_control_accepted(response.body);
                }
                if(const auto poll=runtime.demo_control_poll()) {
                    const auto response=post_demo_control(*poll,stop,false);
                    if(response.status==200)runtime.demo_control_command(response.body,wall_milliseconds());
                }
            } catch(...) {log.state("DEMO_CONTROL_CHANGED","UNAVAILABLE","RESET_CONTROL_UNAVAILABLE");}
            next_control=boot_milliseconds()+5000;
        }
        if(boot_milliseconds()<next_delivery){pause(stop,100);continue;}
        try {
            const auto pending = runtime.next_message();
            if (!pending) { attempt = 0; pause(stop, 100); continue; }
            HttpResponse response;
            try { response = post_backend(pending->bytes, stop); } catch (...) {}
            if (runtime.accept(*pending, response)) {
                attempt = 0; log.state("BACKEND_CONNECTION_CHANGED", "CONNECTED", "NONE");
            } else {
                log.state("BACKEND_CONNECTION_CHANGED", "BACKLOG", "NONE");
                next_delivery=boot_milliseconds()+retry_delay(attempt++, jitter(random), response.retry_after)*1000LL;
            }
        } catch (...) {
            log.state("READINESS_CHANGED", "NOT_READY", "STORAGE_UNAVAILABLE");
            pause(stop, 1000);
        }
    }
}
// One context per session, watched independently of blocking gRPC calls.
// Token loss/change cancels the subscription, including a stalled Get/Read.
void subscribe(Runtime& runtime, const ApplicationInputs& inputs, std::atomic<bool>& stop, Log& log) {
    const auto metadata_bytes = read_file(inputs.metadata_file, 8192);
    runtime.update_vdp_metadata(runtime_metadata(inputs, metadata_bytes));
    const auto ca = read_file(inputs.ca_file, 65536);
    const auto token_file = token_file_from_environment();
    const auto token = read_private_token(token_file);
    grpc::SslCredentialsOptions tls; tls.pem_root_certs = ca;
    grpc::ChannelArguments arguments;
    arguments.SetMaxReceiveMessageSize(65536);
    auto channel = grpc::CreateCustomChannel("Server:55555", grpc::SslCredentials(tls), arguments);
    auto stub = val::VAL::NewStub(channel);
    std::mutex context_mutex;
    std::shared_ptr<grpc::ClientContext> active, advisory_context;
    std::atomic<bool> invalid{false}, finished{false};
    SessionInterruption interruption;
    std::atomic<std::int64_t> last_frame{boot_milliseconds()};
    std::thread watcher([&] {
        while (!finished) {
            if (stop || interrupted) interruption.observe(SessionInputChange::Unavailable);
            interruption.observe(inspect_session_inputs(inputs, token_file, token, metadata_bytes, ca));
            const bool cancel = interruption.cancelled();
            if (cancel) {
                invalid = true;
                std::lock_guard<std::mutex> lock(context_mutex);
                if (active) active->TryCancel();
                if (advisory_context) advisory_context->TryCancel();
            }
            if (boot_milliseconds() - last_frame.load() > 250) {
                try {
                    runtime.disconnect();
                    runtime.input_observation("CONNECTED","STALE","SOURCE_GAP");
                    log.state("READINESS_CHANGED", "NOT_READY", "KUKSA_DATA_UNAVAILABLE");
                } catch (...) {
                    interruption.observe(SessionInputChange::Unavailable);
                    invalid = true;
                    std::lock_guard<std::mutex> lock(context_mutex);
                    if (active) active->TryCancel();
                    if (advisory_context) advisory_context->TryCancel();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    struct Join { std::atomic<bool>& finished; std::thread& thread; ~Join() { finished = true; thread.join(); } } join{finished, watcher};
    const auto make_context = [&] {
        auto result = std::make_shared<grpc::ClientContext>();
        result->AddMetadata("authorization", "Bearer " + token);
        std::lock_guard<std::mutex> lock(context_mutex); active = result;
        if (invalid) result->TryCancel();
        return result;
    };
    val::GetRequest request; val::GetResponse response;
    for (const auto* path : paths) { auto* entry = request.add_entries(); entry->set_path(path); entry->set_view(val::VIEW_METADATA); }
    for (const auto* path : {request_path,status_path,readiness_path}) {auto* entry=request.add_entries();entry->set_path(path);entry->set_view(val::VIEW_METADATA);}
    auto get_context = make_context();
    get_context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(8));
    const auto get_status = stub->Get(get_context.get(), request, &response);
    if (!get_status.ok()) {
        const auto code = get_status.error_code();
        interruption.observe(inspect_session_inputs(inputs, token_file, token, metadata_bytes, ca));
        if (code == grpc::StatusCode::CANCELLED && interruption.token_replaced() && !stop && !interrupted)
            throw ReauthenticationRequired{};
        throw std::runtime_error(code == grpc::StatusCode::UNAUTHENTICATED || code == grpc::StatusCode::PERMISSION_DENIED
            ? "KUKSA_AUTH_UNAVAILABLE" : "KUKSA_DATA_UNAVAILABLE");
    }
    verify_metadata(response);
    runtime.input_observation("CONNECTED","WAITING","AWAITING_INPUT");
    log.state("VDP_COMPATIBILITY_CHANGED", "READY", "NONE");
    std::thread advisory([&] {
        std::int64_t next_readiness=0;
        while(!finished && !stop && !interrupted && !invalid) {
            try {
                const auto bytes=runtime.next_advisory(wall_milliseconds());
                if(bytes) {
                    auto context=std::make_shared<grpc::ClientContext>();context->AddMetadata("authorization","Bearer "+token);
                    context->set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(2));
                    {std::lock_guard<std::mutex> lock(context_mutex);advisory_context=context;if(invalid)context->TryCancel();}
                    val::SetRequest request;val::SetResponse response;auto* update=request.add_updates();update->mutable_entry()->set_path(request_path);
                    update->mutable_entry()->mutable_actuator_target()->set_string(*bytes);update->add_fields(val::FIELD_ACTUATOR_TARGET);
                    (void)stub->Set(context.get(),request,&response);
                    // Transport success is never Gateway application evidence.
                    log.state("ADVISORY_REQUESTED","REQUESTED","NONE");
                    {std::lock_guard<std::mutex> lock(context_mutex);advisory_context.reset();}
                }
                if(boot_milliseconds()>=next_readiness) {
                    auto context=std::make_shared<grpc::ClientContext>();context->AddMetadata("authorization","Bearer "+token);
                    context->set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(2));
                    {std::lock_guard<std::mutex> lock(context_mutex);advisory_context=context;if(invalid)context->TryCancel();}
                    val::SetRequest request;val::SetResponse response;auto* update=request.add_updates();update->mutable_entry()->set_path(readiness_path);
                    update->mutable_entry()->mutable_actuator_target()->set_string(runtime.advisory_readiness(wall_milliseconds()));
                    update->add_fields(val::FIELD_ACTUATOR_TARGET);(void)stub->Set(context.get(),request,&response);
                    {std::lock_guard<std::mutex> lock(context_mutex);advisory_context.reset();}
                    next_readiness=boot_milliseconds()+5000;
                }
            } catch (...) {runtime.advisory_observation("UNAVAILABLE");log.state("ADVISORY_STATUS_CHANGED","UNAVAILABLE","ADVISORY_UNAVAILABLE");}
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    struct AdvisoryJoin {std::atomic<bool>& finished;std::thread& thread;~AdvisoryJoin(){finished=true;thread.join();}} advisory_join{finished,advisory};
    val::SubscribeRequest subscription;
    // The pinned KUKSA Subscribe handler uses explicit fields, not Get views.
    for (const auto* path : paths) { auto* entry = subscription.add_entries(); entry->set_path(path); entry->set_view(val::VIEW_CURRENT_VALUE); entry->add_fields(val::FIELD_VALUE); }
    {auto* entry=subscription.add_entries();entry->set_path(status_path);entry->set_view(val::VIEW_CURRENT_VALUE);entry->add_fields(val::FIELD_VALUE);}
    auto stream_context = make_context();
    auto reader = stub->Subscribe(stream_context.get(), subscription);
    struct Cancel {
        std::shared_ptr<grpc::ClientContext> context;
        ~Cancel() { context->TryCancel(); }
    } cancel{stream_context};
    std::array<Signal, 15> values{};
    std::int64_t previous_epoch = -1;
    val::SubscribeResponse update;
    while (reader->Read(&update)) {
        if (stop || interrupted || invalid) break;
        std::set<std::size_t> seen;
        for (const auto& item : update.updates()) {
            if(item.entry().path()==status_path) {
                if(item.entry().has_value() && item.entry().value().value_case()==val::Datapoint::kString) {
                    try {runtime.gateway_status(item.entry().value().string(),wall_milliseconds());}
                    catch (...) {runtime.advisory_observation("UNAVAILABLE");log.state("ADVISORY_STATUS_CHANGED","UNAVAILABLE","GATEWAY_STATUS_INVALID");}
                }
                continue;
            }
            const auto index = path_index(item.entry().path());
            if (!seen.insert(index).second) throw std::runtime_error("KUKSA_DUPLICATE_UPDATE");
            values[index] = signal(item.entry(), index);
        }
        const auto now = boot_milliseconds();
        const auto frame = complete_frame(values, wall_milliseconds());
        if (!frame || frame->epoch_ms==previous_epoch) continue;
        const auto result = runtime.ingest(*frame);
        previous_epoch = frame->epoch_ms;
        last_frame = now;
        runtime.function_status("READY",wall_milliseconds());
        log.state("READINESS_CHANGED","READY","NONE");
        if(result) {
            const auto features=tire_health::extract_features(*result);
            const bool accepted=features && runtime.apply_episode(*features,*result,tire_health::kModelConfigSha256);
            log.state(accepted?"EXERCISE_COMPLETED":"EXERCISE_SKIPPED",
                accepted?"READY":"NOT_READY",accepted?"NONE":"ASSESSMENT_SKIPPED_INPUT_QUALITY",result->id);
        }
    }
    stream_context->TryCancel();
    const auto stream_status = reader->Finish();
    runtime.disconnect();
    interruption.observe(inspect_session_inputs(inputs, token_file, token, metadata_bytes, ca));
    if ((stream_status.ok() || stream_status.error_code() == grpc::StatusCode::CANCELLED) &&
        interruption.token_replaced() && !stop && !interrupted) throw ReauthenticationRequired{};
    log.state("KUKSA_CONNECTION_CHANGED", "NOT_READY", "KUKSA_DATA_UNAVAILABLE");
}
}
int main(int argc, char** argv) {
    std::atomic<bool> stop{false};
    Log log;
    try {
        auto inputs = parse_arguments(argc, argv);
        initialize_service_inputs(inputs);
        if (std::getenv("AOS_SECRET")) throw std::runtime_error("CREDENTIAL_BOUNDARY_INVALID");
        (void)token_file_from_environment();
        const auto metadata = runtime_metadata(inputs, read_file(inputs.metadata_file, 8192));
        Runtime runtime("/storage/tire-health/state/v1","/storage/tire-health/outbox/v1", metadata);
        std::signal(SIGINT, signal_handler); std::signal(SIGTERM, signal_handler);
        std::thread delivery([&] { deliver(runtime, stop, log); });
        struct Join { std::atomic<bool>& stop; std::thread& thread; ~Join() { stop = true; thread.join(); } } join{stop, delivery};
        log.state("SERVICE_STARTED", "RUNNING", "NONE");
        while (!interrupted) {
            try { subscribe(runtime, inputs, stop, log); }
            catch (const ReauthenticationRequired&) {
                runtime.disconnect();
                runtime.input_observation("REAUTHENTICATING","WAITING","REAUTHENTICATING");
                runtime.advisory_observation("REAUTHENTICATING");
                log.state("KUKSA_CONNECTION_CHANGED", "REAUTHENTICATING", "TOKEN_REPLACED");
                continue; // No synthetic assessment/ACK or access claim during renewal.
            }
            catch (const std::exception& error) {
                runtime.disconnect();
                const std::string code = error.what();
                const auto observation = subscription_failure_observation(code);
                runtime.input_observation(observation.connection, observation.input, observation.reason);
                runtime.advisory_observation("UNAVAILABLE");
                const auto reason = code == "KUKSA_AUTH_PENDING" ? "KUKSA_AUTH_PENDING" :
                    code == "KUKSA_AUTH_UNAVAILABLE" ? "KUKSA_AUTH_UNAVAILABLE" :
                    code == "VDP_INCOMPATIBLE" ? "VDP_INCOMPATIBLE" :
                    code == "IMMUTABLE_IDENTITY_CHANGED" || code == "INPUT_FILE_UNAVAILABLE" ? "STATE_INVALID" : "KUKSA_DATA_UNAVAILABLE";
                log.state("READINESS_CHANGED", "NOT_READY", reason);
                if(code=="VDP_INCOMPATIBLE")runtime.function_status("INCOMPATIBLE_VDP",wall_milliseconds(),std::vector<std::string>(paths.begin(),paths.end()));
                else if(code=="KUKSA_AUTH_UNAVAILABLE")runtime.function_status("SERVICE_ACCESS_DENIED",wall_milliseconds());
                else runtime.function_status("TELEMETRY_DISCONNECTED",wall_milliseconds());
            }
            pause(stop, 1000);
        }
        runtime.stop(); stop = true;
        log.state("SERVICE_STOPPED", "STOPPED", "NONE");
        return 0;
    } catch (...) {
        stop = true;
        log.state("READINESS_CHANGED", "NOT_READY", "STATE_INVALID");
        return 2;
    }
}
