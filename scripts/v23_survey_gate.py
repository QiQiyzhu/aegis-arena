"""Validate the v2.3 survey ledger separately from the unchanged energy ledger."""
import math


class SurveyLedger:
    def __init__(self):
        self.enabled = False
        self.supply = False
        self.active = None
        self.interrupted = False
        self.positions = {}
        self.mask = self.keys = self.supplies = self.boosts = self.cancelled = 0
        self.player_heal = self.companion_heal = 0.0
        self.pending = False
        self.bound = -1
        self.used_relays = set()

    @staticmethod
    def num(data, key, low=0, high=1e9, integer=False):
        value = data.get(key)
        if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
            raise ValueError('Invalid survey number: ' + key)
        if integer and value != int(value): raise ValueError('Noninteger survey field: ' + key)
        return value

    @staticmethod
    def boolean(data, key):
        value = data.get(key)
        if type(value) is not bool: raise ValueError('Invalid survey boolean: ' + key)
        return value

    def distance(self, data):
        distance = self.num(data, 'distance', 0, 180.001)
        vectors = []
        for key in ('playerPosition', 'nodePosition'):
            vec = data.get(key)
            if not isinstance(vec, list) or len(vec) != 3: raise ValueError('Invalid survey position')
            vectors.append([self.num({'v': v}, 'v', -1e7, 1e7) for v in vec])
        actual = math.hypot(vectors[0][0] - vectors[1][0], vectors[0][1] - vectors[1][1])
        if not math.isclose(actual, distance, abs_tol=.02): raise ValueError('Survey distance disagrees with positions')

    def observe(self, kind, data, stamp, phase, wave):
        if kind == 'session_started':
            self.enabled = data.get('gameplayVersion') == '2.3'
            if self.enabled:
                for key, value in dict(surveySeconds=2.5, surveyRadius=180, surveyPlayerHeal=20,
                                       surveyCompanionHeal=15, surveyRelayMultiplier=1.25).items():
                    if self.num(data, key) != value: raise ValueError('Unexpected survey rules')
            return False
        if not kind.startswith('survey_'):
            if self.active and kind == 'damage' and str(data.get('victim', '')).startswith('AegisPlayerCharacter'):
                self.interrupted = True
            if self.active and kind == 'phase_changed' and data.get('phase') != 'active':
                self.interrupted = True
            return False
        if not self.enabled: raise ValueError('Survey event in an undeclared gameplay version')
        if kind not in ('survey_cancelled', 'survey_boost_finished') and phase != 'active':
            raise ValueError('Survey action outside active gameplay')
        if kind == 'survey_mode_selected':
            value = self.boolean(data, 'supply')
            if self.active or value == self.supply: raise ValueError('Invalid survey mode change')
            self.supply = value
        elif kind == 'survey_started':
            node = int(self.num(data, 'node', 0, 1, True))
            if self.active or self.mask & (1 << node): raise ValueError('Repeated or overlapping survey')
            if self.boolean(data, 'supply') != self.supply: raise ValueError('Survey choice was not selected')
            if not self.supply and (self.pending or wave == 3):
                raise ValueError('Survey key unavailable')
            self.distance(data)
            position = tuple(data['nodePosition'])
            if node in self.positions and self.positions[node] != position:
                raise ValueError('Survey cache moved between attempts')
            other = self.positions.get(1 - node)
            if other:
                expected = (2100, 1550, 0) if node == 1 else (-2100, -1550, 0)
                if any(abs(position[i] - other[i] - expected[i]) > .02 for i in range(3)):
                    raise ValueError('Survey cache map positions disagree')
            self.positions[node] = position
            self.active = (node, stamp, position)
            self.interrupted = False
        elif kind == 'survey_cancelled':
            node = self.num(data, 'node', 0, 1, True)
            if not self.active or node != self.active[0]: raise ValueError('Survey cancellation without a scan')
            progress = self.num(data, 'progressSeconds', 0, 2.5001)
            if progress > stamp - self.active[1] + .05: raise ValueError('Impossible cancelled scan duration')
            if data.get('reason') not in ('released', 'left_range', 'damaged', 'unavailable', 'reward_unavailable', 'session_ended'):
                raise ValueError('Unknown survey cancellation reason')
            self.active = None
            self.interrupted = False
            self.cancelled += 1
        elif kind == 'survey_claimed':
            node = int(self.num(data, 'node', 0, 1, True))
            if not self.active or self.active[0] != node or self.mask & (1 << node):
                raise ValueError('Survey reward lacks a unique scan')
            if self.interrupted: raise ValueError('Interrupted survey incorrectly granted a reward')
            elapsed = self.num(data, 'elapsedGameSeconds', 2.45)
            if abs(elapsed - (stamp - self.active[1])) > .05 or elapsed < 2.45:
                raise ValueError('Survey reward before continuous scan completed')
            if abs(self.num(data, 'scanSeconds') - 2.5) > .001: raise ValueError('Wrong required scan time')
            if self.boolean(data, 'supply') != self.supply: raise ValueError('Survey reward choice changed')
            self.distance(data)
            if tuple(data['nodePosition']) != self.active[2]: raise ValueError('Survey cache moved during a scan')
            player = self.num(data, 'playerActualHealing', 0, 20.001)
            companion = self.num(data, 'companionActualHealing', 0, 15.001)
            if self.supply:
                if player + companion <= 0: raise ValueError('Empty supply consumed a cache')
                self.supplies += 1
                self.player_heal += player
                self.companion_heal += companion
            else:
                if player or companion or self.pending or wave == 3:
                    raise ValueError('Invalid survey key reward')
                self.keys += 1
                self.pending = True
            self.mask |= 1 << node
            if self.num(data, 'claimedMask', 1, 3, True) != self.mask:
                raise ValueError('Survey claim mask mismatch')
            if self.boolean(data, 'keyPending') != self.pending: raise ValueError('Survey pending key mismatch')
            self.active = None
        elif kind == 'survey_boost_bound':
            key = int(self.num(data, 'relayKey', 10, 21, True))
            if not self.pending or self.bound != -1 or key not in (10, 20, 21) or key // 10 != wave or key in self.used_relays:
                raise ValueError('Unbacked or repeated survey relay boost')
            if self.num(data, 'multiplier') != 1.25: raise ValueError('Wrong survey relay multiplier')
            self.pending = False
            self.bound = key
            self.boosts += 1
            self.used_relays.add(key)
        elif kind == 'survey_boost_finished':
            if self.bound == -1 or self.num(data, 'relayKey', 10, 21, True) != self.bound:
                raise ValueError('Survey boost ended without a binding')
            if data.get('reason') not in ('relay_changed', 'relay_complete', 'session_ended'):
                raise ValueError('Unknown survey boost finish reason')
            self.bound = -1
        else:
            raise ValueError('Unknown survey event')
        return True

    def finish(self, summary, sample):
        if not self.enabled: return None
        if self.active or self.bound != -1: raise ValueError('Survey interaction left open at session end')
        if summary.get('gameplayVersion') != '2.3': raise ValueError('Survey summary version mismatch')
        expected = dict(surveyKeys=self.keys, surveySupplies=self.supplies, surveyBoosts=self.boosts,
                        surveyCancelled=self.cancelled, surveyPlayerActualHealing=self.player_heal,
                        surveyCompanionActualHealing=self.companion_heal, surveyClaimedMask=self.mask,
                        surveyKeyPending=self.pending, surveyBoostRelayKey=self.bound)
        for key, value in expected.items():
            observed = self.boolean(summary, key) if type(value) is bool else self.num(summary, key, -1, integer=type(value) is int)
            if (observed != value if type(value) in (bool, int) else not math.isclose(observed, value, abs_tol=.02)):
                raise ValueError('Survey summary mismatch: ' + key)
        for key in ('surveyClaimedMask', 'surveyKeys', 'surveySupplies', 'surveyBoosts'):
            if self.num(sample, key, 0, 3, True) != expected[key]: raise ValueError('Survey final sample mismatch: ' + key)
        return expected
