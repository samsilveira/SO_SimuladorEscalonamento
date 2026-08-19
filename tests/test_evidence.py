#!/usr/bin/env python3
"""Regressoes do empacotamento da evidencia experimental da ISSUE-15."""

from pathlib import Path
import runpy
import sys
import tarfile
import tempfile
import unittest


REPO = Path(__file__).resolve().parent.parent
PACKAGER = REPO / "scripts" / "package_experiment.py"
sys.path.insert(0, str(REPO / "scripts"))
API = runpy.run_path(str(PACKAGER))


class EvidencePackageTests(unittest.TestCase):
    def test_raw_tree_rejects_symbolic_links_before_packaging(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "target"
            target.mkdir()
            (root / "linked-directory").symlink_to(target, target_is_directory=True)

            with self.assertRaisesRegex(API["EvidenceError"], "link simbolico"):
                API["expected_files"](root)

    def test_deterministic_archive_normalizes_metadata_and_order(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            experiment = root / "raw"
            (experiment / "runs").mkdir(parents=True)
            (experiment / "manifest.json").write_text('{"ok": true}\n', encoding="utf-8")
            (experiment / "runs" / "result.csv").write_text("a,b\n1,2\n", encoding="utf-8")
            relative = [Path("manifest.json"), Path("runs/result.csv")]
            first = root / "first.tar.gz"
            second = root / "second.tar.gz"

            API["create_archive"](experiment, relative, first, "experiment-v1")
            API["create_archive"](experiment, relative, second, "experiment-v1")

            self.assertEqual(first.read_bytes(), second.read_bytes())
            with tarfile.open(first, "r:gz") as archive:
                members = archive.getmembers()
            self.assertEqual(
                ["experiment-v1/manifest.json", "experiment-v1/runs/result.csv"],
                [member.name for member in members],
            )
            self.assertEqual({0}, {member.mtime for member in members})
            self.assertEqual({0}, {member.uid for member in members})
            self.assertEqual({0}, {member.gid for member in members})
            self.assertEqual({0o644}, {member.mode for member in members})

    def test_frozen_revision_must_match_exact_tag(self):
        manifest = {
            "git_tag": "experiment-v1",
            "git_commit": "8af1022a90e804e3d667e7accf41ae01620ad4be",
        }
        tag, commit, runner_sha256 = API["validate_frozen_revision"](manifest, REPO)
        self.assertEqual((manifest["git_tag"], manifest["git_commit"]), (tag, commit))
        self.assertEqual(64, len(runner_sha256))
        manifest["git_commit"] = "0" * 40
        with self.assertRaisesRegex(API["EvidenceError"], "mas o manifesto registra"):
            API["validate_frozen_revision"](manifest, REPO)

    def test_evidence_record_detects_archive_changes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "raw.tar.gz"
            snapshot = root / "manifest.json"
            archive.write_bytes(b"archive")
            snapshot.write_text("{}\n", encoding="utf-8")
            manifest = {
                "experiment_id": "main",
                "binary_sha256": "a" * 64,
                "started_at": "2026-08-19T00:00:00+00:00",
                "finished_at": "2026-08-19T00:01:00+00:00",
                "summary": {"valid_workloads": 400, "successful_runs": 1600},
            }
            first = API["build_record"](
                manifest, "experiment-v1", "b" * 40, "d" * 64, archive, snapshot,
                [Path("manifest.json")], {"summary.csv": "c" * 64},
            )
            archive.write_bytes(b"changed")
            second = API["build_record"](
                manifest, "experiment-v1", "b" * 40, "d" * 64, archive, snapshot,
                [Path("manifest.json")], {"summary.csv": "c" * 64},
            )
            self.assertNotEqual(first["raw_archive"]["sha256"], second["raw_archive"]["sha256"])


if __name__ == "__main__":
    unittest.main()
