"""가독성 정리 후 기존 회귀 시험을 실행하고 이번 날짜에 결과를 보관한다."""
from pathlib import Path
import importlib.util
import json
import os
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).resolve().parent
RESULTS = []
POWERSHELL = Path.home() / ".cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe"
if "--resume" in sys.argv and (OUT / "regression.json").exists():
    RESULTS = [r for r in json.loads((OUT / "regression.json").read_text("utf-8")) if r["exit_code"] == 0]


def run(name, args):
    if any(r["name"] == name for r in RESULTS):
        print("[ALREADY PASSED] " + name, flush=True)
        return
    started = time.monotonic()
    environment = os.environ.copy()
    environment["PSModulePath"] = ";".join((
        str(POWERSHELL.parent / "Modules"),
        r"C:\Windows\System32\WindowsPowerShell\v1.0\Modules",
        r"C:\Program Files\WindowsPowerShell\Modules",
        environment.get("PSModulePath", "")))
    with (OUT / (name + ".log")).open("wb") as log:
        result = subprocess.run(args, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, env=environment,
                                creationflags=subprocess.CREATE_NO_WINDOW)
    entry = {"name": name, "exit_code": result.returncode,
             "seconds": round(time.monotonic() - started, 2), "log": name + ".log"}
    RESULTS.append(entry)
    (OUT / "regression.json").write_text(json.dumps(RESULTS, indent=2), encoding="utf-8")
    print(("[OK] " if result.returncode == 0 else "[FAIL] ") + name, flush=True)
    if result.returncode:
        raise RuntimeError(f"{name}: see {entry['log']}")


def ps(name, script, *args):
    run(name, [str(POWERSHELL), "-NoProfile", "-ExecutionPolicy", "Bypass",
               "-File", str(ROOT / script), *args])


def main():
    ps("build-debug", "build.ps1", "-Configuration", "Debug", "-RunChecks")
    ps("build-release", "build.ps1", "-Configuration", "Release", "-RunChecks")
    for mode in ("IOCP", "WSAPoll"):
        ps("network-debug-" + mode.lower(), "test-network.ps1",
           "-Configuration", "Debug", "-NetworkMode", mode)
    ps("parallel-release", "test-parallel.ps1", "-Configuration", "Release")
    ps("gameplay-wsapoll", "test-gameplay.ps1",
       "-Configuration", "Release", "-NetworkMode", "WSAPoll")
    for script in ("test-capacity.ps1", "test-auto-battle.ps1", "test-chat-relay.ps1",
                   "test-recovery.ps1", "test-ranking.ps1", "test-replay.ps1"):
        ps(script.removesuffix(".ps1"), script, "-Configuration", "Release")
    # 과거 성능 측정에 연결된 fault-release.json을 덮어쓰지 않는다.
    source = ROOT / "docs/results/2026-10-19/iocp/fault_check.py"
    spec = importlib.util.spec_from_file_location("iocp_fault", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    trials = []
    for pool, capacity in ((0, 8192), (1, 8192), (1, 16)):
        trials.append(module.trial(ROOT / "Run64/Release", pool, capacity))
        print(f"[OK] IOCP fault pool={pool}, capacity={capacity}", flush=True)
    (OUT / "iocp-fault.json").write_text(json.dumps(trials, indent=2), encoding="utf-8")
    print("[OK] Readability refactor regression complete", flush=True)


if __name__ == "__main__":
    main()
