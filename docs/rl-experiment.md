# Learning Agents / RL experiment status

**Not implemented, not trained, not evaluated.** There is no Learning Agents plugin enabled in the project, no policy checkpoint, training curve, demonstration dataset, reward history or RL success claim. A supported Unreal installation was unavailable during the verified work. The user is arranging an engine installation; installing an editor does not by itself establish Learning Agents compatibility or successful training.

Learning Agents is experimental. It must not be described as a commercially stable production solution. No imitation-learning claim is made either.

## Conditional future experiment

If the installed UE 5.8 hotfix's official Learning Agents plugin can actually run, scope the experiment to projectile dodging in this arena. Keep gameplay, encounter composition and reward calculation fixed while comparing controllers.

| Interface | Proposed definition, not implemented |
|---|---|
| Observation | Relative visible target/projectile positions, self velocity/health, known cover distance, cooldowns; fixed normalization and masks for missing observations |
| Action | 2D movement, bounded dodge request, attack/no attack |
| Reward | Small survival reward, successful collision-verified dodge, applied damage, preferred-distance term |
| Penalty | Applied damage, death, stuck duration, leaving arena |
| Scripted comparator | Same authorized observation and action budget, same held-out seeds |
| Train/evaluation split | Separate seeds and projectile patterns; freeze weights before evaluation |

Reward hacking examples: circling forever to accumulate survival reward; damaging an invulnerable target to farm hit rewards; moving between two locations to evade a naive stuck detector. Use finite episodes, applied damage, task success metrics and adversarial scenario cases to reveal these behaviors.

High training reward is not evidence of generalization. Report independent success rate, damage taken, survival, collision-verified dodge rate and failure cases alongside reward. Keep baseline and learned policies under the same sensing/navigation/action constraints. Training curves alone must not appear in the resume as proof of a useful bot.

Imitation initialization should be attempted only if demonstration recording and training genuinely work in the installed plugin. Compare against RL from scratch at equal environment-step budgets, on independent evaluation seeds. Until then, this file is a proposed experimental protocol, not a completed experiment.

Official reference: [Learning Agents experimental plugin index](https://dev.epicgames.com/documentation/unreal-engine/API/PluginIndex/LearningAgents).
