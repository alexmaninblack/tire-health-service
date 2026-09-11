// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#include "tire_health/runtime/demo_mock.hpp"
#include <cassert>
#include <unistd.h>
using namespace tire_health::runtime;
int main(int argc, char** argv) {
    const bool emit = argc == 2 && std::string(argv[1]) == "--emit";
    const auto native = parse_service_inputs(R"({"schemaVersion":1,"serviceVersion":"6.0.0"})",
        {{"AOS_ITEM_ID","tire-service"},{"AOS_SUBJECT_ID","tire-subject"},{"AOS_INSTANCE_INDEX","0"},{"AOS_INSTANCE_ID","tire-instance"}});
    auto metadata = parse_metadata(std::string(R"({"schemaVersion":2,"unitSystemUid":"mock-test-unit","unitRole":"validation","vdpContractVersion":"18.0.0","vdpContractSha256":")") + std::string(64,'a') + "\"}", native);
    demo_mock::require_test(metadata);
    auto wrong = metadata; wrong.unit_role = "PRODUCTION";
    bool rejected = false; try { demo_mock::require_test(wrong); } catch (...) { rejected = true; } assert(rejected);
    wrong = metadata; wrong.service_instance.reset(); rejected = false;
    try { demo_mock::require_test(wrong); } catch (...) { rejected = true; } assert(rejected);
    char path[] = "/tmp/tire-mock-tests-XXXXXX"; assert(::mkdtemp(path));
    const auto root = std::filesystem::path(path); std::string retained;
    {
        tire_health::Runtime runtime(root / "state", root / "outbox", metadata);
        demo_mock::generate(runtime, 1789162000000LL);
        auto pending = runtime.next_message(); assert(pending); retained = pending->bytes;
        assert(!runtime.accept(*pending, {503, "", 0}));
    }
    tire_health::Runtime runtime(root / "state", root / "outbox", metadata);
    assert(runtime.next_message()->bytes == retained);
    unsigned count = 0;
    while (auto pending = runtime.next_message()) {
        const auto value = parse_json(pending->bytes);
        assert(value.at("schemaVersion").integer() == 2);
        assert(value.at("serviceVersion").string() == "6.0.0");
        assert(value.at("messageType").string() != "TIRE_ADVISORY_FACT");
        if (emit) std::cout << pending->bytes << '\n';
        const auto ack = "{\"schemaVersion\":1,\"contractVersion\":\"1.0.0\",\"receiptId\":\"00000000-0000-4000-8000-000000000001\",\"messageKeySha256\":" + quote_json(message_key(value)) + ",\"contentSha256\":" + quote_json(value.at("contentSha256").string()) + ",\"state\":\"DURABLE_ACCEPTED\",\"receivedAt\":\"2026-09-11T21:00:00Z\"}";
        assert(runtime.accept(*pending, {201, ack, 0})); ++count;
    }
    assert(count == 3); std::filesystem::remove_all(path);
}
