#!/usr/bin/env python3
"""Testes de integracao do executor da ISSUE-13."""

import json
import os
from pathlib import Path
import runpy
import signal
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


REPO = Path(__file__).resolve().parent.parent
RUNNER = REPO / "scripts" / "run_experiment.py"
SIMULATOR = REPO / "bin" / "simulador_dev"
RUNNER_API = runpy.run_path(str(RUNNER))


class ExperimentRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.results = self.root / "raw"
        self.config = self.root / "default.conf"
        shutil.copy(REPO / "configs" / "default.conf", self.config)

    def tearDown(self):
        self.temporary.cleanup()

    def command(self, experiment="test", binary=SIMULATOR, extra=()):
        return [sys.executable, str(RUNNER), "--experiment-id", experiment,
                "--binary", str(binary), "--config", str(self.config),
                "--results-dir", str(self.results), "--reduced", *extra]

    def invoke(self, experiment="test", binary=SIMULATOR, extra=(), env=None, success=True):
        completed = subprocess.run(self.command(experiment, binary, extra), cwd=REPO, text=True,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, env=env, check=False)
        if success and completed.returncode != 0:
            self.fail(f"executor falhou ({completed.returncode}): {completed.stderr}\n{completed.stdout}")
        return completed

    def manifest(self, experiment="test"):
        return json.loads((self.results / experiment / "manifest.json").read_text())

    def test_main_matrix_has_canonical_cardinality_and_unique_ids(self):
        config = {
            "effective": {"process_count": 1000, "context_switch_cost": 1, "rr_quantum": 4},
            "scenarios": list(RUNNER_API["SCENARIOS"]),
            "seeds": {"first": 1, "last": 100},
            "algorithms": list(RUNNER_API["ALGORITHMS"]),
        }
        workloads, runs = RUNNER_API["matrix_specs"](config)
        self.assertEqual(400, len(workloads))
        self.assertEqual(1600, len(runs))
        self.assertEqual(1600, len({run["run_id"] for run in runs}))

    def test_sjf_comparison_adds_runs_without_changing_required_matrix(self):
        required = RUNNER_API["REQUIRED_ALGORITHMS"]
        comparison = RUNNER_API["SJF_COMPARISON_ALGORITHMS"]
        self.assertEqual((*required, "sjf"), comparison)

        config = {
            "effective": {"process_count": 1000, "context_switch_cost": 1, "rr_quantum": 4},
            "scenarios": list(RUNNER_API["SCENARIOS"]),
            "seeds": {"first": 1, "last": 100},
            "algorithms": list(comparison),
        }
        workloads, runs = RUNNER_API["matrix_specs"](config)
        self.assertEqual(400, len(workloads))
        self.assertEqual(2000, len(runs))
        self.assertEqual(2000, len({run["run_id"] for run in runs}))

    def test_reduced_sjf_profile_reuses_each_workload_for_five_algorithms(self):
        completed = self.invoke(experiment="sjf", extra=("--include-sjf",))
        self.assertIn("[10/10]", completed.stdout)
        manifest = self.manifest("sjf")
        self.assertEqual(
            list(RUNNER_API["SJF_COMPARISON_ALGORITHMS"]),
            manifest["config"]["algorithms"],
        )
        self.assertEqual(2, manifest["summary"]["valid_workloads"])
        self.assertEqual(10, manifest["summary"]["successful_runs"])
        for workload in manifest["workloads"]:
            hashes = {
                run["workload_sha256"] for run in manifest["runs"]
                if (run["scenario"], run["seed"])
                == (workload["scenario"], workload["seed"])
            }
            self.assertEqual({workload["workload_sha256"]}, hashes)

    def test_execution_profiles_have_explicit_reproducible_shapes(self):
        shape = RUNNER_API["execution_shape"]
        self.assertEqual((RUNNER_API["SCENARIOS"], 1, 100, 1000), shape(False, False))
        self.assertEqual((RUNNER_API["SCENARIOS"], 1, 10, 1000), shape(False, True))
        self.assertEqual((("equilibrado",), 1, 2, 10), shape(True, False))

    def test_matrix_hashes_and_safe_resume(self):
        first = self.invoke()
        self.assertIn("[8/8]", first.stdout)
        self.assertIn("ETA", first.stdout)
        manifest = self.manifest()
        self.assertEqual(
            RUNNER_API["sha256"](RUNNER),
            manifest["runner_sha256"],
        )
        self.assertEqual(2, len(manifest["workloads"]))
        self.assertEqual(8, len(manifest["runs"]))
        self.assertEqual(2, manifest["summary"]["valid_workloads"])
        self.assertEqual(8, manifest["summary"]["successful_runs"])
        self.assertEqual({1}, {run["attempts"] for run in manifest["runs"]})
        for workload in manifest["workloads"]:
            hashes = {run["workload_sha256"] for run in manifest["runs"]
                      if (run["scenario"], run["seed"]) ==
                      (workload["scenario"], workload["seed"])}
            self.assertEqual({workload["workload_sha256"]}, hashes)
        manifest_path = self.results / "test" / "manifest.json"
        preserved_finished_at = "2000-01-01T00:00:00+00:00"
        manifest["finished_at"] = preserved_finished_at
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        self.invoke()
        resumed = self.manifest()
        self.assertEqual([1] * 8, [run["attempts"] for run in resumed["runs"]])
        self.assertEqual(preserved_finished_at, resumed["finished_at"])
        before_verify = manifest_path.read_bytes()
        verified = self.invoke(extra=("--verify-only",))
        self.assertIn("8/8 resultados", verified.stdout)
        self.assertEqual(before_verify, manifest_path.read_bytes())

    def test_verify_only_does_not_create_missing_experiment(self):
        verified = self.invoke(experiment="missing", extra=("--verify-only",), success=False)
        self.assertEqual(2, verified.returncode)
        self.assertIn("manifesto ausente", verified.stderr)
        self.assertFalse((self.results / "missing").exists())

    def test_partial_duplicate_and_hash_corruption_are_reexecuted(self):
        self.invoke()
        manifest = self.manifest()
        paths = [self.results / "test" / run["result_path"] for run in manifest["runs"]]
        paths[0].unlink()
        with paths[1].open("a", encoding="utf-8") as stream:
            stream.write(paths[1].read_text(encoding="utf-8").splitlines()[1] + "\n")
        with paths[2].open("a", encoding="utf-8") as stream:
            stream.write("corruption\n")
        self.invoke()
        resumed = self.manifest()
        attempts = [run["attempts"] for run in resumed["runs"]]
        self.assertEqual([2, 2, 2, 1, 1, 1, 1, 1], attempts)
        self.assertTrue(all(resumed["runs"][index]["reexecutions"] for index in range(3)))
        self.invoke(extra=("--verify-only",))

        workload = self.results / "test" / self.manifest()["workloads"][1]["path"]
        with workload.open("a", encoding="utf-8") as stream:
            stream.write("corruption\n")
        self.invoke()
        resumed = self.manifest()
        attempts = [run["attempts"] for run in resumed["runs"]]
        # A carga e regenerada de forma identica; so o FCFS que a publica precisa rodar.
        self.assertEqual([2, 2, 2, 1, 2, 1, 1, 1], attempts)
        self.assertIn("workload invalido", resumed["runs"][4]["reexecutions"][-1]["reason"])
        self.invoke(extra=("--verify-only",))

    def test_identity_changes_and_duplicate_manifest_are_rejected(self):
        self.invoke()
        with self.config.open("a", encoding="utf-8") as stream:
            stream.write("# identidade diferente\n")
        self.assertEqual(2, self.invoke(success=False).returncode)
        shutil.copy(REPO / "configs" / "default.conf", self.config)
        manifest_path = self.results / "test" / "manifest.json"
        manifest = self.manifest()
        manifest["runs"].append(dict(manifest["runs"][0]))
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        self.assertEqual(2, self.invoke(success=False).returncode)

    def test_canonical_run_fields_are_not_trusted_from_manifest(self):
        self.invoke()
        manifest_path = self.results / "test" / "manifest.json"
        manifest = self.manifest()
        manifest["runs"][0]["run_id"] = "tampered-run-id"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        rejected = self.invoke(extra=("--verify-only",), success=False)
        self.assertEqual(2, rejected.returncode)
        self.assertIn("run_id incompativel", rejected.stderr)

    def test_failure_is_recorded_and_resume_retries_only_missing(self):
        wrapper = self.root / "simulator-wrapper.py"
        wrapper.write_text(
            "#!/usr/bin/env python3\n"
            "import os, subprocess, sys\n"
            "args=sys.argv[1:]\n"
            "rid=args[args.index('--run-id')+1]\n"
            "if os.environ.get('FAIL_RUN_ID') == rid:\n"
            " print('falha injetada', file=sys.stderr); sys.exit(17)\n"
            f"sys.exit(subprocess.run([{str(SIMULATOR)!r}, *args]).returncode)\n",
            encoding="utf-8",
        )
        wrapper.chmod(0o755)
        environment = os.environ.copy()
        environment["FAIL_RUN_ID"] = "equilibrado-rr-seed-1"
        first = self.invoke(binary=wrapper, env=environment, success=False)
        self.assertEqual(1, first.returncode)
        failed = next(run for run in self.manifest()["runs"]
                      if run["run_id"] == environment["FAIL_RUN_ID"])
        self.assertEqual("failed", failed["status"])
        self.assertEqual(17, failed["failures"][0]["exit_code"])
        self.invoke(binary=wrapper)
        manifest = self.manifest()
        retried = next(run for run in manifest["runs"] if run["run_id"] == failed["run_id"])
        self.assertEqual(2, retried["attempts"])
        self.assertEqual("success", retried["status"])
        self.assertTrue(all(run["attempts"] == 1 for run in manifest["runs"]
                            if run["run_id"] != failed["run_id"]))

    def test_sigint_is_persisted_and_summary_remains_incremental(self):
        marker = self.root / "wrapper-ready"
        wrapper = self.root / "interrupt-wrapper.py"
        wrapper.write_text(
            "#!/usr/bin/env python3\n"
            "from pathlib import Path\n"
            "import subprocess, sys, time\n"
            "args=sys.argv[1:]\n"
            "rid=args[args.index('--run-id')+1]\n"
            f"marker=Path({str(marker)!r})\n"
            "if rid == 'equilibrado-rr-seed-1' and "
            "(not marker.exists() or marker.read_text(encoding='utf-8') != 'resume'):\n"
            " marker.write_text('ready', encoding='utf-8')\n"
            " time.sleep(30)\n"
            f"sys.exit(subprocess.run([{str(SIMULATOR)!r}, *args]).returncode)\n",
            encoding="utf-8",
        )
        wrapper.chmod(0o755)
        process = subprocess.Popen(
            self.command(experiment="interrupt", binary=wrapper), cwd=REPO, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True,
        )
        try:
            deadline = time.monotonic() + 5
            while not marker.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(marker.exists(), "wrapper nao iniciou a execucao interrompivel")
            os.killpg(process.pid, signal.SIGINT)
            stdout, stderr = process.communicate(timeout=10)
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.communicate(timeout=5)

        self.assertEqual(130, process.returncode)
        self.assertIn("[1/8]", stdout)
        self.assertIn("ETA", stdout)
        self.assertIn("Interrompido com seguranca", stderr)
        manifest = self.manifest("interrupt")
        interrupted = next(run for run in manifest["runs"]
                           if run["run_id"] == "equilibrado-rr-seed-1")
        self.assertEqual("interrupted", interrupted["status"])
        self.assertIsNotNone(interrupted["finished_at"])
        self.assertEqual(130, interrupted["failures"][-1]["exit_code"])
        self.assertEqual(1, manifest["summary"]["valid_workloads"])
        self.assertEqual(1, manifest["summary"]["successful_runs"])

        marker.write_text("resume", encoding="utf-8")
        resumed_output = self.invoke(experiment="interrupt", binary=wrapper)
        self.assertIn("iniciando em 1/8", resumed_output.stdout)
        self.assertIn("[8/8]", resumed_output.stdout)
        resumed = self.manifest("interrupt")
        retried = next(run for run in resumed["runs"]
                       if run["run_id"] == "equilibrado-rr-seed-1")
        self.assertEqual(2, retried["attempts"])
        self.assertEqual("success", retried["status"])
        self.assertEqual(2, resumed["summary"]["valid_workloads"])
        self.assertEqual(8, resumed["summary"]["successful_runs"])


if __name__ == "__main__":
    unittest.main()
