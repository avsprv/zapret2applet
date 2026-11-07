#!/usr/bin/env python3
"""Strict Blockcheck option schema and upstream process environment.

This module intentionally does not manage systemd or service recovery yet.
It is the validation boundary shared by the future submit and worker commands.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
import re
from typing import Any

SCHEMA_VERSION = 1
MAX_OPTIONS_SIZE = 4096
_ALLOWED_IPVS = frozenset({"4", "6", "46"})
_ALLOWED_SCAN_LEVELS = frozenset({"veryquick", "quick", "standard", "force"})
_DOMAIN_LABEL = re.compile(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\\Z")


class OptionsError(ValueError):
    """Raised when Blockcheck options are malformed or unsafe."""


@dataclass(frozen=True)
class BlockcheckOptions:
    domains: tuple[str, ...] = ("rutracker.org",)
    ipvs: str = "4"
    http: bool = True
    tls12: bool = True
    tls13: bool = False
    http3: bool = False
    repeats: int = 1
    scan_level: str = "standard"

    def __post_init__(self) -> None:
        if not isinstance(self.domains, tuple) or not 1 <= len(self.domains) <= 10:
            raise OptionsError("domains must contain between 1 and 10 entries")
        for domain in self.domains:
            _validate_domain(domain)
        if self.ipvs not in _ALLOWED_IPVS:
            raise OptionsError("ipvs must be 4, 6, or 46")
        for name in ("http", "tls12", "tls13", "http3"):
            if type(getattr(self, name)) is not bool:
                raise OptionsError(f"{name} must be a boolean")
        if type(self.repeats) is not int or not 1 <= self.repeats <= 10:
            raise OptionsError("repeats must be an integer from 1 to 10")
        if self.scan_level not in _ALLOWED_SCAN_LEVELS:
            raise OptionsError("unsupported scan level")

    def to_json(self) -> bytes:
        payload = {
            "version": SCHEMA_VERSION,
            "domains": list(self.domains),
            "ipvs": self.ipvs,
            "http": self.http,
            "tls12": self.tls12,
            "tls13": self.tls13,
            "http3": self.http3,
            "repeats": self.repeats,
            "scan_level": self.scan_level,
        }
        raw = (json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\\n").encode("ascii")
        if len(raw) > MAX_OPTIONS_SIZE:
            raise OptionsError("options exceed size limit")
        return raw

    @classmethod
    def from_json(cls, raw: bytes) -> "BlockcheckOptions":
        if not isinstance(raw, bytes) or not raw or len(raw) > MAX_OPTIONS_SIZE:
            raise OptionsError("options file is empty or exceeds size limit")
        try:
            text = raw.decode("ascii")
        except UnicodeDecodeError as exc:
            raise OptionsError("options must use ASCII") from exc

        def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
            result: dict[str, Any] = {}
            for key, value in pairs:
                if key in result:
                    raise ValueError(f"duplicate JSON key: {key}")
                result[key] = value
            return result

        try:
            value = json.loads(text, object_pairs_hook=reject_duplicate_keys)
        except (json.JSONDecodeError, ValueError) as exc:
            raise OptionsError(f"malformed options JSON: {exc}") from exc

        expected = {
            "version", "domains", "ipvs", "http", "tls12", "tls13",
            "http3", "repeats", "scan_level",
        }
        if (not isinstance(value, dict) or set(value) != expected
                or type(value["version"]) is not int
                or value["version"] != SCHEMA_VERSION):
            raise OptionsError("unsupported options schema")

        domains = value["domains"]
        if not isinstance(domains, list) or any(not isinstance(item, str) for item in domains):
            raise OptionsError("domains must be a JSON string array")
        try:
            return cls(
                domains=tuple(domains),
                ipvs=value["ipvs"],
                http=value["http"],
                tls12=value["tls12"],
                tls13=value["tls13"],
                http3=value["http3"],
                repeats=value["repeats"],
                scan_level=value["scan_level"],
            )
        except (TypeError, OptionsError) as exc:
            if isinstance(exc, OptionsError):
                raise
            raise OptionsError(f"invalid option types: {exc}") from exc

    def upstream_environment(self) -> dict[str, str]:
        """Return a complete allowlisted environment; never inherit caller state."""
        return {
            "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
            "HOME": "/root",
            "LANG": "C.UTF-8",
            "BATCH": "1",
            "DOMAINS": " ".join(self.domains),
            "IPVS": self.ipvs,
            "ENABLE_HTTP": "1" if self.http else "0",
            "ENABLE_HTTPS_TLS12": "1" if self.tls12 else "0",
            "ENABLE_HTTPS_TLS13": "1" if self.tls13 else "0",
            "ENABLE_HTTP3": "1" if self.http3 else "0",
            "REPEATS": str(self.repeats),
            "SCANLEVEL": self.scan_level,
        }


def _validate_domain(domain: str) -> None:
    if not isinstance(domain, str) or not domain or len(domain) > 254:
        raise OptionsError("domain is empty or too long")
    candidate = domain[:-1] if domain.endswith(".") else domain
    if not candidate or len(candidate) > 253:
        raise OptionsError("invalid domain length")
    labels = candidate.split(".")
    if any(not label or len(label) > 63 or not _DOMAIN_LABEL.fullmatch(label)
           for label in labels):
        raise OptionsError(f"invalid domain name: {domain!r}")
