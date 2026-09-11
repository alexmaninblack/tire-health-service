// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "tire_health/runtime/application.hpp"
#include "tire_health/runtime/sha256.hpp"
#include "tire_health/service.hpp"
#include <csignal>
#include <iostream>
#include <thread>

namespace tire_health::runtime {
namespace demo_mock {
inline std::atomic<bool> stopped{false};
inline void stop(int) { stopped.store(true, std::memory_order_relaxed); }
inline void require_test(const Metadata& metadata) {
    if (metadata.unit_role != "VALIDATION" || !metadata.service_instance)
        throw std::runtime_error("DEMO_MOCK_NATIVE_TEST_ONLY");
}
inline void generate(tire_health::Runtime& runtime, std::int64_t now) {
    // Deliberately synthetic normalized features, not the unfinished production
    // feature extractor. No Gateway status or vehicle advisory is fabricated.
    Episode episode{random_uuid(), now - 3000, now, {}, "COMPLETE"};
    episode.samples.resize(30);
    const auto config = sha256_hex("DEMO_MOCK:tire-normalized-features:7000,6000,5000,5500:v1");
    if (!runtime.apply_episode({7000, 6000, 5000, 5500}, episode, config))
        throw std::runtime_error("DEMO_MOCK_FIXTURE_INVALID");
    runtime.function_status("SERVICE_ACCESS_DENIED", now);
}
}
inline int run_demo_mock(const Metadata& metadata) {
    demo_mock::require_test(metadata);
    tire_health::Runtime runtime("/storage/tire-health/demo-mock/state", "/storage/tire-health/demo-mock/outbox", metadata);
    if (!runtime.state_ready()) throw std::runtime_error("DEMO_MOCK_STATE_INVALID");
    demo_mock::stopped = false;
    const auto old_int = std::signal(SIGINT, demo_mock::stop);
    const auto old_term = std::signal(SIGTERM, demo_mock::stop);
    std::cout << "{\"eventType\":\"DEMO_MOCK_STARTED\",\"source\":\"DEMO_MOCK\",\"vehicleTelemetry\":false,\"serviceVersion\":"
              << quote_json(metadata.service_version) << "}" << std::endl;
    std::int64_t next_episode = 0, next_attempt = 0;
    unsigned failures = 0;
    while (!demo_mock::stopped) {
        const auto boot = boot_milliseconds();
        const auto pending = runtime.next_message();
        if (pending && boot >= next_attempt) {
            HttpResponse response;
            try { response = post_backend(pending->bytes, demo_mock::stopped, true); } catch (...) {}
            const bool accepted = runtime.accept(*pending, response);
            std::cout << "{\"eventType\":\"DEMO_MOCK_DELIVERY\",\"source\":\"DEMO_MOCK\",\"accepted\":"
                      << (accepted ? "true" : "false") << ",\"httpStatus\":" << response.status << "}" << std::endl;
            if (accepted) { failures = 0; next_attempt = 0; }
            else next_attempt = boot_milliseconds() + retry_delay(failures++, 0, response.retry_after) * 1000LL;
        } else if (!pending && boot >= next_episode) {
            demo_mock::generate(runtime, wall_milliseconds());
            next_episode = boot + 30000;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    runtime.stop();
    std::signal(SIGINT, old_int); std::signal(SIGTERM, old_term);
    return 0;
}
}
