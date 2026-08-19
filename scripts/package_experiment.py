#!/usr/bin/env python3
"""Validate and package the canonical experiment as deterministic release evidence."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile

import analyze_experiment


class EvidenceError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_value(repo: Path, *arguments: str) -> str:
    completed = subprocess.run(
        ["git", *arguments], cwd=repo, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False,
    )
    if completed.returncode != 0 or not completed.stdout.strip():
        detail = completed.stderr.strip() or "resposta Git vazia"
        raise EvidenceError(f"falha ao consultar Git: {detail}")
    return completed.stdout.strip()


def git_blob_sha256(repo: Path, commit: str, path: str) -> str:
    completed = subprocess.run(
        ["git", "show", f"{commit}:{path}"], cwd=repo, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False,
    )
    if completed.returncode != 0:
        detail = completed.stderr.decode(errors="replace").strip() or "blob Git indisponivel"
        raise EvidenceError(f"falha ao ler {path} na revisao congelada: {detail}")
    return hashlib.sha256(completed.stdout).hexdigest()


def validate_frozen_revision(manifest: dict, repo: Path) -> tuple[str, str, str]:
    tag = manifest.get("git_tag")
    commit = manifest.get("git_commit")
    if not isinstance(tag, str) or not tag:
        raise EvidenceError("manifesto final deve registrar uma tag Git exata")
    if not isinstance(commit, str) or len(commit) != 40:
        raise EvidenceError("manifesto final deve registrar o commit Git completo")
    tagged_commit = git_value(repo, "rev-list", "-n", "1", tag)
    if tagged_commit != commit:
        raise EvidenceError(
            f"tag {tag} aponta para {tagged_commit}, mas o manifesto registra {commit}"
        )
    runner_sha256 = git_blob_sha256(repo, commit, "scripts/run_experiment.py")
    registered_runner = manifest.get("runner_sha256")
    if registered_runner is not None and registered_runner != runner_sha256:
        raise EvidenceError("hash do executor diverge da revisao congelada")
    return tag, commit, runner_sha256


def expected_files(experiment_dir: Path) -> list[Path]:
    entries = list(experiment_dir.rglob("*"))
    for path in entries:
        if path.is_symlink():
            raise EvidenceError(
                f"link simbolico nao permitido na evidencia bruta: {path.relative_to(experiment_dir)}"
            )
    relative = [Path("manifest.json")]
    relative.extend(Path(item["path"]) for item in analyze_experiment.canonical_workloads())
    relative.extend(Path(item["result_path"]) for item in analyze_experiment.canonical_runs())
    expected = sorted(relative, key=lambda path: path.as_posix())
    actual = sorted(
        (path.relative_to(experiment_dir) for path in entries if path.is_file()),
        key=lambda path: path.as_posix(),
    )
    if actual != expected:
        missing = sorted(set(expected) - set(actual), key=lambda path: path.as_posix())
        extra = sorted(set(actual) - set(expected), key=lambda path: path.as_posix())
        raise EvidenceError(
            f"conjunto bruto inesperado; ausentes={len(missing)}, extras={len(extra)}"
        )
    for relative_path in expected:
        path = experiment_dir / relative_path
        if path.is_symlink() or not path.is_file():
            raise EvidenceError(f"artefato bruto nao e arquivo regular: {relative_path}")
    return expected


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


def atomic_copy(source: Path, target: Path) -> None:
    temporary = target.with_name(f".{target.name}.tmp.{os.getpid()}")
    try:
        with source.open("rb") as input_stream, temporary.open("wb") as output_stream:
            for chunk in iter(lambda: input_stream.read(1024 * 1024), b""):
                output_stream.write(chunk)
            output_stream.flush()
            os.fsync(output_stream.fileno())
        os.replace(temporary, target)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def create_archive(experiment_dir: Path, relative_files: list[Path], target: Path,
                   archive_root: str) -> None:
    temporary = target.with_name(f".{target.name}.tmp.{os.getpid()}")
    try:
        with temporary.open("wb") as raw_stream:
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw_stream, mtime=0) as gzip_stream:
                with tarfile.open(fileobj=gzip_stream, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                    for relative_path in relative_files:
                        source = experiment_dir / relative_path
                        info = archive.gettarinfo(
                            str(source), arcname=f"{archive_root}/{relative_path.as_posix()}"
                        )
                        info.uid = 0
                        info.gid = 0
                        info.uname = ""
                        info.gname = ""
                        info.mtime = 0
                        info.mode = 0o644
                        with source.open("rb") as stream:
                            archive.addfile(info, stream)
            raw_stream.flush()
            os.fsync(raw_stream.fileno())
        os.replace(temporary, target)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def published_hashes(repo: Path, summary: Path, figures_dir: Path) -> dict[str, str]:
    paths = [summary, *sorted(figures_dir.glob("*.png"))]
    if len(paths) != 4 or any(not path.is_file() for path in paths):
        raise EvidenceError("summary.csv e os tres graficos PNG devem existir")
    return {os.path.relpath(path, repo): sha256(path) for path in paths}


def build_record(manifest: dict, tag: str, commit: str, runner_sha256: str, archive: Path,
                 manifest_snapshot: Path, relative_files: list[Path],
                 output_hashes: dict[str, str]) -> dict:
    summary = manifest["summary"]
    return {
        "schema_version": 1,
        "experiment_id": manifest["experiment_id"],
        "git_tag": tag,
        "git_commit": commit,
        "binary_sha256": manifest["binary_sha256"],
        "runner_sha256": runner_sha256,
        "experiment_started_at": manifest["started_at"],
        "experiment_finished_at": manifest["finished_at"],
        "matrix": {
            "workloads": summary["valid_workloads"],
            "runs": summary["successful_runs"],
        },
        "manifest": {
            "file": manifest_snapshot.name,
            "sha256": sha256(manifest_snapshot),
        },
        "raw_archive": {
            "file": archive.name,
            "sha256": sha256(archive),
            "members": len(relative_files),
            "size_bytes": archive.stat().st_size,
        },
        "published_outputs": output_hashes,
    }


def execute(args: argparse.Namespace) -> int:
    repo = Path(__file__).resolve().parent.parent
    experiment_dir = (repo / args.experiment_dir).resolve()
    output_dir = (repo / args.output_dir).resolve()
    summary_path = (repo / args.summary).resolve()
    figures_dir = (repo / args.figures_dir).resolve()

    observations = analyze_experiment.load_observations(experiment_dir, repo)
    if len(observations) != 2000:
        raise EvidenceError("a validacao nao retornou as 2.000 observacoes canonicas")
    manifest_path = experiment_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    tag, commit, runner_sha256 = validate_frozen_revision(manifest, repo)
    relative_files = expected_files(experiment_dir)
    output_hashes = published_hashes(repo, summary_path, figures_dir)

    archive = output_dir / f"{tag}-raw.tar.gz"
    manifest_snapshot = output_dir / f"{tag}-manifest.json"
    record_path = output_dir / f"{tag}-evidence.json"
    if args.verify_only:
        if not archive.is_file() or not manifest_snapshot.is_file() or not record_path.is_file():
            raise EvidenceError("pacote, manifesto ou registro de evidencia ausente")
        if manifest_snapshot.read_bytes() != manifest_path.read_bytes():
            raise EvidenceError("snapshot do manifesto diverge do manifesto validado")
        actual_record = json.loads(record_path.read_text(encoding="utf-8"))
        expected_record = build_record(
            manifest, tag, commit, runner_sha256, archive, manifest_snapshot,
            relative_files, output_hashes
        )
        if actual_record != expected_record:
            raise EvidenceError("registro de evidencia diverge dos artefatos atuais")
        print(
            f"Evidencia valida: {tag}, 400 workloads, 2000 resultados, "
            f"arquivo {archive.name} ({archive.stat().st_size} bytes)."
        )
        return 0

    output_dir.mkdir(parents=True, exist_ok=True)
    create_archive(experiment_dir, relative_files, archive, tag)
    atomic_copy(manifest_path, manifest_snapshot)
    record = build_record(
        manifest, tag, commit, runner_sha256, archive, manifest_snapshot,
        relative_files, output_hashes
    )
    atomic_json(record_path, record)
    print(f"Evidencia empacotada: {archive}")
    print(f"SHA-256: {record['raw_archive']['sha256']}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiment-dir", default="results/raw/main")
    parser.add_argument("--output-dir", default="results/evidence")
    parser.add_argument("--summary", default="results/consolidated/summary.csv")
    parser.add_argument("--figures-dir", default="results/figures")
    parser.add_argument("--verify-only", action="store_true")
    return parser.parse_args()


if __name__ == "__main__":
    try:
        sys.exit(execute(parse_args()))
    except (EvidenceError, analyze_experiment.AnalysisError, OSError, ValueError,
            json.JSONDecodeError, tarfile.TarError) as error:
        print(f"Erro: {error}", file=sys.stderr)
        sys.exit(2)
