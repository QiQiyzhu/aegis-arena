"""Adversarial gate checks use synthetic records; native behavior runs separately."""
import copy
import json
import unittest

import run_unreal_copilot_fault_probe as gate


class CopilotFaultGateTests(unittest.TestCase):
    def setUp(self):
        self.report = dict(schemaVersion=1, passed=True, engine='unreal-runtime', worldType=1, begunPlay=True,
                           rendering='NullRHI', fixtureService=True, syntheticKeyboardInput=True,
                           directPlannerApi=True, normalGameplayPauseUsed=True, actualModelInference=False,
                           syntheticSlateInput=False, humanUsabilityTest=False, fixtureDamage=False,
                           combatValuesChanged=False, wallSeconds=22, timeoutObservedSeconds=10.1,
                           plannerCounters=dict(requests=5, acceptedPlans=0, rejectedPlans=3, fallbacks=3, completedSteps=0),
                           assertions=[dict(name=name, passed=True, wallSeconds=index * 0.5, gameSeconds=1)
                                       for index, name in enumerate(gate.ASSERTIONS)])
        self.log = '\n'.join('LogTemp: Display: ' + line for line in
                             [gate.BEGIN, *['AEGIS_COPILOT_FAULT_ASSERT_PASS ' + name for name in gate.ASSERTIONS], gate.PASS])
        self.trace, self.http = [], []
        for index, case in enumerate(gate.CASES):
            body = {'model': gate.MODEL, 'messages': []}
            self.trace.append({'event': 'request', 'data': {'request_body': body}})
            self.http.append(dict(event='received', index=index, case=case, request=body, authorizationPresent=False, wallSeconds=index))
            plan = {'steps': [{'skill': 'capture_relay', 'target': 'none'}]}
            response = {'choices': [{'message': {'content': json.dumps(plan)}}]}
            self.http.append(dict(event='response_attempt', index=index, case=case, status=503 if index == 3 else 200,
                                  response=response, wallSeconds=index + gate.DELAYS[index]))
            self.http.append(dict(event='response_finished', index=index, case=case,
                                  result='sent' if index in (0, 3) else 'client_disconnected'))
        self.trace += [{'event': 'response'}] * 2 + [{'event': 'fallback'}] * 3
        self.trace += [dict(event='plan_cancelled', detail=reason) for reason in ('manual override', 'restart')]

    def test_complete_disclosed_evidence_passes(self):
        gate.validate(self.report, self.log)
        gate.validate_transport(self.trace, self.http)

    def test_missing_duplicate_or_failed_assertions_are_rejected(self):
        for name in gate.ASSERTIONS:
            report = copy.deepcopy(self.report)
            report['assertions'] = [row for row in report['assertions'] if row['name'] != name]
            with self.subTest(name=name), self.assertRaises(ValueError):
                gate.validate(report, self.log)
        report = copy.deepcopy(self.report)
        report['assertions'][0]['passed'] = False
        with self.assertRaises(ValueError): gate.validate(report, self.log)

    def test_replayed_logs_and_false_evidence_scope_are_rejected(self):
        for log in (self.log.replace('LogTemp: Display:', 'LogAutomationController:'),
                    self.log + '\nLogTemp: Display: ' + gate.PASS, self.log + '\nHandled ensure'):
            with self.assertRaises(ValueError): gate.validate(self.report, log)
        for field, value in (('actualModelInference', True), ('fixtureService', False),
                             ('humanUsabilityTest', True), ('worldType', True), ('worldType', 3),
                             ('wallSeconds', float('nan')), ('timeoutObservedSeconds', 1)):
            report = copy.deepcopy(self.report)
            report[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError): gate.validate(report, self.log)

    def test_late_plan_acceptance_or_missing_cancellation_is_rejected(self):
        with self.assertRaises(ValueError):
            gate.validate_transport(self.trace + [{'event': 'plan_accepted'}], self.http)
        trace = [row for row in self.trace if row.get('detail') != 'manual override']
        with self.assertRaises(ValueError): gate.validate_transport(trace, self.http)
        report = copy.deepcopy(self.report)
        report['plannerCounters']['acceptedPlans'] = 1
        with self.assertRaises(ValueError): gate.validate(report, self.log)

    def test_service_records_must_match_native_requests_and_wait_for_late_attempts(self):
        for mutation in ('missing', 'early', 'secret', 'body'):
            http = copy.deepcopy(self.http)
            if mutation == 'missing': http.pop()
            if mutation == 'early': http[4]['wallSeconds'] = 1.1
            if mutation == 'secret': http[0]['authorizationPresent'] = True
            if mutation == 'body': http[0]['request']['model'] = 'unrelated-model'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                gate.validate_transport(self.trace, http)


if __name__ == '__main__': unittest.main()
