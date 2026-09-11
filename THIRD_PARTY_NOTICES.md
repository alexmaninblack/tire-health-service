<!-- SPDX-FileCopyrightText: 2026 maninblack -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Third-party notices

The runtime primitives identified in `NOTICE` were adapted from Brake Health
Service under Apache-2.0. The product Docker recipe, exporter and parser-test
structure were adapted from the same repository's accepted product-build
pattern, with Tire executable names, exactly three required CTest suites,
fixed v1 profile and no diagnostic scaffold mode. Tire product logic, identity,
storage, resources and backend remain independent.

`DEPENDENCIES.json` identifies the external pinned product dependencies.
gRPC and its code generator are Apache-2.0 with transitive BSD-3-Clause,
MPL-2.0, MIT and Zlib material. Protobuf is BSD-3-Clause; Abseil, OpenSSL and
the KUKSA VAL schemas are Apache-2.0. C++ bindings are generated from the
external pinned schemas during product compilation, not vendored in source.

The product exporter retains public upstream LICENSE, NOTICE, COPYING,
COPYRIGHT and AUTHORS files by relative source path for gRPC (including its
pinned submodules), Protobuf, Abseil, KUKSA and OpenSSL. GCC runtime copyright
and exception notices accompany statically linked libstdc++ and libgcc.
The final package must retain these notices. The exporter must reject absent
upstream notices instead of treating this summary as their replacement.

No upstream binary, library, compiler, certificate or private credential is
vendored here. AosEdge, COVESA VSS and CARLA are architecture/compatibility
references; their source is not included in this Tire product export.
Actual artifact/link/license closure is verified at export; source tests do
not establish a completed ARM64 artifact or live qualification.
