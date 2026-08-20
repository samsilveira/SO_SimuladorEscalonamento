#!/usr/bin/env python3
"""Valida, consolida e representa o experimento principal com IC95%."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile
from typing import Iterable


SCHEMA_VERSION = 1
SCENARIOS = ("equilibrado", "io_bound", "cpu_bound", "prioridades_desbalanceadas")
ALGORITHMS = ("fcfs", "rr", "prioridade", "proprio", "sjf")
SEEDS = tuple(range(1, 1001))
PROCESS_COUNT = 1000
CONTEXT_SWITCH_COST = 1
RR_QUANTUM = 4
# Primeiro commit que implementa o algoritmo proprio PDBH. Um lote anterior a
# esta revisao mede outra politica sob o nome "proprio" e nao e comparavel.
PDBH_BASELINE_COMMIT = "af5f5ede29eed56532bfc3c08fa9534102e10378"
AGGREGATE_HEADER = (
    "schema_version", "run_id", "workload_sha256", "algorithm", "scenario", "seed",
    "process_count", "context_switch_cost", "rr_quantum", "makespan",
    "mean_turnaround", "context_switches", "jain_slowdown_pct", "status",
)
SUMMARY_HEADER = (
    "algorithm", "scenario", "metric", "n", "mean", "std",
    "ci95_low", "ci95_high", "unit",
)
METRICS = {
    "mean_turnaround": {
        "unit": "ticks",
        "label": "Turnaround médio (ticks)",
        "title": "Turnaround médio por algoritmo e cenário",
        "filename": "mean_turnaround_ic95.png",
    },
    "context_switches": {
        "unit": "trocas",
        "label": "Trocas de contexto (quantidade)",
        "title": "Trocas de contexto por algoritmo e cenário",
        "filename": "context_switches_ic95.png",
    },
    "jain_slowdown_pct": {
        "unit": "%",
        "label": "Índice de Jain do slowdown (%)",
        "title": "Justiça de Jain por algoritmo e cenário",
        "filename": "jain_slowdown_pct_ic95.png",
    },
}
SCENARIO_LABELS = {
    "equilibrado": "Equilibrado",
    "io_bound": "I/O-bound",
    "cpu_bound": "CPU-bound",
    "prioridades_desbalanceadas": "Prioridades\ndesbalanceadas",
}
ALGORITHM_LABELS = {
    "fcfs": "FCFS",
    "rr": "RR",
    "prioridade": "Prioridade",
    "proprio": "PDBH (próprio)",
    "sjf": "SJF",
}


class AnalysisError(RuntimeError):
    """Erro de integridade que impede a consolidacao."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def valid_hash(value: object) -> bool:
    return isinstance(value, str) and bool(re.fullmatch(r"[0-9a-f]{64}", value))


def require_integer(value: object, field: str, *, minimum: int = 0) -> int:
    if not isinstance(value, str) or not re.fullmatch(r"[0-9]+", value):
        raise AnalysisError(f"{field} deve ser um inteiro decimal")
    parsed = int(value)
    if parsed < minimum:
        raise AnalysisError(f"{field} possui valor impossivel: {value}")
    return parsed


def require_finite(value: object, field: str) -> float:
    try:
        parsed = float(value)
    except (TypeError, ValueError) as error:
        raise AnalysisError(f"{field} nao e numerico: {value!r}") from error
    if not math.isfinite(parsed):
        raise AnalysisError(f"{field} deve ser finito; NaN e infinito nao sao aceitos")
    return parsed


def read_single_result(path: Path, expected: dict[str, object], registered_hash: object) -> dict:
    if not path.is_file():
        raise AnalysisError(f"resultado ausente: {path}")
    if not valid_hash(registered_hash) or sha256(path) != registered_hash:
        raise AnalysisError(f"hash ausente ou divergente: {path}")
    try:
        with path.open("r", encoding="utf-8", newline="") as stream:
            rows = list(csv.reader(stream, strict=True))
    except (OSError, UnicodeError, csv.Error) as error:
        raise AnalysisError(f"resultado ilegivel {path}: {error}") from error
    if len(rows) != 2:
        raise AnalysisError(f"{path} deve conter cabecalho e exatamente uma observacao")
    if tuple(rows[0]) != AGGREGATE_HEADER or len(rows[1]) != len(AGGREGATE_HEADER):
        raise AnalysisError(f"schema agregado incompativel: {path}")

    row = dict(zip(AGGREGATE_HEADER, rows[1]))
    identity = {
        "schema_version": str(SCHEMA_VERSION),
        "run_id": expected["run_id"],
        "workload_sha256": expected["workload_sha256"],
        "algorithm": expected["algorithm"],
        "scenario": expected["scenario"],
        "seed": str(expected["seed"]),
        "process_count": str(PROCESS_COUNT),
        "context_switch_cost": str(CONTEXT_SWITCH_COST),
        "rr_quantum": str(RR_QUANTUM),
        "status": "success",
    }
    for field, value in identity.items():
        if row.get(field) != value:
            raise AnalysisError(f"campo {field} incompativel em {path}")

    makespan = require_integer(row["makespan"], "makespan", minimum=1)
    turnaround = require_finite(row["mean_turnaround"], "mean_turnaround")
    switches = require_integer(row["context_switches"], "context_switches")
    jain = require_finite(row["jain_slowdown_pct"], "jain_slowdown_pct")
    if not 0.0 < turnaround <= makespan:
        raise AnalysisError(f"mean_turnaround possui valor impossivel em {path}")
    if switches * CONTEXT_SWITCH_COST > makespan:
        raise AnalysisError(f"context_switches possui valor impossivel em {path}")
    if not 0.0 < jain <= 100.0:
        raise AnalysisError(f"jain_slowdown_pct deve estar no intervalo (0, 100] em {path}")

    return {
        "scenario": row["scenario"],
        "algorithm": row["algorithm"],
        "seed": int(row["seed"]),
        "mean_turnaround": turnaround,
        "context_switches": float(switches),
        "jain_slowdown_pct": jain,
    }


def canonical_workloads() -> list[dict[str, object]]:
    return [
        {
            "scenario": scenario,
            "seed": seed,
            "path": f"workloads/{scenario}_seed_{seed}.csv",
        }
        for scenario in SCENARIOS
        for seed in SEEDS
    ]


def canonical_runs() -> list[dict[str, object]]:
    return [
        {
            "run_id": f"{scenario}-{algorithm}-seed-{seed}",
            "scenario": scenario,
            "algorithm": algorithm,
            "seed": seed,
            "result_path": f"runs/{scenario}_{algorithm}_seed_{seed}.csv",
        }
        for scenario in SCENARIOS
        for seed in SEEDS
        for algorithm in ALGORITHMS
    ]


def load_manifest(experiment_dir: Path) -> dict:
    path = experiment_dir / "manifest.json"
    if not path.is_file():
        raise AnalysisError(f"manifesto ausente: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise AnalysisError(f"manifesto invalido: {error}") from error
    if not isinstance(value, dict):
        raise AnalysisError("manifesto deve ser um objeto JSON")
    return value


def validate_git_provenance(manifest: dict, repo: Path) -> None:
    commit = manifest.get("git_commit")
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise AnalysisError("git_commit ausente ou invalido no manifesto")
    completed = subprocess.run(
        ["git", "merge-base", "--is-ancestor", PDBH_BASELINE_COMMIT, commit],
        cwd=repo, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        text=True, check=False,
    )
    if completed.returncode == 1:
        raise AnalysisError(
            "lote anterior a implementacao do PDBH; reexecute o experimento principal"
        )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or "historico Git indisponivel"
        raise AnalysisError(f"nao foi possivel validar a proveniencia Git: {detail}")


def validate_manifest_contract(manifest: dict, repo: Path) -> None:
    config = manifest.get("config")
    effective = config.get("effective") if isinstance(config, dict) else None
    required_config = (
        isinstance(config, dict)
        and config.get("scenarios") == list(SCENARIOS)
        and config.get("algorithms") == list(ALGORITHMS)
        and config.get("seeds") == {"first": SEEDS[0], "last": SEEDS[-1]}
        and isinstance(effective, dict)
        and effective.get("process_count") == PROCESS_COUNT
        and effective.get("context_switch_cost") == CONTEXT_SWITCH_COST
        and effective.get("rr_quantum") == RR_QUANTUM
    )
    if manifest.get("schema_version") != SCHEMA_VERSION or not required_config:
        raise AnalysisError("manifesto nao representa a matriz experimental principal canonica")
    validate_git_provenance(manifest, repo)
    expected_summary = {
        "expected_workloads": len(SCENARIOS) * len(SEEDS),
        "valid_workloads": len(SCENARIOS) * len(SEEDS),
        "expected_runs": len(SCENARIOS) * len(SEEDS) * len(ALGORITHMS),
        "successful_runs": len(SCENARIOS) * len(SEEDS) * len(ALGORITHMS),
    }
    if manifest.get("summary") != expected_summary or not manifest.get("finished_at"):
        raise AnalysisError("experimento principal nao esta completo no manifesto")


def load_observations(experiment_dir: Path, repo: Path) -> list[dict[str, object]]:
    manifest = load_manifest(experiment_dir)
    validate_manifest_contract(manifest, repo)

    workloads = manifest.get("workloads")
    expected_workloads = canonical_workloads()
    if not isinstance(workloads, list) or len(workloads) != len(expected_workloads):
        raise AnalysisError("ausencia ou duplicata na matriz de workloads")
    workload_hashes: dict[tuple[str, int], str] = {}
    for index, (actual, expected) in enumerate(zip(workloads, expected_workloads)):
        if not isinstance(actual, dict):
            raise AnalysisError(f"workload {index} invalido no manifesto")
        for field, value in expected.items():
            if actual.get(field) != value:
                raise AnalysisError(f"workload {index} possui campo {field} incompativel")
        registered_hash = actual.get("workload_sha256")
        path = experiment_dir / str(expected["path"])
        if actual.get("status") != "success" or not path.is_file():
            raise AnalysisError(f"workload ausente ou sem sucesso: {path}")
        if not valid_hash(registered_hash) or sha256(path) != registered_hash:
            raise AnalysisError(f"hash do workload ausente ou divergente: {path}")
        key = (str(expected["scenario"]), int(expected["seed"]))
        if key in workload_hashes:
            raise AnalysisError(f"workload duplicado para cenario/seed: {key}")
        workload_hashes[key] = str(registered_hash)

    runs = manifest.get("runs")
    expected_runs = canonical_runs()
    if not isinstance(runs, list) or len(runs) != len(expected_runs):
        raise AnalysisError("ausencia ou duplicata na matriz de execucoes")
    observations: list[dict[str, object]] = []
    observed_keys: set[tuple[str, str, int]] = set()
    for index, (actual, canonical) in enumerate(zip(runs, expected_runs)):
        if not isinstance(actual, dict):
            raise AnalysisError(f"execucao {index} invalida no manifesto")
        for field, value in canonical.items():
            if actual.get(field) != value:
                raise AnalysisError(f"execucao {index} possui campo {field} incompativel")
        key = (str(canonical["scenario"]), str(canonical["algorithm"]), int(canonical["seed"]))
        if key in observed_keys:
            raise AnalysisError(f"observacao duplicada: {key}")
        observed_keys.add(key)
        workload_hash = workload_hashes[(key[0], key[2])]
        if actual.get("status") != "success" or actual.get("workload_sha256") != workload_hash:
            raise AnalysisError(f"execucao sem sucesso ou com workload divergente: {canonical['run_id']}")
        expected = {**canonical, "workload_sha256": workload_hash}
        observation = read_single_result(
            experiment_dir / str(canonical["result_path"]), expected,
            actual.get("result_sha256"),
        )
        observations.append(observation)

    validate_observation_matrix(observations)
    return observations


def validate_observation_matrix(
    observations: Iterable[dict[str, object]],
    scenarios: tuple[str, ...] = SCENARIOS,
    algorithms: tuple[str, ...] = ALGORITHMS,
    seeds: tuple[int, ...] = SEEDS,
) -> None:
    groups: dict[tuple[str, str], list[int]] = {
        (scenario, algorithm): [] for scenario in scenarios for algorithm in algorithms
    }
    seen: set[tuple[str, str, int]] = set()
    for item in observations:
        key = (str(item["scenario"]), str(item["algorithm"]))
        seed = int(item["seed"])
        full_key = (*key, seed)
        if key not in groups:
            raise AnalysisError(f"combinacao inesperada: {key}")
        if full_key in seen:
            raise AnalysisError(f"observacao duplicada: {full_key}")
        seen.add(full_key)
        groups[key].append(seed)
    for key, actual_seeds in groups.items():
        if sorted(actual_seeds) != list(seeds):
            raise AnalysisError(
                f"{key} deve possuir exatamente {len(seeds)} observacoes, uma por seed"
            )


def sample_statistics(values: Iterable[float]) -> tuple[int, float, float, float, float]:
    data = list(values)
    if len(data) < 2 or any(not math.isfinite(value) for value in data):
        raise AnalysisError("o IC95% exige ao menos duas observacoes finitas")
    mean = statistics.fmean(data)
    std = statistics.stdev(data)
    margin = 1.96 * std / math.sqrt(len(data))
    return len(data), mean, std, mean - margin, mean + margin


def consolidate(observations: list[dict[str, object]]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for scenario in SCENARIOS:
        for algorithm in ALGORITHMS:
            group = [
                item for item in observations
                if item["scenario"] == scenario and item["algorithm"] == algorithm
            ]
            for metric, metadata in METRICS.items():
                n, mean, std, low, high = sample_statistics(
                    float(item[metric]) for item in group
                )
                rows.append({
                    "algorithm": algorithm,
                    "scenario": scenario,
                    "metric": metric,
                    "n": n,
                    "mean": mean,
                    "std": std,
                    "ci95_low": low,
                    "ci95_high": high,
                    "unit": metadata["unit"],
                })
    return rows


def write_summary(path: Path, rows: list[dict[str, object]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=SUMMARY_HEADER, lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow({
                **row,
                "mean": format(float(row["mean"]), ".17g"),
                "std": format(float(row["std"]), ".17g"),
                "ci95_low": format(float(row["ci95_low"]), ".17g"),
                "ci95_high": format(float(row["ci95_high"]), ".17g"),
            })


def render_figures(rows: list[dict[str, object]], temporary_paths: dict[str, Path]) -> None:
    cache = Path(tempfile.gettempdir()) / f"simulador-matplotlib-{os.getuid()}"
    cache.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(cache))
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError as error:
        raise AnalysisError(
            "Matplotlib ausente; instale requirements-analysis.txt antes de gerar as figuras"
        ) from error

    hatches = ("", "///", "xxx", "...", "\\\\")
    colors = ("#f2f2f2", "#c7c7c7", "#969696", "#525252", "#2e2e2e")
    group_centers = list(range(len(SCENARIOS)))
    width = 0.16
    offsets = tuple((index - (len(ALGORITHMS) - 1) / 2) * width for index in range(len(ALGORITHMS)))

    for metric, metadata in METRICS.items():
        figure, axis = plt.subplots(figsize=(11, 6.5), constrained_layout=True)
        for index, algorithm in enumerate(ALGORITHMS):
            selected = [
                next(
                    row for row in rows
                    if row["metric"] == metric
                    and row["scenario"] == scenario
                    and row["algorithm"] == algorithm
                )
                for scenario in SCENARIOS
            ]
            means = [float(row["mean"]) for row in selected]
            margins = [float(row["ci95_high"]) - float(row["mean"]) for row in selected]
            positions = [center + offsets[index] for center in group_centers]
            axis.bar(
                positions, means, width, yerr=margins, capsize=4,
                color=colors[index], edgecolor="black", linewidth=0.9,
                hatch=hatches[index], label=ALGORITHM_LABELS[algorithm],
                error_kw={"ecolor": "black", "elinewidth": 1.1, "capthick": 1.1},
            )
        axis.set_title(str(metadata["title"]), fontweight="bold")
        axis.set_ylabel(str(metadata["label"]))
        axis.set_xticks(group_centers, [SCENARIO_LABELS[item] for item in SCENARIOS])
        axis.grid(axis="y", color="#d9d9d9", linewidth=0.7)
        axis.set_axisbelow(True)
        axis.legend(
            title="Algoritmo", loc="upper left", bbox_to_anchor=(1.01, 1.0),
            ncols=1, frameon=True, borderaxespad=0,
        )
        axis.text(
            0.5, -0.15, "Barras: média; hastes: IC95% = média ± 1,96 × s/√n; n = 1000",
            transform=axis.transAxes, ha="center", va="top", fontsize=9,
        )
        figure.savefig(
            temporary_paths[metric], format="png", dpi=300,
            metadata={"Title": str(metadata["title"]), "Software": "Matplotlib"},
        )
        plt.close(figure)


def publish_outputs(
    rows: list[dict[str, object]], summary_output: Path, figures_dir: Path,
) -> list[Path]:
    summary_output.parent.mkdir(parents=True, exist_ok=True)
    figures_dir.mkdir(parents=True, exist_ok=True)
    token = f"{os.getpid()}"
    summary_temporary = summary_output.with_name(f".{summary_output.name}.tmp.{token}")
    figure_temporaries = {
        metric: figures_dir / f".{metadata['filename']}.tmp.{token}"
        for metric, metadata in METRICS.items()
    }
    staged = [summary_temporary, *figure_temporaries.values()]
    targets = [
        summary_output,
        *(figures_dir / str(metadata["filename"]) for metadata in METRICS.values()),
    ]
    backups: dict[Path, Path] = {}
    published: list[Path] = []
    try:
        write_summary(summary_temporary, rows)
        render_figures(rows, figure_temporaries)
        for target in targets:
            if target.exists():
                backup = target.with_name(f".{target.name}.bak.{token}")
                os.replace(target, backup)
                backups[target] = backup
        for temporary, target in zip(staged, targets):
            os.replace(temporary, target)
            published.append(target)
    except Exception:
        for target in published:
            try:
                target.unlink()
            except FileNotFoundError:
                pass
        for target, backup in backups.items():
            if backup.exists():
                os.replace(backup, target)
        raise
    finally:
        for path in [*staged, *backups.values()]:
            try:
                path.unlink()
            except FileNotFoundError:
                pass
    return targets


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiment-dir", default="results/raw/main")
    parser.add_argument("--summary-output", default="results/consolidated/summary.csv")
    parser.add_argument("--figures-dir", default="results/figures")
    return parser.parse_args()


def execute(args: argparse.Namespace) -> list[Path]:
    repo = Path(__file__).resolve().parent.parent
    experiment_dir = (repo / args.experiment_dir).resolve()
    summary_output = (repo / args.summary_output).resolve()
    figures_dir = (repo / args.figures_dir).resolve()
    observations = load_observations(experiment_dir, repo)
    rows = consolidate(observations)
    return publish_outputs(rows, summary_output, figures_dir)


if __name__ == "__main__":
    try:
        generated = execute(parse_args())
        print("Consolidacao concluida: 20000 observacoes validas, 60 linhas estatisticas.")
        for generated_path in generated:
            print(generated_path)
    except (AnalysisError, OSError, ValueError) as error:
        print(f"Erro: {error}", file=sys.stderr)
        sys.exit(2)
