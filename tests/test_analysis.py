#!/usr/bin/env python3
"""Regressoes da consolidacao estatistica e das figuras da ISSUE-14."""

import csv
import hashlib
import json
import math
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parent.parent
ANALYZER = REPO / "scripts" / "analyze_experiment.py"
API = runpy.run_path(str(ANALYZER))


class StatisticsTests(unittest.TestCase):
    def test_known_sample_uses_sample_standard_deviation_and_ic95(self):
        n, mean, std, low, high = API["sample_statistics"]([1.0, 2.0, 3.0, 4.0])
        expected_std = math.sqrt(5.0 / 3.0)
        margin = 1.96 * expected_std / 2.0
        self.assertEqual(4, n)
        self.assertAlmostEqual(2.5, mean)
        self.assertAlmostEqual(expected_std, std)
        self.assertAlmostEqual(2.5 - margin, low)
        self.assertAlmostEqual(2.5 + margin, high)

    def test_missing_duplicate_nan_and_impossible_values_are_rejected(self):
        valid = [
            {"scenario": "s", "algorithm": "a", "seed": seed}
            for seed in (1, 2)
        ]
        API["validate_observation_matrix"](valid, ("s",), ("a",), (1, 2))
        with self.assertRaisesRegex(API["AnalysisError"], "exatamente 2 observacoes"):
            API["validate_observation_matrix"](valid[:1], ("s",), ("a",), (1, 2))
        with self.assertRaisesRegex(API["AnalysisError"], "duplicada"):
            API["validate_observation_matrix"]([valid[0], valid[0]], ("s",), ("a",), (1, 2))
        with self.assertRaisesRegex(API["AnalysisError"], "NaN"):
            API["require_finite"]("NaN", "metric")
        with self.assertRaisesRegex(API["AnalysisError"], "valor impossivel"):
            API["require_integer"]("0", "makespan", minimum=1)


class AnalysisIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.experiment = cls.root / "raw" / "main"
        cls.summary = cls.root / "consolidated" / "summary.csv"
        cls.figures = cls.root / "figures"
        cls._create_experiment()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def _write_hashed(cls, relative: str, content: str) -> str:
        path = cls.experiment / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="")
        return hashlib.sha256(path.read_bytes()).hexdigest()

    @classmethod
    def _create_experiment(cls):
        workloads = []
        workload_hashes = {}
        for expected in API["canonical_workloads"]():
            content = f"synthetic workload {expected['scenario']} {expected['seed']}\n"
            digest = cls._write_hashed(expected["path"], content)
            workload_hashes[(expected["scenario"], expected["seed"])] = digest
            workloads.append({**expected, "workload_sha256": digest, "status": "success"})

        runs = []
        header = ",".join(API["AGGREGATE_HEADER"])
        for expected in API["canonical_runs"]():
            scenario_index = API["SCENARIOS"].index(expected["scenario"])
            algorithm_index = API["ALGORITHMS"].index(expected["algorithm"])
            seed = expected["seed"]
            workload_hash = workload_hashes[(expected["scenario"], seed)]
            mean_turnaround = seed + scenario_index + algorithm_index / 10
            switches = 100 + seed + scenario_index + algorithm_index
            jain = 80 + seed / 100 + algorithm_index / 1000
            values = [
                "1", expected["run_id"], workload_hash, expected["algorithm"],
                expected["scenario"], str(seed), "1000", "1", "4", "10000",
                format(mean_turnaround, ".17g"), str(switches), format(jain, ".17g"), "success",
            ]
            digest = cls._write_hashed(expected["result_path"], header + "\n" + ",".join(values) + "\n")
            runs.append({
                **expected,
                "process_count": 1000,
                "context_switch_cost": 1,
                "rr_quantum": 4,
                "workload_sha256": workload_hash,
                "result_sha256": digest,
                "status": "success",
            })

        manifest = {
            "schema_version": 1,
            "experiment_id": "main",
            "git_commit": subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=REPO, text=True,
                stdout=subprocess.PIPE, check=True,
            ).stdout.strip(),
            "finished_at": "2026-08-18T00:00:00+00:00",
            "config": {
                "effective": {"process_count": 1000, "context_switch_cost": 1, "rr_quantum": 4},
                "scenarios": list(API["SCENARIOS"]),
                "seeds": {"first": 1, "last": 1000},
                "algorithms": list(API["ALGORITHMS"]),
            },
            "workloads": workloads,
            "runs": runs,
            "summary": {
                "expected_workloads": 4000,
                "valid_workloads": 4000,
                "expected_runs": 20000,
                "successful_runs": 20000,
            },
        }
        (cls.experiment / "manifest.json").write_text(
            json.dumps(manifest), encoding="utf-8", newline="\n"
        )

    def invoke(self, success=True):
        completed = subprocess.run(
            [
                sys.executable, str(ANALYZER),
                "--experiment-dir", str(self.experiment),
                "--summary-output", str(self.summary),
                "--figures-dir", str(self.figures),
            ],
            cwd=REPO, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        if success and completed.returncode != 0:
            self.fail(f"analisador falhou ({completed.returncode}): {completed.stderr}")
        return completed

    @staticmethod
    def png_properties(path: Path):
        data = path.read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise AssertionError("arquivo nao e PNG")
        offset = 8
        width = height = ppm = unit = None
        while offset < len(data):
            length = struct.unpack(">I", data[offset:offset + 4])[0]
            kind = data[offset + 4:offset + 8]
            payload = data[offset + 8:offset + 8 + length]
            if kind == b"IHDR":
                width, height = struct.unpack(">II", payload[:8])
            if kind == b"pHYs":
                ppm, _, unit = struct.unpack(">IIB", payload)
            offset += 12 + length
        return width, height, ppm, unit

    def test_single_command_generates_summary_and_three_300_dpi_figures(self):
        completed = self.invoke()
        self.assertIn("20000 observacoes validas", completed.stdout)
        with self.summary.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(60, len(rows))
        self.assertEqual({"1000"}, {row["n"] for row in rows})
        first = next(
            row for row in rows
            if row["scenario"] == "equilibrado"
            and row["algorithm"] == "fcfs"
            and row["metric"] == "mean_turnaround"
        )
        self.assertAlmostEqual(500.5, float(first["mean"]))
        self.assertAlmostEqual(math.sqrt(83416.66666666667), float(first["std"]))
        expected_names = {metadata["filename"] for metadata in API["METRICS"].values()}
        self.assertEqual(expected_names, {path.name for path in self.figures.glob("*.png")})
        for path in self.figures.glob("*.png"):
            width, height, ppm, unit = self.png_properties(path)
            self.assertGreater(width, 2500)
            self.assertGreater(height, 1500)
            self.assertEqual(1, unit)
            self.assertAlmostEqual(300, ppm * 0.0254, delta=0.1)

    def test_experiment_before_pdbh_is_rejected(self):
        manifest_path = self.experiment / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["git_commit"] = "f29f90d2f50760d5e222019251ee9a4781485fb7"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        rejected = self.invoke(success=False)
        self.assertEqual(2, rejected.returncode)
        self.assertIn("anterior a implementacao do PDBH", rejected.stderr)
        self._create_experiment()

    def test_corruption_blocks_publication_and_preserves_previous_outputs(self):
        self.invoke()
        before = {path: path.read_bytes() for path in [self.summary, *self.figures.glob("*.png")]}
        manifest_path = self.experiment / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        run = manifest["runs"][0]
        result = self.experiment / run["result_path"]
        rows = list(csv.reader(result.read_text(encoding="utf-8").splitlines()))
        rows[1][API["AGGREGATE_HEADER"].index("mean_turnaround")] = "NaN"
        result.write_text("\n".join(",".join(row) for row in rows) + "\n", encoding="utf-8")
        run["result_sha256"] = hashlib.sha256(result.read_bytes()).hexdigest()
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        rejected = self.invoke(success=False)
        self.assertEqual(2, rejected.returncode)
        self.assertIn("NaN", rejected.stderr)
        self.assertEqual(before, {path: path.read_bytes() for path in before})

        self._create_experiment()


if __name__ == "__main__":
    unittest.main()
