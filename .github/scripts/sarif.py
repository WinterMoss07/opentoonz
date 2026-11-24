#!/usr/bin/env python3
# SARIF helpers for the static analysis workflows.
#   sarif.py msvc <build dir> <repo root> <out.sarif>  merge MSVC /analyze logs
#   sarif.py tidy <log> <repo root> <out.sarif>        convert clang-tidy output
import json
import os
import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlparse


def relative(path, root):
    if path.startswith("file:"):
        path = unquote(urlparse(path).path)
        if re.match(r"^/[A-Za-z]:", path):
            path = path[1:]
    path = os.path.normpath(path).replace("\\", "/")
    root = os.path.normpath(root).replace("\\", "/").rstrip("/") + "/"
    if path.lower().startswith(root.lower()):
        rel = path[len(root):]
        if not rel.startswith("toonz/build/"):
            return rel
    return None


def sarif(tool, rules, results):
    return {
        "version": "2.1.0",
        "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
        "runs": [{
            "tool": {"driver": {"name": tool, "rules": list(rules.values())}},
            "results": results,
        }],
    }


def msvc(build, root, out):
    rules, results, seen = {}, [], set()
    files = list(Path(build).rglob("*.sarif"))
    for f in files:
        try:
            data = json.loads(f.read_text(encoding="utf-8-sig"))
        except Exception:
            continue
        for run in data.get("runs", []):
            for r in run.get("tool", {}).get("driver", {}).get("rules", []):
                rules.setdefault(r["id"], {"id": r["id"], **{k: v for k, v in r.items() if k != "id"}})
            for res in run.get("results", []):
                locs = res.get("locations") or []
                if not locs:
                    continue
                phys = locs[0].get("physicalLocation", {})
                uri = phys.get("artifactLocation", {}).get("uri", "")
                rel = relative(uri, root)
                if rel is None:
                    continue
                line = phys.get("region", {}).get("startLine", 0)
                msg = res.get("message", {}).get("text", "")
                key = (res.get("ruleId"), rel, line, msg)
                if key in seen:
                    continue
                seen.add(key)
                rules.setdefault(res.get("ruleId"), {"id": res.get("ruleId")})
                results.append({
                    "ruleId": res.get("ruleId"),
                    "level": res.get("level", "warning"),
                    "message": {"text": msg},
                    "locations": [{"physicalLocation": {
                        "artifactLocation": {"uri": rel},
                        "region": {"startLine": max(1, line),
                                   "startColumn": max(1, phys.get("region", {}).get("startColumn", 1))},
                    }}],
                })
    Path(out).write_text(json.dumps(sarif("MSVC /analyze", rules, results)), encoding="utf-8")
    print(f"{len(files)} logs, {len(results)} results, {len(rules)} rules")
    if not files:
        sys.exit("no /analyze log found")


LINE = re.compile(r"^(.*?):(\d+):(\d+): (warning|error): (.*) \[([^\]]+)\]$")


def tidy(log, root, out):
    rules, results, seen = {}, [], set()
    for raw in Path(log).read_text(encoding="utf-8", errors="replace").splitlines():
        m = LINE.match(raw.strip())
        if not m:
            continue
        path, line, col, level, msg, rule = m.groups()
        rule = rule.split(",")[0]
        rel = relative(path, root)
        if rel is None:
            continue
        key = (rule, rel, line, msg)
        if key in seen:
            continue
        seen.add(key)
        rules.setdefault(rule, {"id": rule, "helpUri": "https://clang.llvm.org/extra/clang-tidy/checks/list.html"})
        results.append({
            "ruleId": rule,
            "level": "error" if level == "error" else "warning",
            "message": {"text": msg},
            "locations": [{"physicalLocation": {
                "artifactLocation": {"uri": rel},
                "region": {"startLine": int(line), "startColumn": int(col)},
            }}],
        })
    Path(out).write_text(json.dumps(sarif("clang-tidy", rules, results)), encoding="utf-8")
    print(f"{len(results)} results, {len(rules)} rules")


if __name__ == "__main__":
    {"msvc": msvc, "tidy": tidy}[sys.argv[1]](*sys.argv[2:5])
