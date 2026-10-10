# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import unittest

SCRIPT = Path(__file__).parents[1] / "github_distribution_metrics.py"
spec = importlib.util.spec_from_file_location("github_distribution_metrics", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MetricsTests(unittest.TestCase):
    def test_first_baseline_has_no_delta(self):
        self.assertEqual(module.downloads_delta({}, {"1": {"downloads": 9}}), 9)
        # The collector emits null for the first snapshot, without invoking this helper.

    def test_unchanged_asset_and_deletions_never_negative(self):
        old = {"1": {"downloads": 12}, "2": {"downloads": 7}}
        new = {"1": {"downloads": 10}, "3": {"downloads": 4}}
        self.assertEqual(module.downloads_delta(old, new), 4)

    def test_merge_overwrites_rolling_days_and_keeps_older(self):
        old = {"clones": {"2026-09-01": {"count": 1, "uniques": 1}}}
        response = {"clones": [{"timestamp": "2026-10-08T00:00:00Z", "count": 8, "uniques": 5}]}
        result = module.merge_traffic(old, "clones", response)
        self.assertEqual(result["clones"]["2026-09-01"]["count"], 1)
        self.assertEqual(result["clones"]["2026-10-08"]["uniques"], 5)


if __name__ == "__main__":
    unittest.main()
