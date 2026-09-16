<!-- SPDX-FileCopyrightText: 2026 maninblack -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Tire Health service — source integration candidate

## Temporary Test-only lifecycle mode

The explicit final bootstrap argument `--demo-no-telemetry` is a temporary
Cloud-permissions workaround accepted on 11 September 2026. It validates native
identity, packaged version and public metadata, rejects Production, and keeps
only the bootstrap alive until SIGTERM/SIGINT. It does not start analytics,
request a token, connect to KUKSA, emit derived records or produce advisory.
Its single lifecycle event reports `NOT_READY / TELEMETRY_DISABLED`.
It is never selected automatically when authorization fails.

Build, package and publish through Demo Control. Preparation requires both
`--without-permissions --demo-no-telemetry`; this explicit package mode also
requests `noFileLimit: 1024` for native container construction. Normal bootstrap
authorization and normal package contracts remain unchanged.

Independent Function Team 2 C++17 service for the AosEdge SDV demo. This is not
a production diagnostic or a qualified P7 artifact. It does not infer tread
depth, useful life or simulator oracle state.

The authoritative Solution sources are D4-018
`contracts/tire-health-model/tire-health-product-profile.v1.json`, D4-019
`contracts/tire-cloud-api/tire-cloud-api-profile.v1.json`,
`docs/architecture/demo-control-service-inputs.md`,
`docs/requirements/components/tire-health-service.md` and D4-023 qualification.
Common JSON/hash/UUID/bootstrap/transport primitives were adapted from Brake
`6fb8e02`; Tire product logic, identity, producer epoch, storage, backend and
resource paths are independent.

## Implemented and unqualified boundaries

| Implemented source | Not yet established |
| --- | --- |
| KAC bootstrap, private expiring JWT, TLS KUKSA adapter and exact 15-path metadata checking | Actual Linux ARM64 gRPC build/container run |
| Source-time maneuver segmentation, approved raw features and BPS model/hysteresis | Live raw-input classification and advisory qualification |
| Persistent derived-only outbox, matching durable ACK, retries/conflict retention | Complete crash-injection/live restart/network-isolation qualification |
| Typed Tire advisory lease, sequence and Gateway correlation | Live advisory proof; Set success is not application evidence |
| Function-status builder and independent outbound delivery | Complete five-axis readiness integration in product executable |
| Native fixtures and cross-language wire validation | Calibration, independent 10/10 classification and quota proof |

The 16 September operator amendment defines raw wheel dispersion as
`(maxWheelSpeed-minWheelSpeed)/max(maxWheelSpeed,5 km/h)`, reduced by the
maximum over valid active samples. Slip persistence is the fraction of valid
active samples with any wheel at absolute longitudinal slip >=0.08 or lateral
slip angle >=4 degrees. The product now extracts those real-input features;
the internal normalized-feature seam is not an operator endpoint. Invalid or
incomplete episodes never fabricate an assessment, GOOD state or advisory.
Live real-input/advisory qualification remains required.

## Exact runtime/package inputs

```text
/usr/bin/tire-health-bootstrap
  --metadata-file /run/aosedge/platform/service-inputs/metadata.json
  --ca-file /run/aosedge/platform/service-inputs/kuksa-ca.pem
```

Bootstrap starts `/usr/bin/tire-health-service` with those arguments. Only
bootstrap consumes `AOS_SECRET`; it removes the variable before starting the
child. Bootstrap creates a fresh private 0700 session under the per-container
1777 tmpfs. Analytics receives only the KUKSA_TOKEN_FILE path at
`/run/aosedge/secrets/kuksa/session-<random>/token.jwt`. Tokens are regular
0400 files, renewed at 180 seconds and invalidated at 300 seconds. Parent/leaf
symlinks, hard links and wrong ownership/modes are rejected. Normal exit
removes only its own session. Restart never adopts a token. Eight session
entries bounds crash orphans; exhaustion fails without deleting a peer
session. Container destruction clears the tmpfs. No secret enters state or logs.

Named resources: `kuksa`, `kuksa-auth-client`, `tire-runtime-inputs`.
Demo Control projects exactly `metadata.json` and public `kuksa-ca.pem` from
`/run/aos-demo-service-inputs/tire` to
`/run/aosedge/platform/service-inputs`, read-only with
`nosuid,nodev,noexec`. Never mount a shared parent or Brake resource directory.

Private-session source implements
[ADR 0015](../aosedge-sdv-demo/docs/architecture/decisions/0015-use-native-aos-service-runtime-inputs.md)
without an SM patch. Native input readers and all four revision-2 product
message kinds now pass host tests and offline backend conformance. Package
assembly/public projection, boot ordering and ARM64/live proof remain pending;
do not publish this source checkpoint as a qualified runtime.

The public reader accepts exactly five keys: `schemaVersion: 2`,
`unitSystemUid`, `unitRole`, `vdpContractVersion` and
`vdpContractSha256`. Role is lower-case on input and upper-case in messages.
Both entry points read `/usr/share/aosedge/service-release.json`
(`schemaVersion: 1`, `serviceVersion`) and the four standard Aos identity
environment variables once at startup. Versions are strict `X.Y.Z`, max
32 characters; neither package nor public input overrides native identity.
Products use revision 2 / 2.0.0 with `serviceInstance`, without an OCI digest.
The VDP pair still identifies the committed compatibility contract.

Queued messages remain byte-identical across restart/update. New requests
persist their original metadata as `lastRequestMetadata` in the existing
private wrapper state, independently of the unchanged 13-field model state.
The reader accepts old eight-field wrapper state and new bound state; no new
store, epoch rotation or state deletion is introduced. A legacy request without
a saved binding cannot produce a fact using current metadata. Its stored bytes
remain intact, and normal advisory refresh uses the next sequence in the same
epoch to create a bound request. Request/GatewayStatus wire revision stays 1.
Input/CA/token replacement reconnects KUKSA; VDP pair change aborts the old
episode. UID/role/service/native-instance change is rejected in-process.

KUKSA uses TLS at `Server:55555`; KAC uses
`/run/aosedge/platform/kuksa-auth/request.sock`. The exact 15 paths are in
`include/tire_health/runtime/runtime.hpp`. Only Tire Advisory.Request is
writable; GatewayStatus is read-only. The pinned VDP KUKSA producer uses float
for all 15 numeric inputs; longitudinal slip has no unit, lateral slip is in
degrees. No oracle/friction/profile/vehicle-control path is read.

Outbound backend route is the accepted isolated demo route
`http://10.0.0.1:18092/api/v1/tire/messages`. This is not a production
authenticated connection. Persistent roots follow D4-018:
`/storage/tire-health/state/v1` and `/storage/tire-health/outbox/v1`.
State maximum is 128 KiB; outbox maximum is 256 messages/2 MiB. Overflow keeps
existing messages and rejects new outbound records, without blocking local
model/state persistence. Unknown/corrupt state remains intact and held
`NOT_READY_STATE`; physical quarantine and all crash-point recovery are not
yet complete. Ordinary restart preserves producer epoch/sequence.

Package requests: `instances.minInstances: 1`, `offlineTTL: P7D`, 150 DMIPS,
RAM 16 MiB, storage 4 MiB, state 2 MiB, tmp 2 MiB, 32 files and 8 PIDs. These
are requests to AosCore, not measurements. Packaging/signing/upload and all
live operations belong to Demo Control, not a new wrapper in this repository.

## Source checks and product build requirements

```sh
cmake -S . -B /tmp/tire-host-build -DBUILD_TESTING=ON
cmake --build /tmp/tire-host-build
ctest --test-dir /tmp/tire-host-build --output-on-failure
```

Default builds are host/domain/bootstrap only. Product builds must explicitly
set `THS_BUILD_KUKSA_RUNTIME=ON`, supply a Linux ARM64 toolchain/sysroot,
gRPC **1.60.1**, Protobuf **25.8.0**, and `THS_KUKSA_SOURCE_ROOT` at commit
`30e5c13abc496d0b39aaa6c25acebb088b9902e3`. CMake checks VAL file hashes and
rejects modifications; it downloads nothing. Install prefix `/usr` stages
both executables. The preparation-only Docker recipe and
[product exporter](docs/product-build.md) close static third-party linkage,
verify the actual glibc-only ARM64 dependency boundary, preserve public
licenses and require all three successful CTest suites. Missing dependencies
fail the build; there is no diagnostic binary or placeholder product hash.

Actual builds remain exclusively owned by Demo Control:
`democtl service build tire --content-profile v1`. Tire has only fixed content
profile `v1`; release allocation remains a later package-preparation action.
The recipe changes no native container launch, recovery or startup behavior.
Source/host tests do not establish an ARM64 export or live qualification.

`tire_health_tests --emit-conformance` emits four synthetic message kinds for
legacy offline validation. `--emit-native-conformance` emits actual revision-2
output; `node tests/native_backend_conformance.mjs <test-binary> <backend-checkout>`
checks all four kinds, exact retry receipts and queries in an in-memory store.
This test binary is not
installed and must not be put into the service image.

## Explicit remaining gates

1. Agree raw wheel-dispersion/reference arithmetic and strict versus inclusive
   persistence thresholds; normalized fixtures alone cannot define extraction.
   Calibrate and freeze configuration before acceptance.
2. Specify CPU proof wire messages and lease cadence. Fixed START/STOP, exact
   identity binding, one worker and 180-second ceiling are known. No guessed
   API, caller-chosen shell/intensity/duration, or CPU worker is enabled.
3. Complete physical corrupt-state quarantine, replacement epoch rotation,
   crash-point matrix and readiness/overflow publication integration.
4. Build/run real ARM64 gRPC through Demo Control and test resource/KAC/TLS
   boundaries, metadata renewal and independent delivery on Test.
5. Run accepted calibration, fresh-state classification, live advisory,
   disconnect/restart and AosCore CPU-isolation qualification. Host tests and
   backend receipts cannot substitute for those observations.
