# 保留的失败尝试

以下均为失败原件，后续成功记录不改变它们的状态。来源、摘要和复制路径见上一级 [sources.json](../sources.json)。

- **[compile-01](compile-01/build.log)**：首次构建因 `AegisLab.cpp` 的局部变量 `Role` 遮蔽 `AActor::Role`，触发 C4458，结尾为 `Result: Failed (OtherCompilationError)`。后续改名修复，最新 [build-editor-07](../build-editor-07/build.log) 构建成功。该失败构建没有原生功能测试结果，也没有原始源码摘要。
- **[trial-01](trial-01/provenance.json)**：编辑器主页联网探测触发 `LogHttp` 的 `https://www.google.com/generate_204` 超时，在 Automation 报告中产生失败；虽然进程退出码是 0，严格门禁保存了 `passed=false`。后续测试启动命令显式关闭 HomeScreen，保留对警告/失败的严格要求，[trial-final](../trial/provenance.json) 的 54 次断言和完整门禁通过。原 `index.json`、stdout 和 engine.log 均未删改。
- **[input-01](input-01/provenance.json)**：engine.log 明确记录 `FAILED: navigation did not become ready within 30 seconds`；wrapper 最终达到 120 秒超时并保存 `passed=false`，未产生完整输入报告或截图。后续将导航就绪条件改为检查双方实际出生区域之间的完整路径，并使探针模式中的启动失败有界退出。[input-font](../input-editor/provenance.json) 记录后续 26 条输入断言与四张截图通过，不能据此把这次失败标为通过。

这些目录保存的是本轮指定的三次失败，未声称穷尽全部历史诊断。较早其他失败、被排除的 HTML 和本机完整目录仍在各来源位置。
