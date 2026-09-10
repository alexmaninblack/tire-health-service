<!-- SPDX-FileCopyrightText: 2026 maninblack -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Tire Health service — source integration candidate

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
| Source-time maneuver segmentation and normalized BPS model/hysteresis | Raw dispersion denominator and persistence equality rule absent from accepted sources |
| Persistent derived-only outbox, matching durable ACK, retries/conflict retention | Complete crash-injection/live restart/network-isolation qualification |
| Typed Tire advisory lease, sequence and Gateway correlation | Live advisory proof; Set success is not application evidence |
| Function-status builder and independent outbound delivery | Complete five-axis readiness integration in product executable |
| Native fixtures and cross-language wire validation | Calibration, independent 10/10 classification and quota proof |

The product executable reports `MODEL_CONTRACT_UNRESOLVED` when compatible
fresh input arrives. It does **not** call the normalized-feature model seam
with invented extraction arithmetic. That seam is internal C++, not an
operator endpoint or live fixture input. No fabricated assessment, GOOD state
or advisory bypasses the gate.

## Exact runtime/package inputs

```text
/usr/bin/tire-health-bootstrap
  --metadata-file /run/aosedge/platform/service-inputs/metadata.json
  --ca-file /run/aosedge/platform/service-inputs/kuksa-ca.pem
```

Bootstrap starts `/usr/bin/tire-health-service` with those arguments. Only
bootstrap consumes `AOS_SECRET`; it removes the variable before starting the
child. Analytics receives only `KUKSA_TOKEN_FILE`, fixed at
`/run/aosedge/secrets/kuksa/token.jwt`. Its private tmpfs parent must exist,
belong to the service user, and be `0700`; tokens are replaced atomically as
`0400`, renewed at 180 seconds and invalidated at 300 seconds. No secret,
token or private key belongs in state/messages/logs.

Named resources: `kuksa`, `kuksa-auth-client`, `tire-runtime-inputs`.
Demo Control projects exactly `metadata.json` and public `kuksa-ca.pem` from
`/run/aos-demo-service-inputs/tire` to
`/run/aosedge/platform/service-inputs`, read-only with
`nosuid,nodev,noexec`. Never mount a shared parent or Brake resource directory.

Metadata has exactly seven keys: `schemaVersion`, `unitSystemUid`, `unitRole`,
`serviceVersion`, `serviceArtifactSha256`, `vdpContractVersion`,
`vdpContractSha256`. Input role is `validation` or `production`, upper-case on
messages. Versions are canonical `X.Y.Z`, max 32 characters, no leading zeros.
Service digest identifies the ARM64 OCI manifest, not a transport archive or
Cloud bundle. The VDP pair comes from the committed capability contract, not
the Cloud release or functional VDP_V3 label. Do not bake a Unit UID into a
reusable package. Input/CA/token replacement reconnects KUKSA; VDP pair change
aborts the old episode. UID/role/service identity change is rejected.

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
rejects modifications; it downloads nothing. Install prefix `/usr` packages
both executables and their actual dynamic-library closure. Missing product
dependencies fail the build, never substitute a diagnostic binary.

`tire_health_tests --emit-conformance` emits four synthetic message kinds for
offline validation against Tire Cloud's packaged schemas. This test binary is not
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
