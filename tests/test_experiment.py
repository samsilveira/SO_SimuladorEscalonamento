#!/usr/bin/env python3
"""Testes de integracao do executor da ISSUE-13."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parent.parent
RUNNER = REPO / "scripts" / "run_experiment.py"
SIMULATOR = REPO / "bin" / "simulador_dev"


class ExperimentRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.results = self.root / "raw"
        self.config = self.root / "default.conf"
        shutil.copy(REPO / "configs" / "default.conf", self.config)

    def tearDown(self):
        self.temporary.cleanup()

    def invoke(self, experiment="test", binary=SIMULATOR, extra=(), env=None, success=True):
        command = [sys.executable, str(RUNNER), "--experiment-id", experiment,
                   "--binary", str(binary), "--config", str(self.config),
                   "--results-dir", str(self.results), "--reduced", *extra]
        completed = subprocess.run(command, cwd=REPO, text=True, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, env=env, check=False)
        if success and completed.returncode != 0:
            self.fail(f"executor falhou ({completed.returncode}): {completed.stderr}\n{completed.stdout}")
        return completed

    def manifest(self, experiment="test"):
        return json.loads((self.results / experiment / "manifest.json").read_text())

    def test_matrix_hashes_and_safe_resume(self):
        self.invoke()
        manifest = self.manifest()
        self.assertEqual(2, len(manifest["workloads"]))
        self.assertEqual(8, len(manifest["runs"]))
        self.assertEqual({1}, {run["attempts"] for run in manifest["runs"]})
        for workload in manifest["workloads"]:
            hashes = {run["workload_sha256"] for run in manifest["runs"]
                      if (run["scenario"], run["seed"]) ==
                      (workload["scenario"], workload["seed"])}
            self.assertEqual({workload["workload_sha256"]}, hashes)
        self.invoke()
        self.assertEqual([1] * 8, [run["attempts"] for run in self.manifest()["runs"]])
        verified = self.invoke(extra=("--verify-only",))
        self.assertIn("8/8 resultados", verified.stdout)

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
        attempts = [run["attempts"] for run in self.manifest()["runs"]]
        self.assertEqual([2, 2, 2, 1, 1, 1, 1, 1], attempts)
        self.invoke(extra=("--verify-only",))

        workload = self.results / "test" / self.manifest()["workloads"][1]["path"]
        with workload.open("a", encoding="utf-8") as stream:
            stream.write("corruption\n")
        self.invoke()
        attempts = [run["attempts"] for run in self.manifest()["runs"]]
        # A carga e regenerada de forma identica; so o FCFS que a publica precisa rodar.
        self.assertEqual([2, 2, 2, 1, 2, 1, 1, 1], attempts)
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


if __name__ == "__main__":
    unittest.main()
