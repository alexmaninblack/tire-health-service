<!-- SPDX-FileCopyrightText: 2026 maninblack -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Tire Health Service

Independent in-vehicle Tire Health QM service. Its model, persistent outbox and advisory are separate from Brake. It does not control vehicle motion.

<a id="sdv-lab-entry"></a>

For the **whole demo**, start at the [SDV Lab README](https://github.com/alexmaninblack/aosedge-sdv-demo).
Only the product repository is manually cloned for its pinned multi-component
build. The instructions below are for working on **this component alone**;
a host check does not publish, install or qualify a vehicle package.

## 1 Prepare a macOS component workspace

Use native Apple Silicon Terminal. These component commands are for development,
not a qualified full-demo installation. Run blocks in order and stop on error.
The revised instructions await the joint walkthrough; they were not executed
during this documentation update.

Choose an already mounted external APFS SSD:

```sh
uname -m
printf 'Mounted external APFS volume (for example /Volumes/BUILD): '
read -r SDV_VOLUME
diskutil info "$SDV_VOLUME"
df -h "$SDV_VOLUME"
```

Expect `arm64` and the actual external volume. Do not create a missing mount
directory. After confirming storage:

```sh
SDV_WORK="$SDV_VOLUME/sdv-components"
mkdir -p "$SDV_WORK" "$SDV_VOLUME/tmp"
export TMPDIR="$SDV_VOLUME/tmp"
export HOMEBREW_CACHE="$SDV_WORK/cache/homebrew"
```

Install Apple's Command Line Tools with `xcode-select --install` if missing,
and finish the system dialog. Install [Homebrew](https://docs.brew.sh/Installation)
if absent. Then:

```sh
eval "$(/opt/homebrew/bin/brew shellenv)"
brew install cmake python@3.12
export PATH="$(brew --prefix python@3.12)/libexec/bin:$PATH"
git --version
cmake --version
python3 --version
xcrun clang++ --version
```

Do not use the installed demo's private interpreter or a Rosetta toolchain.

## 2 Clone this component

```sh
git clone --branch main https://github.com/alexmaninblack/tire-health-service.git "$SDV_WORK/tire-health-service"
cd "$SDV_WORK/tire-health-service"
git rev-parse HEAD
```

Record the printed revision with your results. `main` is current development,
not a release pin. To reproduce the complete candidate, use the product
repository's manifest-driven route instead of independently choosing branches.

## 3 Build and check host targets

```sh
SDV_BUILD="$SDV_WORK/build/tire-health-service"
cmake -S . -B "$SDV_BUILD" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DTHS_BUILD_KUKSA_RUNTIME=OFF
cmake --build "$SDV_BUILD" --parallel 2
ctest --test-dir "$SDV_BUILD" --output-on-failure
```

Expect a successful build and no failed CTest cases. Keep the first failure;
do not replace expected results or lower resource/security requirements.

This C++17 build is **host/domain/bootstrap only**, not the real Linux ARM64
KUKSA executable. The configuration reports that limitation. There are no
simulator, Cloud or Docker prerequisites for this host build.

## 4 Produce the vehicle package

Use the product repository's pinned build chain and the
[product build contract](docs/product-build.md). It supplies Linux ARM64,
gRPC 1.60.1, Protobuf 25.8.0 and the exact KUKSA source expected by the recipe;
turning on `THS_BUILD_KUKSA_RUNTIME` alone is insufficient.

Tire has one functional profile, V1; release numbers can change independently.
Demo Control owns build/export, signing and publication. Do not package the
host test executable, manufacture a KUKSA token or use Brake's state/resources.

## 5 Finish

CTest exits when complete and leaves no vehicle service running. Preserve the
recorded source revision and results. A host cleanup is not permission to reset
a deployed model or delete its durable outbox.

## Component documentation

- [Product build](docs/product-build.md)
- [Advisory integration](docs/advisory-demo-control.md)
- [License](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md)

## Runtime reference

Tire V1 performs local condition analysis and driver advisory using authorized
vehicle inputs. It is an illustrative QM service, not a vehicle-control
function or production diagnostic algorithm. Backend delivery is independent
of local operation.

Use the [product build](docs/product-build.md) for its real Linux ARM64
executable and export requirements. The integration owner signs, publishes
and assigns it separately from Brake. Functional profile V1 is not an
instruction to reuse a previous Cloud release number.

## Security and license

Keep service-private credentials, certificates, tokens, model state and
operational data out of Git. Original work is Apache-2.0 under `maninblack`;
see [LICENSE](LICENSE), [NOTICE](NOTICE) and
[third-party notices](THIRD_PARTY_NOTICES.md).
