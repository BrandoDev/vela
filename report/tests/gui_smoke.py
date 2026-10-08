# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real Qt frontend, Python bridge, review and ZIP export headlessly."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

with tempfile.TemporaryDirectory() as directory:
    output = Path(directory) / "report.zip"
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", HOME=directory,
                       XDG_STATE_HOME=directory, XDG_CONFIG_HOME=directory,
                       PYTHONPATH=str(Path(__file__).resolve().parents[1]))
    environment.pop("XDG_RUNTIME_DIR", None)
    subprocess.run([sys.argv[1], "--python", sys.executable, "--smoke-test", str(output)],
                   env=environment, check=True, timeout=25)
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        manifest = json.loads(archive.read("manifest.json"))
        assert manifest["description"]["title"] == "GUI smoke test"
        assert not manifest["artifacts"]
        assert set(archive.namelist()) == {"report.md", "manifest.json"}
    assert output.stat().st_mode & 0o777 == 0o600
print("GUI review/export smoke check passed.")
