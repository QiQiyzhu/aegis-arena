"""Assemble local upload attachments and the exact validated native game package."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile
from run_portfolio_probe import binary_snapshot

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for part in iter(lambda: f.read(1024*1024), b''):
            h.update(part)
    return h.hexdigest()

def info(path):
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha(path))

def archive(target, files):
    if target.exists():
        raise FileExistsError(target)
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for source, name in files:
            z.write(source, name)
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        assert len(z.namelist()) == len(files)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture', type=Path, required=True)
    p.add_argument('--probe', type=Path, required=True)
    p.add_argument('--video', type=Path, required=True, help='video QA/output directory')
    p.add_argument('--documents', type=Path, required=True)
    p.add_argument('--document-review', type=Path, required=True)
    p.add_argument('--package', type=Path, required=True, help='validated Windows folder')
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    capture = read(args.capture/'provenance.json')
    probe = read(args.probe/'provenance.json')
    video = read(args.video/'video-provenance.json')
    review = read(args.document_review)
    assert capture['passed'] is True and capture['launch'] == 'packaged-development'
    assert capture.get('resultQuitEvidence'), 'Final package must verify normal X from the result page'
    assert probe['passed'] is True and probe['nativeAssertions'] == 25
    assert video['passed'] is True and not video['preview'] and video['fullDecodePassed'] is True
    assert review['passed'] is True and review['allPagesReviewed'] is True
    package_binding = binary_snapshot(args.package/'AegisArena.exe')
    assert package_binding == capture['binariesAfter'] == probe['binariesAfter']
    movie = args.video/'Aegis-Arena-Portfolio-v1.5.mp4'
    assert movie.stat().st_size < 300_000_000 and sha(movie) == video['video']['sha256']
    args.output.mkdir(parents=True, exist_ok=True)
    files = [movie, args.documents/'Aegis_Arena_System_Design_v1.5.pdf',
             args.documents/'Aegis_Arena_System_Design_v1.5.docx']
    delivered = []
    for file in files:
        if file.suffix in ('.docx', '.pdf'):
            assert review['outputHashes'][file.name] == sha(file)
        dest = args.output/file.name
        if dest.exists() and sha(dest) != sha(file):
            raise FileExistsError(dest)
        shutil.copy2(file, dest)
        delivered.append(dest)
    readme = args.output/'先读我-投递与试玩.txt'
    readme.write_text(
        'Aegis Arena · PRISM RELAY / v1.5\n系统／综合策划作品展示\n\n'
        '投递建议：将 MP4 视频与 PDF 设计说明直接作为简历附件上传；DOCX 是可编辑副本。\n'
        'Windows 游戏包单独提供，适合需要试玩的面试环节。无需网盘。\n\n'
        f'视频：{video["encodedDurationSeconds"]:.2f} 秒，{movie.stat().st_size/1_000_000:.2f} MB，小于 300 MB。\n'
        '视频是完整 Unreal 原生画面，使用正常输入驱动自动玩家；旁注为后期讲解。\n'
        '音效由引擎真实事件时间重混原创 WAV，不是硬件录音；没有生成假游戏帧。\n'
        '这不是真人试玩、胜率统计或硬件性能基准。AI 协助实现与制作，个人职责按实际确认。\n\n'
        '试玩：解压 Windows 游戏包，保留完整文件夹，双击 PLAY-Aegis-Portfolio.cmd。\n'
        'Enter 开始，WASD 移动，鼠标左键射击，Space 冲刺，Q 脉冲，E 修复。\n'
        'Z/X/C 队友命令，局间 1/2/3 选升级，R 重开，Esc 或 P 菜单，菜单或结算页 X 退出。\n'
        '首局选 Guided：启动中继 → 顺序传输 → 清敌并亲自撤离。\n'
        '无需编辑器或 API Key。若新电脑缺 VC++ 运行库，安装包内 vc_redist.x64.exe。\n\n'
        '简历中的游戏经历需填本人真实游戏时长、熟悉系统和体验分析；本交付没有代编。\n'
        '历史云端 Copilot 与 Decision Lab 的证据独立保留，本版不新增 RL 训练成果。\n', encoding='utf-8-sig')
    delivered.append(readme)
    inventory = args.output/'文件清单.txt'
    inventory.write_text('Aegis Arena v1.5 / 直接投递附件\n\n' + '\n\n'.join(
        f'{f.name}\n{f.stat().st_size:,} bytes / {f.stat().st_size/1_000_000:.2f} MB\nSHA256 {sha(f)}'
        for f in delivered) + '\n', encoding='utf-8-sig')
    delivered.append(inventory)
    windows = args.package.resolve()
    assert (windows/'AegisArena.exe').is_file()
    launch = windows/'PLAY-Aegis-Portfolio.cmd'
    launch.write_text('@echo off\ncd /d "%~dp0"\nstart "" "%~dp0AegisArena.exe" -AegisPortfolio -windowed -ResX=1280 -ResY=720 -d3d11\n', encoding='ascii')
    guides = windows/'Portfolio'
    guides.mkdir(exist_ok=True)
    for name in ('portfolio-play.md', 'portfolio-cpp-guide.md', 'portfolio-v1.5-references.md'):
        shutil.copy2(ROOT/'docs'/name, guides/name)
    shutil.copy2(ROOT/'ASSETS.md', guides/'ASSETS.md')
    shutil.copy2(ROOT/'LICENSE', guides/'LICENSE')
    for file in delivered[1:]:
        shutil.copy2(file, guides/file.name)
    game_files = []
    for f in sorted(windows.rglob('*')):
        rel = f.relative_to(windows)
        if not f.is_file() or 'Saved' in rel.parts or f.suffix.lower() in ('.pdb', '.log', '.tmp') or f.name.startswith('Manifest_'):
            continue
        game_files.append((f, 'Windows/'+rel.as_posix()))
    game_zip = args.output/'Aegis-Arena-v1.5-Windows.zip'
    app_zip = args.output/'Aegis-Arena-v1.5-Application.zip'
    archive(game_zip, game_files)
    archive(app_zip, [(f, f.name) for f in delivered])
    record = dict(passed=True, delivery=[info(f) for f in delivered+[game_zip,app_zip]],
                  packageDirectory=str(windows), packageFiles=[dict(info(f), archivePath=n) for f,n in game_files],
                  validation=dict(capture=info(args.capture/'provenance.json'), probe=info(args.probe/'provenance.json'),
                                  video=info(args.video/'video-provenance.json'), documents=info(args.document_review)),
                  configuration='Development', humanPlaytest=False, reinforcementLearningTraining=False,
                  packageExclusions=['PDB symbols','Saved runtime files','log/tmp files','staging manifests'])
    (args.output/'delivery-manifest.json').write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'passed':True, 'files':record['delivery']}, ensure_ascii=True, indent=2))

if __name__ == '__main__':
    main()
