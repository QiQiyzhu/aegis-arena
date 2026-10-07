"""Package reviewed v2 artifacts for direct attachment; never upload or publish."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import zipfile


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('package', 'video', 'pdf', 'docx', 'evidence', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--guide', type=Path)
    p.add_argument('--case', type=Path)
    p.add_argument('--recording-case', type=Path)
    p.add_argument('--references', type=Path)
    a = p.parse_args()
    package = a.package.resolve()
    assert (package/'AegisArena.exe').is_file(), 'Missing Windows launcher'
    assert (package/'AegisArena/Binaries/Win64/AegisArena.exe').is_file(), 'Missing game payload'
    for path in (a.video, a.pdf, a.docx, a.evidence):
        assert path.is_file() and path.stat().st_size > 0, str(path)
    assert a.video.stat().st_size < 300_000_000, 'Video exceeds attachment limit'
    evidence = json.loads(a.evidence.read_text(encoding='utf-8-sig'))
    assert evidence.get('deliveryReady') is True, 'Acceptance review required'
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    artifacts = {}
    for source, name in ((a.video, 'Aegis-Arena-v2.0-Demo.mp4'),
                         (a.pdf, 'Aegis-Arena-v2.0-Design.pdf'),
                         (a.docx, 'Aegis-Arena-v2.0-Design.docx'),
                         (a.evidence, 'acceptance.json')):
        dest = out/name
        shutil.copy2(source, dest)
        assert sha(source) == sha(dest)
        artifacts[name] = {'bytes': dest.stat().st_size, 'sha256': sha(dest)}
    for source, name in ((a.guide, 'Aegis-Arena-v2.0-Cpp-Guide.md'),
                         (a.case, 'Aegis-Arena-v2.0-AI-Case.md'),
                         (a.recording_case, 'portfolio-v2-recording-case.md'),
                         (a.references, 'v2-reference-and-direction.md')):
        if source:
            assert source.is_file()
            dest = out/name
            # Standalone attachments retain project-relative references as text:
            # their original repository siblings are not present in this bundle.
            body = source.read_text(encoding='utf-8-sig')
            body = re.sub(r'\[([^\]]+)\]\((?!https?://|[A-Za-z]:[/\\])([^)]*)\)',
                          lambda m: f'{m[1]}（项目内路径：`{m[2]}`）', body)
            dest.write_text(body, encoding='utf-8')
            artifacts[name] = {'bytes': dest.stat().st_size, 'sha256': sha(dest)}
    readme = '''Aegis Arena v2.0 / PRISM FALL

面向系统／综合策划的原生 Unreal C++ 作品集。
投递附件：Demo.mp4、Design.pdf；Design.docx 为可编辑版本。
视频小于 300 MB。视频来自真实原生运行；演示使用自动按键，非真人测试。
音效按引擎实际触发事件混音。未进行 RL 训练，不声称获奖。

试玩：解压 Windows.zip 整个目录，双击 Start-Aegis-Arena-v2.bat。
请勿只移动 EXE。首次启动可能需要同包 Engine/Extras/Redist/en-us 的运行库。
本包为 Windows Development 配置，保留 F1 诊断功能。
作品集启动器固定使用种子1101，与正式演示场景一致；R按相同种子重开。
可选：Start-Decision-Lab.bat 打开独立的 AI 决策实验室，查看实际 Utility 与 EQS。
实验室沿用其原有规则与画面，不与 PRISM FALL 的战术策略评测混合。

Enter 开始；WASD 移动；鼠标瞄准；左键连射；右键蓄能0.7秒后松开；
Space 冲刺；Q 消耗35能量击退；E 根据实际治疗收取8-40能量；
F 在中继圈内消耗35能量，超载6秒推进x2（争夺仍耗时）；
V 在简报或首次升级页选择第二阶段路线；1/2/3选升级；
Z 跟随掩护，X 集火，C 向鼠标位置集结；Esc/P 菜单，R 重开。
退出：Esc打开菜单再按X；结算页直接按X。
完成：占领中继且清敌 → 依次连接两个中继且清敌 → 清敌后玩家进入撤离圈。

设计参考、实际改进证据、测试范围和C++调用链见设计文档与验收清单。
另附 portfolio-v2-recording-case.md 与 v2-reference-and-direction.md，保留录制工具失败和完整参考来源。
候选人的游戏经历与实际职责应按本人事实补充，不能将参考研究写成亲自游玩经历。
'''
    (out/'README-先读我.txt').write_text(readme, encoding='utf-8-sig')
    batch = '@echo off\r\ncd /d "%~dp0"\r\nstart "Aegis Arena v2.0" "AegisArena.exe" -AegisV2 -AegisPortfolioSeed=1101 -d3d11 -windowed -ResX=1600 -ResY=900\r\n'
    lab_batch = '@echo off\r\ncd /d "%~dp0"\r\nstart "Aegis Decision Lab" "AegisArena.exe" -AegisDecisionLab -AegisLabPolicy=improved -AegisLabEnemies=2 -AegisLabSeed=2001 -AegisLabDuration=60 -AegisLabLayout=0 -d3d11 -windowed -ResX=1280 -ResY=720\r\n'
    game_zip = out/'Aegis-Arena-v2.0-Windows.zip'
    excluded = []
    with zipfile.ZipFile(game_zip, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                relative = path.relative_to(package)
                if 'saved' in [part.casefold() for part in relative.parts] or path.suffix.casefold() == '.pdb':
                    excluded.append(relative.as_posix())
                    continue
                z.write(path, 'Aegis-Arena-v2.0/' + relative.as_posix())
        z.writestr('Aegis-Arena-v2.0/Start-Aegis-Arena-v2.bat', batch)
        z.writestr('Aegis-Arena-v2.0/Start-Decision-Lab.bat', lab_batch)
        z.writestr('Aegis-Arena-v2.0/README.txt', readme.encode('utf-8-sig'))
    application = out/'Aegis-Arena-v2.0-Application.zip'
    with zipfile.ZipFile(application, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name in (*artifacts.keys(), 'README-先读我.txt'):
            z.write(out/name, name)
    for path in (application, game_zip):
        with zipfile.ZipFile(path) as z:
            assert z.testzip() is None
            count = len(z.infolist())
        artifacts[path.name] = {'bytes': path.stat().st_size, 'sha256': sha(path), 'entries': count, 'crcPassed': True}
    manifest = {'schemaVersion': 2, 'version': '2.0.0', 'packageSource': str(package),
                'videoUnder300MB': True, 'artifacts': artifacts,
                'excludedBuildFiles': excluded, 'exclusionReason': 'Local generated Saved state and optional debugger PDB symbols; original build preserved'}
    (out/'delivery-manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    print(json.dumps({'output': str(out), 'videoBytes': a.video.stat().st_size, 'archivesVerified': True}, ensure_ascii=True))


if __name__ == '__main__':
    main()
