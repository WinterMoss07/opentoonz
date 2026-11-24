#!/usr/bin/env python3
# Runs clang-tidy on every OpenToonz source file of compile_commands.json.
#   run_tidy.py <build dir> <path regex> <out.log>
import json
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

build, pattern, out = sys.argv[1:4]
regex = re.compile(pattern or ".*")
with open(os.path.join(build, "compile_commands.json"), encoding="utf-8") as f:
    entries = json.load(f)

db = os.path.join(build, "tidy-db")
os.makedirs(db, exist_ok=True)
for e in entries:
    for key in ("command", "arguments"):
        if key in e and key == "command":
            e[key] = re.sub(r"^\S*sccache(\.exe)?\s+", "", e[key])
        elif key in e:
            if e[key] and os.path.basename(e[key][0]).lower().startswith("sccache"):
                e[key] = e[key][1:]
with open(os.path.join(db, "compile_commands.json"), "w", encoding="utf-8") as f:
    json.dump(entries, f)

files = []
for e in entries:
    path = os.path.normpath(os.path.join(e["directory"], e["file"])).replace("\\", "/")
    name = os.path.basename(path)
    if "/toonz/sources/" not in path or "/toonz/build/" in path:
        continue
    if name.startswith(("moc_", "qrc_", "ui_")):
        continue
    if regex.search(path):
        files.append(path)
files = sorted(set(files))
print(f"{len(files)} files", flush=True)


def run(path):
    try:
        p = subprocess.run(["clang-tidy", "-p", db, "--quiet", path],
                           capture_output=True, text=True, encoding="utf-8",
                           errors="replace", timeout=900)
        return path, p.stdout
    except subprocess.TimeoutExpired:
        return path, f"{path}:1:1: warning: clang-tidy timed out [tidy-timeout]\n"


start = time.time()
done = 0
with open(out, "w", encoding="utf-8") as log, ThreadPoolExecutor(os.cpu_count() or 4) as pool:
    for path, text in pool.map(run, files):
        log.write(text)
        done += 1
        if done % 50 == 0 or done == len(files):
            print(f"{done}/{len(files)} in {int(time.time() - start)} s", flush=True)
