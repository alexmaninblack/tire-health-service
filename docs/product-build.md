<!-- SPDX-FileCopyrightText: 2026 maninblack -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Linux ARM64 product export contract

The root `Dockerfile` is a preparation-only build recipe, not a runnable
service container or a replacement lifecycle mechanism. Demo Control owns
actual builds and exports. Native Aos container creation, launch, retained
assignments and recovery remain unchanged.

The recipe builds the real `tire-health-bootstrap` and `tire-health-service`
with `THS_BUILD_KUKSA_RUNTIME=ON`. There is no diagnostic scaffold mode or
substitute executable. Tire supports exactly functional profile `v1`; this
is separate from the release version later allocated during package
preparation. CMake and the exporter reject any other content profile.

## Demo Control adapter

The existing `democtl service build tire --content-profile v1` action resolves
a clean committed Tire checkout, its exact source revision and positive
commit timestamp. It supplies this argument vector without shell expansion:

```text
docker buildx build
  --platform linux/arm64
  --pull=false
  --file <tire-health-service>/Dockerfile
  --target export
  --build-arg SOURCE_REVISION=<40-character source commit>
  --build-arg SOURCE_DATE_EPOCH=<positive commit Unix timestamp>
  --build-arg THS_FUNCTIONAL_PROFILE=v1
  --output type=local,dest=<owned-output-directory>
  <tire-health-service>
```

This describes the adapter contract, not permission to invoke Docker directly.
`BUILD_JOBS` defaults to 4 and accepts only 1 through 8. The output must be
new and owned by this build attempt. Failed or partial output is retained for
reconciliation; it must not be merged into an accepted candidate. Demo Control
retains its separate `build.json` receipt and catalogs source/profile pairs.
Neither source revision nor timestamp has a fabricated fallback.

The `.dockerignore` allowlist includes only required build source, tests,
exporter and public notices. Git, local state, build artifacts, package release
metadata, public runtime metadata and private credential files are excluded.
The recipe runs the isolated exporter/recipe tests before service compilation,
then all five CTest suites before installation/export. Contract-test
assertions stay enabled even in Release builds. The test executable is
not installed or included in the exported rootfs.

## Pinned inputs and warm dependency reuse

Dependency stages intentionally match the accepted Brake recipe byte-for-byte,
including its internal `/opt/bhs` toolchain install prefix. That prefix is a
build-cache detail, not a shared runtime path, service identity or resource.
The Tire source/product stage and exported payload are independent.

`DEPENDENCIES.json` records the same frozen Debian Linux ARM64 base digest,
`20260901T000000Z` package snapshots, gRPC 1.60.1, Protobuf 25.8.0, Abseil
20240116.3, KUKSA 0.5.0 and OpenSSL 3.2.6 inputs. The Docker recipe pins Git
commits and the OpenSSL archive hash. The exporter checks actual source pins,
including all selected gRPC submodules, and the actual archive digest. CMake
also checks the KUKSA schema commit and exact VAL content hashes before
generation. No dependency download occurs inside CMake.

The initial dependency preparation can require network access; subsequent
source changes reuse the accepted warm layers. This does not authorize
network work during deployment. No Builder/cache cleanup is part of export.
Frozen inputs do not themselves prove bit-for-bit reproducibility; actual
CTest timing evidence can differ between builds.

## Executable and license closure

gRPC, Protobuf, Abseil, OpenSSL, c-ares, RE2, zlib, libstdc++ and libgcc are
linked statically. Both product executables must be executable regular files,
not symlinks, hardlinks, scripts or host binaries. The exporter checks ELF64
little-endian AArch64 headers and `readelf` evidence: interpreter
`/lib/ld-linux-aarch64.so.1`, only `libc.so.6`, `libm.so.6` and
`ld-linux-aarch64.so.1` in `DT_NEEDED`, no RPATH/RUNPATH, and no GLIBC symbol
newer than 2.36. Recorded digests and sizes are calculated from those files,
never inferred from names or replaced with a placeholder.

Public dependency LICENSE/NOTICE/COPYING/COPYRIGHT/AUTHORS files retain their
source-relative paths. GCC runtime copyright and exception notices accompany
static libstdc++/libgcc. The runtime export includes only the two executables
and public notices, with no credential, CA, package release, Unit identity,
runtime metadata, model state, outbox or deployment configuration.

The guest loader/glibc boundary, actual resource fit and complete runtime
behavior remain separate qualification gates. No guest libraries are replaced
and no Factory rebuild is implied by this recipe.

## Exact output

```text
output/
  product-build.json
  evidence/ctest-results.xml
  rootfs/usr/bin/tire-health-bootstrap
  rootfs/usr/bin/tire-health-service
  rootfs/usr/share/licenses/tire-health-service/...
```

`tools/build_product.py` is an internal export library invoked by this recipe,
not another operational wrapper. `product-build.json` uses schema version 1,
kind `tire-health-linux-arm64-product`, architecture `arm64`, OS `linux`,
product target `THS_BUILD_KUKSA_RUNTIME=ON` and functional profile `v1`.
It contains the source revision/timestamp; two binary path, SHA-256, size,
interpreter, dependency and GLIBC records; verified dependency revisions;
compiler runtime package versions; base image and snapshot references.

Its `tests` section must say `ctest: passed`, `count: 5`, identify
`evidence/ctest-results.xml` and contain that report's SHA-256. Required suite
names are exactly `native_service_inputs`, `tire_private_token_session`,
`tire_health_contract`, `tire_demo_mock_isolation` and
`function_observation_delivery`; missing, duplicate, failed or skipped cases refuse
export. The manifest always states `liveQualified: false`.

Demo Control consumes only the allowed rootfs leaves during package
preparation, adds its single allocated package/publication version and the
accepted Tire service configuration, then owns signing/publication. Product
compilation is neither publication nor functional acceptance. The operator
approved the exact raw-feature extraction formulas on 16 September 2026;
the runtime now applies that model rather than `MODEL_CONTRACT_UNRESOLVED`.
This source closure does not qualify live analytics, calibration, readiness
or resource usage. Actual ARM64 compilation, KAC/TLS,
subscriptions, renewal, native mounts, recovery and backend records require
their separately controlled build and live gates.
