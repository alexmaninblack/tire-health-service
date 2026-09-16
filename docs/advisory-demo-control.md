<!-- SPDX-FileCopyrightText: 2026 maninblack -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Current-Test advisory demo control

Functional V1 emits its own fixed Tire `Advisory.Readiness` actuator every five seconds from actual telemetry readiness and polls only `/api/v1/tire/demo-control/poll` on its existing backend. No inbound control listener is added.

The fixed RESET_DEMO_SCENARIO envelope binds Unit, installed release, native four-field service instance and persistent epoch. Commands expire after 60 seconds. Reset atomically clears model/capture to NOT_EVALUATED and stops refreshing the old warning, retaining history, outbox, deduplication, epoch and increasing sequence. No healthy assessment or repair claim is invented.

A fresh typed CLEAR uses the reset UUID as decision ID. Completion requires correlated Gateway CLEARED; the result is retried through `/api/v1/tire/demo-control/ack`. Duplicate/restart recovery is idempotent; a release/instance change rejects pending authority. Ordinary restart retains product state.

Host tests cover reset/restart, wrong/expired commands, matching acknowledgement, new warnings and retained sequence/history. Live staging and clean Factory qualification remain separate gates.

