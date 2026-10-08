#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""Exercise revision selection with real Git and simulated package/CI tools."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
UPDATER = ROOT / "packaging/arch/vela-update"
STUB = r'''#!/usr/bin/env python3
import json, os, pathlib, subprocess, sys
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
def record(**extra):
    with open(os.environ["TEST_CALLS"], "a") as log:
        log.write(json.dumps(dict(tool=name, args=args, **extra)) + "\n")
if name == "pacman":
    if not os.environ.get("TEST_INSTALLED"):
        sys.exit(1)
    print("vela-git 0.1.0.r1.g" + os.environ["TEST_INSTALLED"] + "-1")
elif name == "makepkg":
    # Read the real PKGBUILD's VCS URL and check out what it asks makepkg for.
    url = subprocess.check_output(
        ["bash", "-c", 'source ./PKGBUILD; printf "%s" "${source[0]}"'], text=True)
    url = url.split("::git+", 1)[1]
    repo, fragment = url.split("#commit=", 1)
    subprocess.run(["git", "clone", "--quiet", "--mirror", repo, "sources.git"], check=True)
    subprocess.run(["git", "clone", "--quiet", "--shared", "sources.git", "checkout"], check=True)
    subprocess.run(["git", "-C", "checkout", "checkout", "--quiet", "--detach", fragment], check=True)
    sha = subprocess.check_output(["git", "-C", "checkout", "rev-parse", "HEAD"], text=True).strip()
    record(sha=sha, url=url, pkgbuild=pathlib.Path("PKGBUILD").read_text())
elif name == "gh":
    record()
    if args[:2] == ["auth", "status"]:
        sys.exit(0 if os.environ.get("TEST_CI_RUN") else 1)
    if args[:2] == ["run", "list"]:
        print(os.environ["TEST_CI_RUN"])
    elif args[0] == "api":
        print("99")
    elif args[:2] == ["run", "download"]:
        if os.environ.get("TEST_FAIL_DOWNLOAD"):
            sys.exit(1)
        dest = pathlib.Path(args[args.index("-D") + 1])
        dest.mkdir(parents=True)
        sha = os.environ["TEST_DOWNLOAD_SHA"]
        (dest / ("vela-git-0.1.0.r1.g" + sha + "-1-x86_64.pkg.tar.zst")).touch()
elif name == "sudo":
    record()
else:
    sys.exit("Unexpected tool: " + name)
'''


class UpdateRevision(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="vela-update-test-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.source = self.directory / "source"
        self.source.mkdir()
        self.git("init", "--quiet", "--initial-branch=main")
        self.git("config", "user.name", "Updater Test")
        self.git("config", "user.email", "test@example.invalid")
        self.git("remote", "add", "origin", "https://github.com/BrandoDev/vela.git")
        packaging = self.source / "packaging/arch"
        packaging.mkdir(parents=True)
        self.pkgbuild = packaging / "PKGBUILD"
        self.pkgbuild.write_text((ROOT / "packaging/arch/PKGBUILD").read_text())
        self.base = self.commit("base")
        self.pkgbuild.write_text(self.pkgbuild.read_text() + "\n# main package\n")
        self.main = self.commit("main update")
        self.git("checkout", "--quiet", "-b", "fix/nvidia", self.base)
        self.pkgbuild.write_text(self.pkgbuild.read_text() + "\n# NVIDIA test package\n")
        self.nvidia = self.commit("NVIDIA fix")
        self.git("checkout", "--quiet", "main")
        bin_dir = self.directory / "bin"
        bin_dir.mkdir()
        for name in ("pacman", "makepkg", "gh", "sudo"):
            stub = bin_dir / name
            stub.write_text(STUB)
            stub.chmod(0o755)
        self.calls_file = self.directory / "calls.jsonl"
        self.env = dict(os.environ, PATH=f"{bin_dir}:{os.environ['PATH']}",
                        XDG_CONFIG_HOME=str(self.directory / "config"),
                        XDG_CACHE_HOME=str(self.directory / "cache"),
                        TEST_CALLS=str(self.calls_file), TEST_INSTALLED=self.base[:7],
                        TEST_CI_RUN="", TEST_DOWNLOAD_SHA="", TEST_FAIL_DOWNLOAD="")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.source), *args], text=True).strip()

    def commit(self, message):
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", message)
        return self.git("rev-parse", "HEAD")

    def run_update(self, *args, status=0):
        result = subprocess.run(["sh", str(UPDATER), "--source", str(self.source), *args],
                                env=self.env, input="", text=True, capture_output=True)
        self.assertEqual(result.returncode, status, result.stdout + result.stderr)
        return result

    def calls(self, tool):
        if not self.calls_file.exists():
            return []
        return [call for line in self.calls_file.read_text().splitlines()
                if (call := json.loads(line))["tool"] == tool]

    def test_default_build_uses_main(self):
        self.run_update("--build")
        self.assertEqual(self.calls("makepkg")[0]["sha"], self.main)

    def test_branch_build_and_return_to_main(self):
        result = self.run_update("--branch", "fix/nvidia", "--build")
        build = self.calls("makepkg")[0]
        self.assertEqual(build["sha"], self.nvidia)
        self.assertIn("# NVIDIA test package", build["pkgbuild"])
        self.assertIn("NVIDIA fix", result.stdout)
        self.assertNotIn("main update", result.stdout)
        self.env["TEST_INSTALLED"] = self.nvidia[:7]
        self.run_update("--build")
        self.assertEqual(self.calls("makepkg")[-1]["sha"], self.main)

    def test_commit_build_supports_rollback_and_short_sha(self):
        self.env["TEST_INSTALLED"] = self.main[:7]
        self.run_update("--commit=" + self.base[:7], "--build")
        self.assertEqual(self.calls("makepkg")[0]["sha"], self.base)

    def test_full_commit_can_be_fetched_without_a_branch(self):
        self.run_update("--check")
        self.git("checkout", "--quiet", "--detach", self.base)
        self.pkgbuild.write_text(self.pkgbuild.read_text() + "\n# unreferenced test commit\n")
        detached = self.commit("detached fix")
        self.git("checkout", "--quiet", "main")
        self.run_update("--commit", detached, "--build")
        self.assertEqual(self.calls("makepkg")[0]["sha"], detached)

    def test_branch_check_does_not_install(self):
        self.run_update("--branch=fix/nvidia", "--check")
        self.assertEqual(self.calls("makepkg") + self.calls("sudo"), [])

    def test_already_installed_selected_commit(self):
        self.env["TEST_INSTALLED"] = self.nvidia[:7]
        self.run_update("--commit", self.nvidia, "--check", status=1)
        self.assertEqual(self.calls("makepkg"), [])
        self.run_update("--commit", self.nvidia, "--force", "--build")
        self.assertEqual(self.calls("makepkg")[0]["sha"], self.nvidia)

    def test_download_uses_selected_commit(self):
        self.env.update(TEST_CI_RUN="123", TEST_DOWNLOAD_SHA=self.nvidia[:7])
        self.run_update("--branch", "fix/nvidia", "--download")
        listing = next(call for call in self.calls("gh") if call["args"][:2] == ["run", "list"])
        self.assertEqual(listing["args"][listing["args"].index("-c") + 1], self.nvidia)
        self.assertIn(".g" + self.nvidia[:7] + "-", self.calls("sudo")[0]["args"][-1])
        self.assertEqual(self.calls("makepkg"), [])

    def test_failed_download_builds_selected_commit(self):
        self.env.update(TEST_CI_RUN="123", TEST_FAIL_DOWNLOAD="1")
        self.run_update("--commit", self.nvidia, "--download")
        self.assertEqual(self.calls("makepkg")[0]["sha"], self.nvidia)

    def test_wrong_artifact_never_installed(self):
        self.env.update(TEST_CI_RUN="123", TEST_DOWNLOAD_SHA=self.main[:7])
        self.run_update("--branch", "fix/nvidia", "--download")
        self.assertEqual(self.calls("sudo"), [])
        self.assertEqual(self.calls("makepkg")[0]["sha"], self.nvidia)

    def test_missing_revision_never_installed(self):
        for args in (("--branch", "missing"), ("--commit", "0" * 40)):
            with self.subTest(args=args):
                self.run_update(*args, status=1)
        self.assertEqual(self.calls("makepkg") + self.calls("sudo"), [])

    def test_invalid_options_never_installed(self):
        for args in (("--branch",), ("--commit",), ("--branch=",), ("--commit=",),
                     ("--branch", "../main"), ("--commit", "HEAD"),
                     ("--branch", "main", "--commit", self.main)):
            with self.subTest(args=args):
                self.run_update(*args, status=2)
        self.assertEqual(self.calls("makepkg") + self.calls("sudo"), [])


if __name__ == "__main__":
    unittest.main()
