#!/usr/bin/env python3
"""Executor reproduzivel e retomavel da matriz experimental da ISSUE-13."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
from datetime import datetime, timezone


SCHEMA_VERSION = 1
SCENARIOS = ("equilibrado", "io_bound", "cpu_bound", "prioridades_desbalanceadas")
ALGORITHMS = ("fcfs", "rr", "prioridade", "proprio")
AGGREGATE_HEADER = (
    "schema_version", "run_id", "workload_sha256", "algorithm", "scenario", "seed",
    "process_count", "context_switch_cost", "rr_quantum", "makespan",
    "mean_turnaround", "context_switches", "jain_slowdown_pct", "status",
)


class ExperimentError(RuntimeError):
    pass


def now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_value(repo: Path, *arguments: str) -> str | None:
    completed = subprocess.run(
        ["git", *arguments], cwd=repo, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, check=False,
    )
    value = completed.stdout.strip()
    return value if completed.returncode == 0 and value else None


def atomic_json(path: Path, value: dict) -> None:
    temporary = path.with_name(f".{path.name}.tmp.{os.getpid()}")
    try:
        with temporary.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def read_config(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.count("=") != 1:
            raise ExperimentError(f"configuracao invalida na linha {number}")
        key, value = (part.strip() for part in line.split("=", 1))
        if key in values:
            raise ExperimentError(f"chave duplicada na configuracao: {key}")
        values[key] = value
    return values


def validate_hash(value: object) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def validate_result(path: Path, expected: dict, registered_hash: object) -> tuple[bool, str]:
    if expected.get("status") != "success":
        return False, "estado da execucao no manifesto nao e success"
    if not path.is_file():
        return False, "resultado ausente"
    actual_hash = sha256(path)
    if not validate_hash(registered_hash) or actual_hash != registered_hash:
        return False, "hash do resultado ausente ou divergente do manifesto"
    try:
        with path.open("r", encoding="utf-8", newline="") as stream:
            rows = list(csv.reader(stream))
    except (OSError, UnicodeError, csv.Error) as error:
        return False, f"resultado ilegivel: {error}"
    if len(rows) != 2:
        return False, "resultado deve conter cabecalho e exatamente uma linha"
    if tuple(rows[0]) != AGGREGATE_HEADER or len(rows[1]) != len(AGGREGATE_HEADER):
        return False, "schema agregado incompativel"
    row = dict(zip(AGGREGATE_HEADER, rows[1]))
    fields = {
        "schema_version": str(SCHEMA_VERSION), "run_id": expected["run_id"],
        "workload_sha256": expected["workload_sha256"], "algorithm": expected["algorithm"],
        "scenario": expected["scenario"], "seed": str(expected["seed"]),
        "process_count": str(expected["process_count"]),
        "context_switch_cost": str(expected["context_switch_cost"]),
        "rr_quantum": str(expected["rr_quantum"]), "status": "success",
    }
    for key, value in fields.items():
        if row.get(key) != value:
            return False, f"campo {key} incompativel"
    return True, "valido"


def workload_valid(root: Path, entry: dict) -> tuple[bool, str]:
    path = root / entry["path"]
    registered = entry.get("workload_sha256")
    if entry.get("status") != "success":
        return False, "estado do workload no manifesto nao e success"
    if not path.is_file():
        return False, "workload ausente"
    if not validate_hash(registered) or sha256(path) != registered:
        return False, "hash do workload ausente ou divergente"
    return True, "valido"


def identity(args: argparse.Namespace, repo: Path, binary: Path, config: Path,
             scenarios: tuple[str, ...], first: int, last: int, count: int) -> dict:
    config_values = read_config(config)
    effective = {
        "process_count": count, "context_switch_cost": 1, "rr_quantum": 4,
        "arrival_min": int(config_values.get("arrival_min", "0")),
        "arrival_max": int(config_values.get("arrival_max", "3")),
    }
    return {
        "git_commit": git_value(repo, "rev-parse", "HEAD") or "unknown",
        "git_tag": git_value(repo, "describe", "--tags", "--exact-match", "HEAD"),
        "binary_sha256": sha256(binary),
        "config": {
            "path": os.path.relpath(config, repo), "sha256": sha256(config),
            "effective": effective, "scenarios": list(scenarios),
            "seeds": {"first": first, "last": last}, "algorithms": list(ALGORITHMS),
        },
    }


def create_manifest(experiment_id: str, identity_value: dict, root: Path) -> dict:
    config = identity_value["config"]
    workloads = []
    runs = []
    effective = config["effective"]
    for scenario in config["scenarios"]:
        for seed in range(config["seeds"]["first"], config["seeds"]["last"] + 1):
            workloads.append({
                "scenario": scenario, "seed": seed,
                "path": f"workloads/{scenario}_seed_{seed}.csv",
                "workload_sha256": None, "status": "pending",
            })
            for algorithm in config["algorithms"]:
                runs.append({
                    "run_id": f"{scenario}-{algorithm}-seed-{seed}",
                    "scenario": scenario, "seed": seed, "algorithm": algorithm,
                    "workload_sha256": None,
                    "result_path": f"runs/{scenario}_{algorithm}_seed_{seed}.csv",
                    "result_sha256": None, "status": "pending", "attempts": 0,
                    "started_at": None, "finished_at": None, "command": None,
                    "failures": [],
                    "process_count": effective["process_count"],
                    "context_switch_cost": effective["context_switch_cost"],
                    "rr_quantum": effective["rr_quantum"],
                })
    return {
        "schema_version": SCHEMA_VERSION, "experiment_id": experiment_id,
        **identity_value, "started_at": now(), "finished_at": None,
        "workloads": workloads, "runs": runs,
        "summary": {"expected_workloads": len(workloads), "valid_workloads": 0,
                    "expected_runs": len(runs), "successful_runs": 0},
    }


def ensure_compatible(manifest: dict, experiment_id: str, expected: dict) -> None:
    if manifest.get("schema_version") != SCHEMA_VERSION or manifest.get("experiment_id") != experiment_id:
        raise ExperimentError("manifesto pertence a outro schema ou experimento")
    for key in ("git_commit", "git_tag", "binary_sha256", "config"):
        if manifest.get(key) != expected.get(key):
            raise ExperimentError(
                f"identidade incompativel ({key}); use outro --experiment-id para nao misturar experimentos"
            )
    config = expected["config"]
    workload_keys = [(item.get("scenario"), item.get("seed"))
                     for item in manifest.get("workloads", [])]
    run_keys = [(item.get("scenario"), item.get("seed"), item.get("algorithm"))
                for item in manifest.get("runs", [])]
    expected_workloads = [
        (scenario, seed) for scenario in config["scenarios"]
        for seed in range(config["seeds"]["first"], config["seeds"]["last"] + 1)
    ]
    expected_runs = [(scenario, seed, algorithm) for scenario, seed in expected_workloads
                     for algorithm in config["algorithms"]]
    if workload_keys != expected_workloads or len(set(workload_keys)) != len(workload_keys):
        raise ExperimentError("matriz de workloads ausente, duplicada ou fora de ordem no manifesto")
    if run_keys != expected_runs or len(set(run_keys)) != len(run_keys):
        raise ExperimentError("matriz de execucoes ausente, duplicada ou fora de ordem no manifesto")


def update_summary(manifest: dict, root: Path) -> tuple[int, int]:
    valid_workloads = sum(workload_valid(root, item)[0] for item in manifest["workloads"])
    valid_runs = 0
    for item in manifest["runs"]:
        expected = dict(item)
        if validate_result(root / item["result_path"], expected, item.get("result_sha256"))[0]:
            valid_runs += 1
    manifest["summary"].update(valid_workloads=valid_workloads, successful_runs=valid_runs)
    return valid_workloads, valid_runs


def execute(args: argparse.Namespace) -> int:
    repo = Path(__file__).resolve().parent.parent
    binary = (repo / args.binary).resolve() if not Path(args.binary).is_absolute() else Path(args.binary)
    config = (repo / args.config).resolve() if not Path(args.config).is_absolute() else Path(args.config)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise ExperimentError(f"executavel ausente ou sem permissao: {binary}")
    if not config.is_file():
        raise ExperimentError(f"configuracao ausente: {config}")

    scenarios = ("equilibrado",) if args.reduced else SCENARIOS
    first, last, count = (1, 2, 10) if args.reduced else (1, 100, 1000)
    root = (repo / args.results_dir / args.experiment_id).resolve()
    root.mkdir(parents=True, exist_ok=True)
    (root / "workloads").mkdir(exist_ok=True)
    (root / "runs").mkdir(exist_ok=True)
    manifest_path = root / "manifest.json"
    current_identity = identity(args, repo, binary, config, scenarios, first, last, count)

    if manifest_path.exists():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            raise ExperimentError(f"manifesto existente invalido: {error}") from error
        ensure_compatible(manifest, args.experiment_id, current_identity)
    else:
        manifest = create_manifest(args.experiment_id, current_identity, root)
        atomic_json(manifest_path, manifest)

    if args.verify_only:
        workloads, runs = update_summary(manifest, root)
        expected_w = manifest["summary"]["expected_workloads"]
        expected_r = manifest["summary"]["expected_runs"]
        print(f"Verificacao: {workloads}/{expected_w} workloads; {runs}/{expected_r} resultados validos")
        return 0 if (workloads, runs) == (expected_w, expected_r) else 1

    run_by_key = {(item["scenario"], item["seed"], item["algorithm"]): item
                  for item in manifest["runs"]}
    failures = 0
    for workload_entry in manifest["workloads"]:
        scenario, seed = workload_entry["scenario"], workload_entry["seed"]
        workload_path = root / workload_entry["path"]
        valid_workload, _ = workload_valid(root, workload_entry)
        for algorithm in ALGORITHMS:
            run = run_by_key[(scenario, seed, algorithm)]
            run["workload_sha256"] = workload_entry.get("workload_sha256")
            valid_result = False
            if valid_workload:
                valid_result, _ = validate_result(
                    root / run["result_path"], run, run.get("result_sha256")
                )
            if valid_result:
                run["status"] = "success"
                continue

            command = [str(binary), "--config", str(config), "--scenario", scenario,
                       "--seed", str(seed), "--processes", str(count),
                       "--context-switch-cost", "1", "--rr-quantum", "4",
                       "--algorithm", algorithm, "--run-id", run["run_id"]]
            if not valid_workload:
                if algorithm != "fcfs":
                    continue
                command += ["--workload-output", str(workload_path)]
            else:
                command += ["--workload-input", str(workload_path)]
            command += ["--output", str(root / run["result_path"])]
            run["attempts"] += 1
            run["started_at"] = now()
            run["finished_at"] = None
            run["command"] = shlex.join(command)
            run["status"] = "running"
            atomic_json(manifest_path, manifest)
            completed = subprocess.run(command, cwd=repo, text=True, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, check=False)
            run["finished_at"] = now()
            if completed.returncode != 0:
                message = (completed.stderr or completed.stdout or "falha sem mensagem").strip()
                run["status"] = "failed"
                run["failures"].append({"attempt": run["attempts"], "at": now(),
                                        "exit_code": completed.returncode, "message": message[-4000:]})
                failures += 1
                atomic_json(manifest_path, manifest)
                break

            if not valid_workload:
                if not workload_path.is_file():
                    run["status"] = "failed"
                    run["failures"].append({"attempt": run["attempts"], "at": now(),
                                            "exit_code": 0, "message": "workload nao foi publicado"})
                    failures += 1
                    atomic_json(manifest_path, manifest)
                    break
                workload_entry["workload_sha256"] = sha256(workload_path)
                workload_entry["status"] = "success"
                valid_workload = True
                run["workload_sha256"] = workload_entry["workload_sha256"]

            result_path = root / run["result_path"]
            run["result_sha256"] = sha256(result_path) if result_path.is_file() else None
            run["status"] = "success"
            valid_result, reason = validate_result(result_path, run, run["result_sha256"])
            if not valid_result:
                run["status"] = "failed"
                run["failures"].append({"attempt": run["attempts"], "at": now(),
                                        "exit_code": 0, "message": reason})
                failures += 1
            else:
                run["status"] = "success"
            atomic_json(manifest_path, manifest)

    workloads, runs = update_summary(manifest, root)
    expected_w = manifest["summary"]["expected_workloads"]
    expected_r = manifest["summary"]["expected_runs"]
    complete = workloads == expected_w and runs == expected_r
    manifest["finished_at"] = now() if complete else None
    atomic_json(manifest_path, manifest)
    print(f"Experimento {args.experiment_id}: {workloads}/{expected_w} workloads; "
          f"{runs}/{expected_r} resultados validos")
    return 0 if complete and failures == 0 else 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiment-id", required=True,
                        help="identificador unico, usado como diretorio em results/raw")
    parser.add_argument("--config", default="configs/default.conf")
    parser.add_argument("--binary", default="bin/simulador")
    parser.add_argument("--results-dir", default="results/raw")
    parser.add_argument("--reduced", action="store_true",
                        help="executa 1 cenario x 2 seeds x 4 algoritmos x 10 processos")
    parser.add_argument("--verify-only", action="store_true",
                        help="valida completude sem executar nem alterar artefatos")
    parsed = parser.parse_args()
    if (not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", parsed.experiment_id)
            or parsed.experiment_id in (".", "..")):
        parser.error("--experiment-id deve usar apenas letras, numeros, ponto, hifen e sublinhado")
    return parsed


if __name__ == "__main__":
    try:
        sys.exit(execute(parse_args()))
    except (ExperimentError, OSError, ValueError) as error:
        print(f"Erro: {error}", file=sys.stderr)
        sys.exit(2)
