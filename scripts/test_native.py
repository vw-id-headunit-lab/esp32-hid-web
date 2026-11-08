#!/usr/bin/env python3
"""Test the encoder/sender; --api also tests the managed cJSON request parser."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--api", action="store_true", help="also test cJSON API (run idf.py build first)")
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="s3-hid-test-") as output:
    binary = str(Path(output) / "test_hid_text")
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    subprocess.run(compiler + [
        "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(root / "include"), str(root / "tests" / "test_hid_text.cpp"),
        "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)
    if args.api:
        cjson = root / "managed_components" / "espressif__cjson" / "cJSON"
        if not (cjson / "cJSON.c").is_file():
            raise SystemExit("cJSON is missing. Run idf.py reconfigure first.")
        obj = str(Path(output) / "cJSON.o")
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + flags + [
            "-c", str(cjson / "cJSON.c"), "-o", obj,
        ], check=True)
        api_binary = str(Path(output) / "test_api_json")
        subprocess.run(compiler + flags + [
            "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(root / "include"), "-I", str(cjson),
            str(root / "tests" / "test_api_json.cpp"), obj, "-o", api_binary,
        ], check=True)
        subprocess.run([api_binary], check=True)
