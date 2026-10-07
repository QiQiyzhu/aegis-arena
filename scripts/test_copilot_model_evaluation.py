"""Offline checks that semantic scoring cannot confuse valid JSON with correct intent."""
import json
import unittest
from evaluate_copilot_model import classify, declared_cases, redact_response, strict_json_loads


class EvaluationScoringTests(unittest.TestCase):
    def setUp(self):
        self.cases = declared_cases({"wave": 1, "relay": 0, "objective": {"complete": False},
                                     "player": {}, "companion": {}, "visible_hostiles": []})

    def score(self, case, skills, targets=None):
        targets = targets or ["none"] * len(skills)
        plan = {"label": "Synthetic test", "steps": [dict(skill=skill, target=target)
                for skill, target in zip(skills, targets)]}
        return classify(json.dumps(plan), self.cases[case])[0]

    def test_predeclared_split_includes_two_empty_visibility_cases(self):
        self.assertEqual(len(self.cases), 12)
        self.assertEqual(sum(row["split"] == "heldout" for row in self.cases), 6)
        self.assertEqual(sum(not row["observation"]["visible_hostiles"] for row in self.cases), 2)

    def test_legal_partial_plan_is_not_complete_intent(self):
        verdict = self.score(0, ["capture_relay"])
        self.assertTrue(verdict["validJson"])
        self.assertTrue(verdict["skillsLegal"])
        self.assertFalse(verdict["fullIntentCorrect"])
        self.assertTrue(self.score(0, ["capture_relay", "guard"])["fullIntentCorrect"])
        self.assertFalse(self.score(0, ["guard", "capture_relay"])["fullIntentCorrect"])

    def test_valid_enum_target_can_still_be_illegal(self):
        verdict = self.score(0, ["capture_relay", "guard"], ["t0", "none"])
        self.assertTrue(verdict["schemaValid"])
        self.assertFalse(verdict["targetsLegal"])
        self.assertFalse(verdict["fullIntentCorrect"])
        self.assertFalse(self.score(8, ["focus_visible"], ["t0"])["skillsLegal"])

    def test_guard_must_not_be_replaced_by_a_legal_attack(self):
        verdict = self.score(1, ["focus_visible"], ["t0"])
        self.assertTrue(verdict["skillsLegal"])
        self.assertFalse(verdict["fullIntentCorrect"])
        self.assertTrue(self.score(1, ["regroup", "guard"])["fullIntentCorrect"])

    def test_malformed_or_extra_structure_cannot_pass(self):
        for content in ("not JSON", "42", '{"label":"x","steps":[]}',
                        '{"label":"x","steps":[{"skill":"guard","target":"none","extra":1}]}'):
            with self.subTest(content=content):
                self.assertFalse(classify(content, self.cases[1])[0]["fullIntentCorrect"])
        self.assertFalse(self.score(1, ["teleport"])["schemaValid"])

    def test_response_redaction_preserves_original_when_unnecessary(self):
        raw = b'{"id":"synthetic","choices":[]}'
        self.assertEqual(redact_response(raw, "fake-key"), raw)
        unsafe = b'{"headers":{"Authorization":"Bearer fake-key"},"content":"fake-key"}'
        cleaned = redact_response(unsafe, "fake-key")
        self.assertNotIn(b"fake-key", cleaned)
        self.assertEqual(json.loads(cleaned)["headers"], "[REDACTED]")

    def test_duplicate_keys_and_nonfinite_constants_match_native_rejection(self):
        duplicate = '{"label":"Guard","steps":[{"skill":"focus_visible","skill":"guard","target":"none"}]}'
        self.assertFalse(classify(duplicate, self.cases[1])[0]["validJson"])
        for raw in ('{"id":"first","id":"second"}', '{"value":NaN}', '{"value":Infinity}', '{"value":-Infinity}'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                strict_json_loads(raw)
