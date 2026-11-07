#!/usr/bin/env python3
"""Unit tests for strict Blockcheck option validation and serialization."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "packaging" / "helper"))
from blockcheck_manager import BlockcheckOptions, OptionsError


class BlockcheckOptionsTests(unittest.TestCase):
    def test_defaults_round_trip(self):
        options = BlockcheckOptions()
        restored = BlockcheckOptions.from_json(options.to_json())
        self.assertEqual(restored, options)

    def test_all_upstream_environment_values_are_explicit(self):
        options = BlockcheckOptions(
            domains=("example.org", "rutracker.org"), ipvs="46",
            http=False, tls12=True, tls13=True, http3=False,
            repeats=3, scan_level="quick",
        )
        self.assertEqual(options.upstream_environment(), {
            "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
            "HOME": "/root",
            "LANG": "C.UTF-8",
            "BATCH": "1",
            "DOMAINS": "example.org rutracker.org",
            "IPVS": "46",
            "ENABLE_HTTP": "0",
            "ENABLE_HTTPS_TLS12": "1",
            "ENABLE_HTTPS_TLS13": "1",
            "ENABLE_HTTP3": "0",
            "REPEATS": "3",
            "SCANLEVEL": "quick",
        })

    def test_domain_validation_rejects_shell_syntax(self):
        for domain in ("", ".example.org", "example..org", "-bad.org",
                       "bad-.org", "example.org;id", "$(id)", "a" * 64 + ".org"):
            with self.subTest(domain=domain):
                with self.assertRaises(OptionsError):
                    BlockcheckOptions(domains=(domain,))

    def test_domain_count_and_option_ranges(self):
        with self.assertRaises(OptionsError):
            BlockcheckOptions(domains=())
        with self.assertRaises(OptionsError):
            BlockcheckOptions(domains=tuple(f"host{i}.example" for i in range(11)))
        for kwargs in (
            {"ipvs": "8"}, {"repeats": 0}, {"repeats": 11},
            {"scan_level": "unknown"}, {"http": 1},
        ):
            with self.subTest(kwargs=kwargs):
                with self.assertRaises(OptionsError):
                    BlockcheckOptions(**kwargs)

    def test_duplicate_json_keys_rejected(self):
        raw = (b'{"version":1,"domains":["example.org"],"ipvs":"4",'
               b'"http":true,"tls12":true,"tls13":false,"http3":false,'
               b'"repeats":1,"scan_level":"standard","http":false}')
        with self.assertRaisesRegex(OptionsError, "duplicate JSON key"):
            BlockcheckOptions.from_json(raw)

    def test_unknown_missing_and_wrong_version_fields_rejected(self):
        valid = BlockcheckOptions().to_json().decode("ascii").strip()
        import json
        value = json.loads(valid)
        value["unexpected"] = True
        with self.assertRaises(OptionsError):
            BlockcheckOptions.from_json(json.dumps(value).encode("ascii"))
        del value["unexpected"]
        del value["http3"]
        with self.assertRaises(OptionsError):
            BlockcheckOptions.from_json(json.dumps(value).encode("ascii"))
        value["http3"] = False
        value["version"] = 2
        with self.assertRaises(OptionsError):
            BlockcheckOptions.from_json(json.dumps(value).encode("ascii"))

    def test_invalid_json_encoding_and_size_rejected(self):
        for raw in (b"", bytes([255]), b"{" + b" " * 4096):
            with self.subTest(raw_length=len(raw)):
                with self.assertRaises(OptionsError):
                    BlockcheckOptions.from_json(raw)


if __name__ == "__main__":
    unittest.main(verbosity=2)
