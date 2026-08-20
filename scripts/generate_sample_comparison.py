#!/usr/bin/env python3
"""Gera graficos e tabelas comparativas entre N=100 e N=1000 execucoes."""

from __future__ import annotations

import csv
import math
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np

REPO = Path(__file__).resolve().parent.parent
RAW_DIR = REPO / "results" / "raw" / "main"
FIGURES_DIR = REPO / "results" / "figures"
CONSOLIDATED_DIR = REPO / "results" / "consolidated"

SCENARIOS = ("equilibrado", "io_bound", "cpu_bound", "prioridades_desbalanceadas")
ALGORITHMS = ("fcfs", "rr", "prioridade", "proprio", "sjf")
ALGORITHM_LABELS = {
    "fcfs": "FCFS",
    "rr": "Round Robin",
    "prioridade": "Prioridade",
    "proprio": "PDBH (Próprio)",
    "sjf": "SJF",
}
ALGORITHM_COLORS = {
    "fcfs": "#2b5c8f",
    "rr": "#d95f02",
    "prioridade": "#7570b3",
    "proprio": "#1b9e77",
    "sjf": "#e7298a",
}
SCENARIO_TITLES = {
    "equilibrado": "Equilibrado",
    "io_bound": "I/O-Bound",
    "cpu_bound": "CPU-Bound",
    "prioridades_desbalanceadas": "Desbalanceado",
}


def load_all_runs() -> dict[tuple[str, str], list[dict]]:
    runs_by_pair: dict[tuple[str, str], list[dict]] = {}
    runs_dir = RAW_DIR / "runs"
    for scenario in SCENARIOS:
        for alg in ALGORITHMS:
            runs_by_pair[(scenario, alg)] = []
            for seed in range(1, 1001):
                path = runs_dir / f"{scenario}_{alg}_seed_{seed}.csv"
                if not path.is_file():
                    continue
                with path.open(encoding="utf-8") as f:
                    reader = csv.DictReader(f)
                    row = next(reader)
                    runs_by_pair[(scenario, alg)].append({
                        "seed": seed,
                        "mean_turnaround": float(row["mean_turnaround"]),
                        "context_switches": float(row["context_switches"]),
                        "jain_slowdown_pct": float(row["jain_slowdown_pct"]),
                    })
    return runs_by_pair


def compute_stats(values: list[float]) -> tuple[float, float, float, float]:
    n = len(values)
    mean = sum(values) / n
    variance = sum((x - mean) ** 2 for x in values) / (n - 1) if n > 1 else 0.0
    std = math.sqrt(variance)
    margin = 1.96 * std / math.sqrt(n) if n > 0 else 0.0
    return mean, std, mean - margin, mean + margin


def generate_comparison_csv(runs_by_pair: dict[tuple[str, str], list[dict]]) -> None:
    output_path = CONSOLIDATED_DIR / "summary_comparison_100_vs_1000.csv"
    with output_path.open("w", encoding="utf-8", newline="\n") as f:
        writer = csv.writer(f)
        writer.writerow([
            "scenario", "algorithm", "metric",
            "mean_100", "ic95_margin_100",
            "mean_1000", "ic95_margin_1000",
            "error_reduction_pct"
        ])
        for scenario in SCENARIOS:
            for alg in ALGORITHMS:
                runs = runs_by_pair[(scenario, alg)]
                for metric in ("mean_turnaround", "context_switches", "jain_slowdown_pct"):
                    v100 = [r[metric] for r in runs if r["seed"] <= 100]
                    v1000 = [r[metric] for r in runs if r["seed"] <= 1000]
                    m100, _, low100, high100 = compute_stats(v100)
                    m1000, _, low1000, high1000 = compute_stats(v1000)
                    margin100 = high100 - m100
                    margin1000 = high1000 - m1000
                    reduct = ((margin100 - margin1000) / margin100 * 100) if margin100 > 0 else 0.0
                    writer.writerow([
                        scenario, alg, metric,
                        f"{m100:.2f}", f"{margin100:.2f}",
                        f"{m1000:.2f}", f"{margin1000:.2f}",
                        f"{reduct:.1f}%"
                    ])
    print(f"Resumo comparativo salvo em: {output_path}")


def plot_side_by_side(runs_by_pair: dict[tuple[str, str], list[dict]]) -> None:
    FIGURES_DIR.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(2, 2, figsize=(14, 10), dpi=300)
    axes = axes.flatten()

    for idx, scenario in enumerate(SCENARIOS):
        ax = axes[idx]
        x = np.arange(len(ALGORITHMS))
        width = 0.35

        means_100, errs_100 = [], []
        means_1000, errs_1000 = [], []

        for alg in ALGORITHMS:
            runs = runs_by_pair[(scenario, alg)]
            v100 = [r["mean_turnaround"] for r in runs if r["seed"] <= 100]
            v1000 = [r["mean_turnaround"] for r in runs if r["seed"] <= 1000]
            m100, _, l100, h100 = compute_stats(v100)
            m1000, _, l1000, h1000 = compute_stats(v1000)
            means_100.append(m100)
            errs_100.append(h100 - m100)
            means_1000.append(m1000)
            errs_1000.append(h1000 - m1000)

        bars1 = ax.bar(x - width/2, means_100, width, yerr=errs_100, capsize=4,
                       label="N = 100", color="#80b1d3", edgecolor="#333333", alpha=0.9)
        bars2 = ax.bar(x + width/2, means_1000, width, yerr=errs_1000, capsize=4,
                       label="N = 1000", color="#386cb0", edgecolor="#333333", alpha=0.9)

        ax.set_title(f"Cenário: {SCENARIO_TITLES[scenario]}", fontsize=12, fontweight="bold")
        ax.set_ylabel("Turnaround Médio (ticks)", fontsize=10)
        ax.set_xticks(x)
        ax.set_xticklabels([ALGORITHM_LABELS[a] for a in ALGORITHMS], fontsize=9)
        ax.grid(axis="y", linestyle="--", alpha=0.5)
        ax.legend(loc="upper left")

    plt.suptitle("Comparativo de Estabilidade e Margem de Erro: N = 100 vs N = 1000 (Turnaround)", fontsize=14, fontweight="bold", y=0.98)
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    out = FIGURES_DIR / "comparativo_barras_100_vs_1000_turnaround.png"
    plt.savefig(out, dpi=300)
    plt.close()
    print(f"Gráfico salvo em: {out}")


def plot_convergence(runs_by_pair: dict[tuple[str, str], list[dict]]) -> None:
    sample_points = [10, 25, 50, 100, 200, 350, 500, 750, 1000]
    fig, axes = plt.subplots(2, 2, figsize=(14, 10), dpi=300)
    axes = axes.flatten()

    for idx, scenario in enumerate(SCENARIOS):
        ax = axes[idx]
        for alg in ALGORITHMS:
            runs = runs_by_pair[(scenario, alg)]
            means, low_bounds, high_bounds = [], [], []
            for n_sub in sample_points:
                vals = [r["mean_turnaround"] for r in runs if r["seed"] <= n_sub]
                m, _, low, high = compute_stats(vals)
                means.append(m)
                low_bounds.append(low)
                high_bounds.append(high)

            color = ALGORITHM_COLORS[alg]
            label = ALGORITHM_LABELS[alg]
            ax.plot(sample_points, means, marker="o", markersize=3, label=label, color=color, linewidth=1.5)
            ax.fill_between(sample_points, low_bounds, high_bounds, color=color, alpha=0.15)

        ax.set_title(f"Cenário: {SCENARIO_TITLES[scenario]}", fontsize=12, fontweight="bold")
        ax.set_xlabel("Tamanho da Amostra (N sementes)", fontsize=10)
        ax.set_ylabel("Turnaround Médio (ticks)", fontsize=10)
        ax.grid(True, linestyle="--", alpha=0.5)
        ax.legend(loc="upper right", fontsize=8)

    plt.suptitle("Convergência Assintótica e Estreitamento do IC95% (N = 10 até N = 1000)", fontsize=14, fontweight="bold", y=0.98)
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    out = FIGURES_DIR / "comparativo_convergencia_ic95.png"
    plt.savefig(out, dpi=300)
    plt.close()
    print(f"Gráfico salvo em: {out}")


def main() -> None:
    runs_by_pair = load_all_runs()
    generate_comparison_csv(runs_by_pair)
    plot_side_by_side(runs_by_pair)
    plot_convergence(runs_by_pair)
    print("Processamento comparativo 100 x 1000 finalizado com sucesso!")


if __name__ == "__main__":
    main()
