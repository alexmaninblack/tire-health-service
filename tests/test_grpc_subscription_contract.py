# SPDX-FileCopyrightText: 2026 maninblack
# SPDX-License-Identifier: Apache-2.0
"""Pinned VAL request regression; this does not qualify the raw Tire model."""
import unittest
from pathlib import Path


class SubscribeContractTests(unittest.TestCase):
    def test_telemetry_and_status_request_value_fields(self):
        source = (Path(__file__).resolve().parents[1] / "src/runtime/grpc_main.cpp").read_text()
        request = source.split("val::SubscribeRequest subscription;", 1)[1].split("auto reader =", 1)[0]
        self.assertEqual(2, request.count("entry->add_fields(val::FIELD_VALUE)"))
        self.assertIn("entry->set_path(path)", request)
        self.assertIn("entry->set_path(status_path)", request)
        self.assertNotIn("FIELD_ACTUATOR_TARGET", request)


if __name__ == "__main__":
    unittest.main()
