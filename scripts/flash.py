#!/usr/bin/env python3
"""
ESP-AgriNet Zigbee - Flashing helper
=====================================
Calls idf.py flash for the given target with sensible defaults.

Usage:
    python3 scripts/flash.py gateway    /dev/ttyUSB0
    python3 scripts/flash.py sensor     /dev/ttyUSB1
    python3 scripts/flash.py actuator   /dev/ttyUSB2
    python3 scripts/flash.py rcp        /dev/ttyUSB0
"""
import os
import sys
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR   = os.path.dirname(SCRIPT_DIR)

TARGET_DIR = {
    "gateway":   os.path.join(ROOT_DIR, "gateway", "host"),
    "sensor":    os.path.join(ROOT_DIR, "nodes", "sensor_node"),
    "actuator":  os.path.join(ROOT_DIR, "nodes", "actuator_node"),
    "rcp":       None,  # special-handled below
}

def flash_target(name: str, port: str) -> int:
    if name not in TARGET_DIR:
        print(f"Unknown target: {name}")
        print(f"Valid targets: {list(TARGET_DIR.keys())}")
        return 1
    if name == "rcp":
        idf_path = os.environ.get("IDF_PATH", "")
        target_dir = os.path.join(idf_path, "examples", "openthread", "ot_rcp")
        if not os.path.isdir(target_dir):
            print("ot_rcp example not found. Make sure ESP-IDF is installed and IDF_PATH is set.")
            return 1
    else:
        target_dir = TARGET_DIR[name]

    print(f"==> Flashing {name} from {target_dir} on {port}")
    return subprocess.call([
        "idf.py", "-p", port, "flash", "monitor"
    ], cwd=target_dir)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python3 flash.py <target> <port>")
        sys.exit(2)
    sys.exit(flash_target(sys.argv[1], sys.argv[2]))
