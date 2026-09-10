import copy
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from run_benchmark import validate, wilson, run, ROOT

class ToolsTests(unittest.TestCase):
    def setUp(self):
        self.config = json.loads((ROOT / "scenarios/evaluation.json").read_text())
    def test_schema_roundtrip(self):
        self.assertEqual(validate(json.loads(json.dumps(self.config))), self.config)
    def test_reject_invalid_schema_and_ranges(self):
        for key, value in [("seed_start", -1), ("seed_start", True), ("episodes", 0), ("duration", 301), ("director", "yes"), ("enemy_counts", [0]), ("companion_policies", ["RL"]), ("arena", "not_a_map")]:
            with self.subTest(key=key, value=value):
                config=copy.deepcopy(self.config);config[key]=value
                with self.assertRaises(ValueError):validate(config)
    def test_overflow(self):
        self.config["seed_start"] = 2**32 - 1
        with self.assertRaises(ValueError):validate(self.config)
    def test_unknown_fields(self):
        self.config["secret"] = "not accepted"
        with self.assertRaises(ValueError):validate(self.config)
    def test_confidence_interval_not_point_estimate(self):
        low,high=wilson(30,30)
        self.assertLess(low,1);self.assertAlmostEqual(high,1)
    def test_actual_binary_json_csv_pipeline(self):
        binary=ROOT/("build/aegis_sim.exe" if sys.platform=="win32" else "build/aegis_sim")
        self.config["episodes"]=1;self.config["duration"]=2
        with tempfile.TemporaryDirectory() as directory:
            out=pathlib.Path(directory)
            report=run(self.config,out,binary)
            self.assertEqual(report["episode_count"],2)
            self.assertTrue((out/"episodes.csv").read_text().startswith("schema_version,engine,"))
            self.assertEqual(len(json.loads((out/"episodes.json").read_text())),2)
            with self.assertRaises(ValueError):run(self.config,out,binary)
    def test_cli_rejects_unknown_and_invalid(self):
        binary=ROOT/("build/aegis_sim.exe" if sys.platform=="win32" else "build/aegis_sim")
        for args in (["--seed","-1"],["--seed","1oops"],["--enemies","51"],["--policy","learning"],["--surprise","1"]):
            self.assertEqual(subprocess.run([str(binary),*args],capture_output=True).returncode,2)

if __name__=="__main__":unittest.main()
