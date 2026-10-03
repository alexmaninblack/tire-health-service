# SPDX-FileCopyrightText: 2026 maninblack
# SPDX-License-Identifier: Apache-2.0
"""Pinned VAL request regression; this does not qualify the raw Tire model."""
import unittest
from pathlib import Path


class SubscribeContractTests(unittest.TestCase):
    def test_transport_retention_never_reuses_rpc_authorization(self):
        source = (Path(__file__).resolve().parents[1] / "src/runtime/grpc_main.cpp").read_text()
        transport = source.split("class SessionTransport", 1)[1].split("class Log", 1)[0]
        self.assertIn("!channel_ || ca_ != ca", transport)
        self.assertNotIn("token", transport)
        self.assertNotIn("AddMetadata", transport)
        subscription = source.split("void subscribe(", 1)[1].split("int main(", 1)[0]
        self.assertIn("read_private_token(token_file)", subscription)
        self.assertIn('AddMetadata("authorization", "Bearer " + token)', subscription)
        self.assertIn("verify_metadata(response);", subscription)
        main = source.split("int main(", 1)[1]
        renewal = main.split("catch (const ReauthenticationRequired&)", 1)[1].split("catch (const std::exception&", 1)[0]
        self.assertNotIn("transport.clear()", renewal)
        self.assertIn("transport.clear();", main.split("catch (const std::exception&", 1)[1])
        self.assertIn("subscribe(runtime, inputs, stop, log, transport); transport.clear();", main)

    def test_readiness_clock_is_sampled_after_ingest_lock(self):
        root = Path(__file__).resolve().parents[1]
        grpc = (root / "src/runtime/grpc_main.cpp").read_text()
        runtime = (root / "src/runtime.cpp").read_text()
        self.assertIn("runtime.advisory_readiness(wall_milliseconds);", grpc)
        clocked = runtime.split("Runtime::advisory_readiness(const std::function", 1)[1].split("Runtime::observation_binding", 1)[0]
        self.assertLess(clocked.index("lock(mutex_)"), clocked.index("const auto now=clock();"))
        self.assertIn("now>=telemetry_at_", clocked)
        self.assertIn("now-telemetry_at_<=5000", clocked)

    def test_subscription_failures_use_the_tested_observation_mapping(self):
        source = (Path(__file__).resolve().parents[1] / "src/runtime/grpc_main.cpp").read_text()
        self.assertIn("const auto observation = subscription_failure_observation(code);", source)
        self.assertIn("runtime.input_observation(observation.connection, observation.input, observation.reason);", source)
        self.assertIn("grpc::StatusCode::UNAUTHENTICATED", source)
        self.assertIn("grpc::StatusCode::PERMISSION_DENIED", source)

    def test_renewal_recreates_stream_without_skipping_authentication(self):
        source = (Path(__file__).resolve().parents[1] / "src/runtime/grpc_main.cpp").read_text()
        self.assertIn("inspect_session_inputs(inputs, token_file, token, metadata_bytes, ca)", source)
        self.assertIn("catch (const ReauthenticationRequired&)", source)
        self.assertIn("runtime.disconnect();", source)
        self.assertIn("grpc::StatusCode::CANCELLED", source)
        self.assertIn("grpc::StatusCode::UNAUTHENTICATED", source)
        self.assertIn("pause(stop, 1000);", source)  # Real failures retain backoff.
        renewal = source.rsplit("catch (const ReauthenticationRequired&)", 1)[1].split("catch (const std::exception&", 1)[0]
        self.assertIn("continue;", renewal)
        self.assertNotIn("pause(", renewal)
        self.assertIn("runtime.reauthenticate();", renewal)
        self.assertNotIn("runtime.disconnect();", renewal)
        finish = source.split("const auto stream_status = reader->Finish();", 1)[1].split("int main(", 1)[0]
        self.assertLess(finish.index("throw ReauthenticationRequired{}"), finish.index("runtime.disconnect();"))

    def test_bad_advisory_status_does_not_report_failed_telemetry(self):
        source = (Path(__file__).resolve().parents[1] / "src/runtime/grpc_main.cpp").read_text()
        self.assertIn('log.state("ADVISORY_STATUS_CHANGED","UNAVAILABLE","GATEWAY_STATUS_INVALID")', source)
        self.assertNotIn('log.state("READINESS_CHANGED","NOT_READY","GATEWAY_STATUS_INVALID")', source)

    def test_auth_diagnostic_uses_validated_code_without_response(self):
        source = (Path(__file__).resolve().parents[1] / "src/runtime/bootstrap_main.cpp").read_text()
        self.assertIn("if (credential.token.empty()) auth_state(false, credential.code)", source)
        self.assertIn("if (reason == previous) return", source)
        self.assertNotIn("<< credential.token", source)

    def test_telemetry_and_status_request_value_fields(self):
        source = (Path(__file__).resolve().parents[1] / "src/runtime/grpc_main.cpp").read_text()
        request = source.split("val::SubscribeRequest subscription;", 1)[1].split("auto reader =", 1)[0]
        self.assertEqual(2, request.count("entry->add_fields(val::FIELD_VALUE)"))
        self.assertIn("entry->set_path(path)", request)
        self.assertIn("entry->set_path(status_path)", request)
        self.assertNotIn("FIELD_ACTUATOR_TARGET", request)


if __name__ == "__main__":
    unittest.main()
