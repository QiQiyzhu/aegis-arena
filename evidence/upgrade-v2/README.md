# Upgrade v2 本地原始证据

## 最终交付补充

最终验收见 [acceptance.json](acceptance.json)。最终源码摘要为 `fae2c996bdfab8a7a5689861f4c97da0bd859028b36a52957aeff04680efa8c7`，资产摘要仍为 `fed2e9efde70c084ffa0c33d6392494952d78c9e7081b8b773b284c1979eeb1b`。该快照已重新构建 Editor、Development 与 Shipping。

- [memory-final](memory-final/provenance.json)：28 条真实 PIE 断言；[trial-final](trial-final/provenance.json)：54 次真实 PIE 断言；均绑定最终源码与资产摘要。
- [editor-final](editor-final/automation/index.json)：最终 Editor 构建后的 5 项 Core Automation 全部通过；[Python 最终重跑](python/python-release.log)：33 项通过。
- [input-packaged](input-packaged/provenance.json)：独立 Development EXE、26 项合成输入检查，四张原始截图已目视检查；没有启动编辑器、没有桌面鼠标输入或人工试玩。
- [Shipping gate](packages/shipping-marker-gate.json)：11 个开发标记在 Development 存在，在 Shipping 缺失。两包原始日志与完整分发文件清单位于 `packages/`。没有最终 Shipping 窗口/输入验收。

新增 24 个复制原件的来源、字节与 SHA-256 记在 `acceptance.json` 的 `finalCopiedFiles`；最初归档的 45 个原件仍由 `sources.json` 记录，不覆盖中间快照或失败证据。四张最终独立包截图为 [briefing](input-packaged/briefing.png)、[active](input-packaged/active.png)、[paused](input-packaged/paused.png)、[newattempt](input-packaged/newattempt.png)。

## 首次证据归档（保留中间快照）

本目录按来源复制本轮已完成的本地报告。原始日志、JSON、CSV 和 PNG 未重新编码或改写，复制后逐文件与来源比较 SHA-256；绝对来源路径、目标相对路径、大小、摘要及排除项见 [sources.json](sources.json)。这次归档没有启动 Unreal、重新执行测试或生成总验收结论。

| 目录 | 原始记录所支持的结果 | 源码绑定 |
| --- | --- | --- |
| [portable](portable/verification.json) | C++17 严格构建；347 条断言、0 失败；原始 smoke 两个 episode | core 摘要 `58809048a96e0c14e7b0d3ce99d0eacc2fe155eafb8fd0292490977770a3bc62` |
| [python](python/python-final.log) | 最后单独执行的 33 项 Python 单元测试，OK；portable 内较早的 Python 日志另行原样保留 | 原始日志没有源码摘要 |
| [memory](memory/provenance.json) | begun-play PIE、NullRHI；28 条具名断言；passed=true | UE 快照 A |
| [trial](trial/provenance.json) | begun-play PIE、NullRHI；54 次具名断言；passed=true | UE 快照 A |
| [rendered-smoke](rendered-smoke/provenance.json) | 两个真实渲染 episode，原始 JSON/CSV 保留；passed=true | UE 快照 A |
| [input-editor](input-editor/provenance.json) | UnrealEditor-Cmd `-game`；26 条引擎内合成输入断言；4 张 1280×720 原始渲染截图 | UE 快照 B |
| [build-editor-07](build-editor-07/build.log) | 最新 Editor Development 构建，Result: Succeeded | 原始构建日志没有独立源码摘要 |
| [core-automation-build-06](core-automation-build-06/automation/index.json) | 前一构建的 5 项 Core Automation：5 succeeded、0 failed | 原报告没有独立源码摘要；没有标成 build 07 重跑 |
| [failures](failures/README.md) | 首次编译、trial-01 和 input-01 的失败原件 | 保留各自原始状态及可用摘要 |

UE 快照 A 的源码摘要为 `94144c382b4d962219be62d81e89b6af97415b8b129fa105a07bf18531862d92`。快照 B 为后续字体修改后的 `583c3534f9f568fa665daad852914da40c376798495037ace6dbaffc24fefd24`。两者资产摘要均为 `fed2e9efde70c084ffa0c33d6392494952d78c9e7081b8b773b284c1979eeb1b`。这些值直接取自各运行的原始 provenance，不用归档时的文件状态倒填。相同 Git HEAD 不代表未提交源码相同；portable core 摘要与 UE 摘要的范围也不同。

四张截图是 [briefing.png](input-editor/briefing.png)、[active.png](input-editor/active.png)、[paused.png](input-editor/paused.png) 和 [newattempt.png](input-editor/newattempt.png)。它们来自引擎内合成输入与离屏渲染，没有使用桌面鼠标，也不代表人工可用性验证或打包可执行文件验收。渲染 smoke 是短时运行，不作为完整性能评估或策略对比。

本目录不包含 EXE、PDB、缓存或 Automation HTML 页面；报告原件仍保留在 `D:/AegisWork/Reports`。本次采集不生成 `acceptance.json`，打包后的总体验收由主流程另行完成。
