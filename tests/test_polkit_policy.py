#!/usr/bin/env python3
"""Validate generated Polkit policy structure and unique action identifiers."""
import pathlib
import sys
import xml.etree.ElementTree as ET

path = pathlib.Path(sys.argv[1])
raw = path.read_text(encoding="utf-8")
if r"\n" in raw:
    raise SystemExit("policy contains literal backslash-n sequences")

root = ET.fromstring(raw)
if root.tag != "policyconfig":
    raise SystemExit("unexpected Polkit policy root element")

actions = root.findall("action")
ids = [action.get("id") for action in actions]
if any(not value for value in ids):
    raise SystemExit("every Polkit action must have an ID")
if len(ids) != len(set(ids)):
    duplicates = sorted(value for value in set(ids) if ids.count(value) > 1)
    raise SystemExit("duplicate Polkit action IDs: " + ", ".join(duplicates))

probe_id = "org.zapret2applet.blockcheck-recovery-probe"
probes = [action for action in actions if action.get("id") == probe_id]
if len(probes) != 1:
    raise SystemExit("Blockcheck recovery probe action must exist exactly once")
annotation = probes[0].find("annotate[@key='org.freedesktop.policykit.exec.argv1']")
if annotation is None or annotation.text != "blockcheck-recovery-probe":
    raise SystemExit("Blockcheck recovery probe must be restricted to its exact argv1")
defaults = probes[0].find("defaults")
if defaults is None or defaults.findtext("allow_active") != "yes":
    raise SystemExit("Blockcheck recovery probe must be non-interactively available to active sessions")
if defaults.findtext("allow_any") != "no" or defaults.findtext("allow_inactive") != "no":
    raise SystemExit("Blockcheck recovery probe must deny non-local and inactive sessions")

print(f"validated {len(actions)} unique Polkit actions")
