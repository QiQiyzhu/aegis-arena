"""Adversarial gameplay-evidence tests. JSON here is synthetic unit-test data."""
import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run_unreal_play_probe as gate


class PlayProbeEvidenceGateTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name).resolve()
        self.final = {name: 0 for name in gate.TOTALS}
        self.final.update(playerShots=3, enemyShots=8, playerDistanceCm=200, enemyDistanceCm=300,
                          playerDamageDealt=28, enemyDamageDealt=105, playerDamageTaken=100,
                          companionDamageTaken=5, playerDeaths=1)
        self.report = dict(schemaVersion=1, engine="unreal-runtime", syntheticInput=True,
                           renderOffscreen=True, renderingEnabled=True, humanUsabilityTest=False,
                           fixtureDamage=False, frozenAI=False, teleportedActors=False, forcedWaves=False,
                           worldType=1, begunPlay=True, passed=True, observationBudgetSeconds=60,
                           wallSeconds=8.2, observedOutcome="lost", finalPhase="lost", reason=gate.REASON,
                           screenshotRequests=6, screenshotProcessed=6, totals=self.final,
                           screenshots=[str(self.output / name) for name in gate.SCREENSHOTS])
        def sample(wall, phase, data):
            return dict(wallSeconds=wall, gameSeconds=wall + 0.5, phase=phase,
                        wave=0 if phase == "briefing" else 1, relay=0, charge=0,
                        objectiveComplete=False, objectiveContested=False, playerHealth=0 if phase == "lost" else 100,
                        enemyAlive=0 if phase == "briefing" else 3, playerPosition=[0, 0, 90],
                        objectivePosition=[-500, -1000, 20], companionCommand="guard", totals=copy.deepcopy(data))
        self.report["samples"] = [sample(0.1, "briefing", {name: 0 for name in gate.TOTALS}),
                                  sample(2, "active", self.final), sample(8.1, "lost", self.final)]
        def event(wall, kind, **fields):
            return dict(wallSeconds=wall, gameSeconds=wall + 0.5, kind=kind, **fields)
        self.report["events"] = [
            event(0, "transition", phase="briefing", wave=0, relay=0),
            event(0.6, "input", key="Enter", state="pressed"),
            event(0.61, "input", key="Enter", state="released"),
            event(0.62, "transition", phase="active", wave=1, relay=0),
            event(0.7, "input", key="W", state="pressed"),
            event(1, "input", key="LeftMouseButton", state="pressed"),
            event(1.1, "damage", amount=28, sourceTeam="player", victimTeam="enemy", source="Player_1", victim="Enemy_1"),
            event(1.2, "damage", amount=100, sourceTeam="enemy", victimTeam="player", source="Enemy_1", victim="Player_1"),
            event(1.3, "damage", amount=5, sourceTeam="enemy", victimTeam="companion", source="Enemy_1", victim="Companion_1"),
            event(8, "transition", phase="lost", wave=1, relay=0),
            event(8.1, "input", key="W", state="released"),
            event(8.1, "input", key="LeftMouseButton", state="released"),
        ]
        self.log = ("LogTemp: Display: " + gate.BEGIN +
                    "\nLogTemp: Display: AEGIS_PLAY_PROBE_PASS screenshots=6 wall=8.200 outcome=lost")

    def reject(self, report=None, log=None):
        with patch.object(gate, "inspect_png", return_value={}), self.assertRaises(ValueError):
            gate.validate(self.report if report is None else report, self.log if log is None else log, self.output)

    def test_natural_loss_is_valid_and_all_six_native_pngs_are_checked(self):
        with patch.object(gate, "inspect_png", return_value={}) as images:
            self.assertEqual(len(gate.validate(self.report, self.log, self.output)), 6)
            self.assertEqual([call.args[0] for call in images.call_args_list],
                             [self.output / name for name in gate.SCREENSHOTS])

    def test_firing_movement_and_mutual_damage_cannot_be_replaced_by_success_flag(self):
        for name in gate.POSITIVE_TOTALS:
            for value in (0, -1, float("nan"), float("inf"), True):
                report = copy.deepcopy(self.report)
                report["totals"][name] = value
                with self.subTest(metric=name, value=value): self.reject(report)

    def test_outcome_requires_matching_phase_and_budget_duration(self):
        for outcome, phase, seconds in (("won", "active", 8.2), ("lost", "won", 8.2),
                                       ("budget_exhausted", "active", 59.9),
                                       ("budget_exhausted", "lost", 60.1),
                                       ("budget_exhausted", "briefing", 60.1),
                                       ("unknown", "active", 60), ("lost", "lost", 66),
                                       ("lost", "lost", 7.9)):
            report = copy.deepcopy(self.report)
            report.update(observedOutcome=outcome, finalPhase=phase, wallSeconds=seconds)
            with self.subTest(outcome=outcome, phase=phase, seconds=seconds): self.reject(report)

    def test_wrong_world_intervention_or_incomplete_images_are_rejected(self):
        for name, value in (("worldType", 3), ("worldType", True), ("schemaVersion", True),
                            ("fixtureDamage", True), ("frozenAI", True), ("teleportedActors", True),
                            ("forcedWaves", True), ("humanUsabilityTest", True), ("syntheticInput", False),
                            ("renderingEnabled", False), ("begunPlay", False), ("passed", False),
                            ("screenshotRequests", 5), ("screenshotProcessed", 5), ("observationBudgetSeconds", 30)):
            report = copy.deepcopy(self.report)
            report[name] = value
            with self.subTest(name=name, value=value): self.reject(report)

    def test_original_unique_markers_and_matching_json_are_required(self):
        replay = "\n".join("LogAutomationController: " + line + " [log]" for line in self.log.splitlines())
        for log in ("", replay, self.log.splitlines()[1], self.log + "\n" + self.log,
                    self.log.replace("wall=8.200", "wall=8.300"), self.log.replace("outcome=lost", "outcome=won"),
                    self.log.replace("screenshots=6", "screenshots=5"), self.log.replace("frozen_ai=0", "frozen_ai=1"),
                    self.log + "\nHandled ensure", self.log + "\nFatal error:", self.log + "\nAEGIS_PLAY_PROBE_FAIL"):
            with self.subTest(log=log[:100]): self.reject(log=log)
        with patch.object(gate, "inspect_png", return_value={}):
            gate.validate(self.report, self.log + "\n" + replay, self.output)

    def test_sample_clock_metrics_positions_and_final_consistency_are_required(self):
        variations = []
        for path, value in (("wallSeconds", 0.05), ("gameSeconds", 0), ("charge", float("nan")),
                            ("playerPosition", [0, float("inf"), 1]), ("phase", "other"),
                            ("companionCommand", "unknown")):
            report = copy.deepcopy(self.report)
            report["samples"][1][path] = value
            variations.append(report)
        report = copy.deepcopy(self.report)
        report["samples"][-1]["totals"]["enemyShots"] = 7
        variations.append(report)
        report = copy.deepcopy(self.report)
        report["samples"] = []
        variations.append(report)
        for report in variations: self.reject(report)

    def test_damage_must_match_source_victim_events_and_controls_must_release(self):
        for kind in ("damage", "input", "transition"):
            report = copy.deepcopy(self.report)
            report["events"] = [event for event in report["events"] if event["kind"] != kind]
            with self.subTest(missing=kind): self.reject(report)
        for name, value in (("amount", 27), ("sourceTeam", "companion"), ("victimTeam", "invalid")):
            report = copy.deepcopy(self.report)
            report["events"][6][name] = value
            with self.subTest(name=name, value=value): self.reject(report)
        report = copy.deepcopy(self.report)
        report["events"].pop()
        self.reject(report)
        report = copy.deepcopy(self.report)
        report["events"][1]["state"] = "unknown"
        self.reject(report)

    def test_image_paths_cannot_escape_run_or_duplicate(self):
        for path in ("play-final.png", str(self.output.parent / "play-final.png"), self.report["screenshots"][0]):
            report = copy.deepcopy(self.report)
            report["screenshots"][-1] = path
            with self.subTest(path=path): self.reject(report)
        with patch.object(gate, "inspect_png", side_effect=ValueError("corrupt original PNG")), self.assertRaises(ValueError):
            gate.validate(self.report, self.log, self.output)

    def test_packaged_launch_never_runs_an_editor_or_uses_nullrhi(self):
        engine = self.output / "EngineInstallation"
        game = self.output / "Package/AegisArena.exe"
        packaged = gate.build_command(engine, self.output, game)
        self.assertEqual(packaged[:2], [str(game), gate.MAP])
        self.assertFalse(any("UnrealEditor" in arg or ".uproject" in arg for arg in packaged))
        self.assertNotIn("-game", packaged)
        for command in (packaged, gate.build_command(engine, self.output)):
            self.assertNotIn("-NullRHI", command)
            for flag in ("-RenderOffscreen", "-AegisPlayProbe", "-AegisInputProbe", "-ResX=1280", "-ResY=720"):
                self.assertIn(flag, command)
            self.assertIn(f"-AegisPlayOutput={self.output}", command)
        self.assertIn("-game", gate.build_command(engine, self.output))
