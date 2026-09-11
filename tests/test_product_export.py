# SPDX-FileCopyrightText: 2026 maninblack
# SPDX-License-Identifier: Apache-2.0

"""Isolated exporter/recipe tests; no Docker or product runtime execution."""

from __future__ import annotations

import importlib.util
import hashlib
import json
import os
import re
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("product_export", ROOT / "tools/build_product.py")
assert SPEC is not None and SPEC.loader is not None
EXPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORT)


class ProductExportTests(unittest.TestCase):
    def test_tire_only_product_identity(self) -> None:
        self.assertEqual(("tire-health-bootstrap", "tire-health-service"), EXPORT.PRODUCT_BINARIES)
        self.assertEqual("tire-health-linux-arm64-product", EXPORT.PRODUCT_KIND)
        self.assertFalse(hasattr(EXPORT, "build_scaffold"))

    def test_rejects_profile_and_missing_source_provenance_before_io(self) -> None:
        for revision, epoch, profile in (("a" * 40, 1, "v2"), ("a" * 40, 1, "v3"),
                                         ("a" * 40, 1, ""), ("a" * 39, 1, "v1"),
                                         ("A" * 40, 1, "v1"), ("a" * 40, 0, "v1")):
            with self.subTest(revision=revision, epoch=epoch, profile=profile), \
                    mock.patch.object(EXPORT, "inspect_product_elf") as inspect:
                with self.assertRaises(EXPORT.ProductExportError):
                    EXPORT.export_runtime(Path("unused"), Path("unused-output"), revision,
                                          epoch, Path("unused"), Path("unused.xml"), profile)
                inspect.assert_not_called()

    def test_refuses_existing_output_and_rootfs_child_before_inspection(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime = root / "rootfs"
            runtime.mkdir()
            for output in (root, runtime, runtime / "export"):
                with self.subTest(output=output), mock.patch.object(EXPORT, "inspect_product_elf") as inspect:
                    with self.assertRaises(EXPORT.ProductExportError):
                        EXPORT.export_runtime(runtime, output, "a" * 40, 1, root, root / "ctest.xml")
                    inspect.assert_not_called()

    def test_rejects_links_nonexecutables_and_missing_binary(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            ordinary = root / "ordinary"
            ordinary.write_bytes(b"not an executable")
            symlink = root / "symlink"
            symlink.symlink_to(ordinary)
            hardlink = root / "hardlink"
            os.link(ordinary, hardlink)
            for path in (ordinary, symlink, hardlink, root / "missing", root):
                with self.subTest(path=path), mock.patch.object(EXPORT, "_readelf") as readelf:
                    with self.assertRaises(EXPORT.ProductExportError):
                        EXPORT.inspect_product_elf(path)
                    readelf.assert_not_called()

    def test_rejects_diagnostic_stub_before_readelf(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "tire-health-service"
            path.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            path.chmod(0o755)
            with mock.patch.object(EXPORT, "_readelf") as readelf:
                with self.assertRaises(EXPORT.ProductExportError):
                    EXPORT.inspect_product_elf(path)
                readelf.assert_not_called()

    def test_elf_shape_and_dynamic_closure_fail_closed(self) -> None:
        # Deliberately synthetic parser fixture: never exported or executed.
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "tire-health-service"
            header = bytearray(64)
            header[:7] = b"\x7fELF\x02\x01\x01"
            struct.pack_into("<HHI", header, 16, 3, 183, 1)
            path.write_bytes(header)
            path.chmod(0o755)
            results = {
                "--program-headers": "[Requesting program interpreter: /lib/ld-linux-aarch64.so.1]",
                "--dynamic": "(NEEDED) Shared library: [libc.so.6]\n(NEEDED) Shared library: [libm.so.6]",
                "--version-info": "Name: GLIBC_2.17\nName: GLIBC_2.34",
            }
            with mock.patch.object(EXPORT, "_readelf", side_effect=lambda _path, option: results[option]):
                observed = EXPORT.inspect_product_elf(path)
                self.assertEqual("rootfs/usr/bin/tire-health-service", observed["path"])
                self.assertEqual(["libc.so.6", "libm.so.6"], observed["needed"])
                self.assertEqual(64, observed["size"])
                self.assertEqual(hashlib.sha256(header).hexdigest(), observed["sha256"])
                original_interpreter = results["--program-headers"]
                for interpreter in ("", "[Requesting program interpreter: /lib/ld-musl-aarch64.so.1]",
                                    original_interpreter + original_interpreter):
                    results["--program-headers"] = interpreter
                    with self.assertRaises(EXPORT.ProductExportError):
                        EXPORT.inspect_product_elf(path)
                results["--program-headers"] = original_interpreter
                results["--dynamic"] += "\n(NEEDED) Shared library: [libgrpc++.so.1]"
                with self.assertRaises(EXPORT.ProductExportError):
                    EXPORT.inspect_product_elf(path)
                results["--dynamic"] = "(NEEDED) Shared library: [libc.so.6]\n(RUNPATH) Library runpath: [/opt/bhs]"
                with self.assertRaises(EXPORT.ProductExportError):
                    EXPORT.inspect_product_elf(path)
                results["--dynamic"] = "(NEEDED) Shared library: [libc.so.6]"
                results["--version-info"] = "Name: GLIBC_2.38"
                with self.assertRaises(EXPORT.ProductExportError):
                    EXPORT.inspect_product_elf(path)
                struct.pack_into("<H", header, 18, 62)
                path.write_bytes(header)
                with self.assertRaises(EXPORT.ProductExportError):
                    EXPORT.inspect_product_elf(path)

    def test_readelf_failure_is_not_substituted_with_declared_architecture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "tire-health-service"
            header = bytearray(64)
            header[:7] = b"\x7fELF\x02\x01\x01"
            struct.pack_into("<HHI", header, 16, 3, 183, 1)
            path.write_bytes(header)
            path.chmod(0o755)
            with mock.patch.object(EXPORT, "_readelf", side_effect=FileNotFoundError):
                with self.assertRaises(FileNotFoundError):
                    EXPORT.inspect_product_elf(path)

    def test_requires_actual_complete_successful_ctest_report(self) -> None:
        names = ("native_service_inputs", "tire_private_token_session", "tire_health_contract", "tire_demo_mock_isolation")
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "report.xml"
            body = '<testsuite tests="4" failures="0">' + "".join(
                f'<testcase name="{name}" />' for name in names) + "</testsuite>"
            path.write_text(body, encoding="utf-8")
            self.assertEqual("passed", EXPORT.inspect_test_report(path)["ctest"])
            self.assertEqual(4, EXPORT.inspect_test_report(path)["count"])
            self.assertEqual(hashlib.sha256(body.encode()).hexdigest(), EXPORT.inspect_test_report(path)["reportSha256"])
            for invalid in (
                body.replace('name="tire_health_contract"', 'name="brake_health_contract"'),
                body.replace('</testsuite>', '<testcase name="tire_health_contract" /></testsuite>'),
                body.replace('name="tire_health_contract" />', 'name="tire_health_contract"><failure /></testcase>'),
                body.replace('name="tire_health_contract" />', 'name="tire_health_contract"><error /></testcase>'),
                body.replace('name="tire_health_contract" />', 'name="tire_health_contract"><skipped /></testcase>'),
                body.replace('failures="0"', 'failures="0" errors="1"'),
                '<testsuite tests="0" failures="0" />',
            ):
                path.write_text(invalid, encoding="utf-8")
                with self.assertRaises(EXPORT.ProductExportError):
                    EXPORT.inspect_test_report(path)
            path.write_text(body.replace('failures="0"', 'failures="1"'), encoding="utf-8")
            with self.assertRaises(EXPORT.ProductExportError):
                EXPORT.inspect_test_report(path)

    def test_report_rejects_links_and_unbounded_content(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = root / "report.xml"
            report.write_bytes(b"x" * (1024 * 1024 + 1))
            link = root / "link.xml"
            link.symlink_to(report)
            for path in (link, report, root / "missing"):
                with self.assertRaises(EXPORT.ProductExportError):
                    EXPORT.inspect_test_report(path)

    def test_dependency_revision_mismatch_does_not_create_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with mock.patch.object(EXPORT, "inspect_product_elf", return_value={}), \
                    mock.patch.object(EXPORT, "inspect_test_report", return_value={}), \
                    mock.patch.object(EXPORT.subprocess, "run", return_value=mock.Mock(stdout="wrong")):
                with self.assertRaisesRegex(EXPORT.ProductExportError, "dependency source revision mismatch"):
                    EXPORT.export_runtime(root / "runtime", root / "output", "a" * 40,
                                          1, root / "sources", root / "report.xml")
            self.assertFalse((root / "output").exists())

    def test_archive_digest_mismatch_does_not_create_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            revisions = [mock.Mock(stdout=value) for value in EXPORT.DEPENDENCY_REVISIONS.values()]
            with mock.patch.object(EXPORT, "inspect_product_elf", return_value={}), \
                    mock.patch.object(EXPORT, "inspect_test_report", return_value={}), \
                    mock.patch.object(EXPORT.subprocess, "run", side_effect=revisions), \
                    mock.patch.object(EXPORT, "_sha256", return_value="wrong"):
                with self.assertRaisesRegex(EXPORT.ProductExportError, "OpenSSL archive digest mismatch"):
                    EXPORT.export_runtime(root / "runtime", root / "output", "a" * 40,
                                          1, root / "sources", root / "report.xml")
            self.assertFalse((root / "output").exists())

    def test_product_recipe_pins_real_target_and_export(self) -> None:
        dockerfile = (ROOT / "Dockerfile").read_text(encoding="utf-8")
        self.assertIn("debian:bookworm-slim@sha256:6bd27d44e6c32a66bbd72d7cb2b76a8ae3497ec2e5274a81abd1b37f6013fa1f", dockerfile)
        self.assertIn("20260901T000000Z", dockerfile)
        for name in ("grpc", "protobuf", "abseil", "kuksa"):
            self.assertIn(EXPORT.DEPENDENCY_REVISIONS[name], dockerfile)
        self.assertIn(EXPORT.OPENSSL_ARCHIVE_SHA256, dockerfile)
        self.assertIn("-DTHS_BUILD_KUKSA_RUNTIME=ON", dockerfile)
        self.assertIn("ARG THS_FUNCTIONAL_PROFILE=v1", dockerfile)
        self.assertIn('-DTHS_FUNCTIONAL_PROFILE="${THS_FUNCTIONAL_PROFILE}"', dockerfile)
        self.assertIn("python3 -m unittest discover -s tests -p test_product_export.py", dockerfile)
        self.assertIn("python3 tools/build_product.py", dockerfile)
        self.assertIn("--output-junit /build/service/ctest-results.xml", dockerfile)
        self.assertIn("--runtime-root /build/rootfs", dockerfile)
        self.assertIn("FROM scratch AS export", dockerfile)
        self.assertNotIn("src/usr/bin/tire-health-service", dockerfile)
        self.assertNotIn("chmod 777", dockerfile)
        self.assertNotIn("--insecure", dockerfile)
        self.assertNotIn("ENTRYPOINT", dockerfile)

    def test_cmake_freezes_profile_and_required_suites(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('if(NOT THS_FUNCTIONAL_PROFILE STREQUAL "v1")', cmake)
        self.assertIn("message(FATAL_ERROR", cmake)
        self.assertIn("target_compile_options(tire_health_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)", cmake)
        self.assertEqual({"native_service_inputs", "tire_private_token_session", "tire_health_contract", "tire_demo_mock_isolation"},
                         set(re.findall(r"add_test\(NAME\s+(\w+)", cmake)))
        runtime = (ROOT / "cmake/KuksaRuntime.cmake").read_text(encoding="utf-8")
        self.assertIn("install(TARGETS tire-health-service tire-health-bootstrap RUNTIME DESTINATION bin)", runtime)

    def test_dependency_inventory_matches_verified_pins(self) -> None:
        inventory = json.loads((ROOT / "DEPENDENCIES.json").read_text(encoding="utf-8"))
        pins = {item["revision"] for category in ("runtime", "build") for item in inventory[category]}
        for name in ("abseil", "protobuf", "grpc", "kuksa"):
            self.assertIn(EXPORT.DEPENDENCY_REVISIONS[name], pins)
        self.assertIn("archive-sha256:" + EXPORT.OPENSSL_ARCHIVE_SHA256, pins)
        self.assertEqual("linux/arm64", inventory["containerBuild"]["architecture"])

    def test_build_context_is_allowlisted_without_runtime_or_private_inputs(self) -> None:
        rules = [line.strip() for line in (ROOT / ".dockerignore").read_text().splitlines()
                 if line.strip() and not line.startswith("#")]
        self.assertEqual("*", rules[0])
        for rule in ("!CMakeLists.txt", "!tools/build_product.py", "!tests/*.cpp", "!DEPENDENCIES.json"):
            self.assertIn(rule, rules)
        for rule in ("**/*.pem", "**/*.key", "**/*.jwt", "**/*.p12", "**/service-release.json", "**/metadata.json"):
            self.assertIn(rule, rules)
        for disallowed in ("!.git/", "!.local/", "!build/", "!packaging/", "!config/"):
            self.assertNotIn(disallowed, rules)


if __name__ == "__main__":
    unittest.main()
