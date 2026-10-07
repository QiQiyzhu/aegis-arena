"""Build a concise Aegis Arena v2.2 submission cut from native v2.1 frames.

The gameplay image sequence is made of hard links to the preserved 1920x1080
native PNG capture. Only the editorial rail and static intro/outro are newly
rendered. Audio is selected from the verified v2.1 film at matching source
times, so the cut does not claim a new ruleset or a human playtest.
"""
from __future__ import annotations

import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

FPS = 30
NAVY, PANEL, WHITE, MUTED, MINT, AMBER, LINE = '#0d202b', '#173441', '#edf2ef', '#a4b7bb', '#73e4c6', '#efbd75', '#3c6670'
INTRO, OUTRO = 4.0, 4.0
# Capture time is gameplay-only. The source film has a 5 s intro before it.
CLIPS = [
    ('briefing', 0.0, 4.0, '中继目标与撤离条件', '先看目标，再进入战斗。'),
    ('secure', 4.0, 16.2, '01 / 占领中继', '敌人进圈会阻断推进；蓄能与超频共享同一份能量。'),
    ('upgrade1', 16.2, 24.733334, '阶段间 / 升级选择', '升级改变站位收益，选择会保留到后续阶段。'),
    ('transfer', 24.733334, 37.6, '02 / 转移中继', '从单点守住转为路线协作；队友指令服务当前目标。'),
    ('upgrade2', 48.433334, 56.96667, '第二次升级', '把已有机制叠成自己的打法，代价仍由资源承担。'),
    ('extract_start', 56.96667, 69.96667, '03 / 撤离阶段', '完成最后一段推进后，仍要清敌并由玩家进入撤离圈。'),
    ('extract_combat', 84.0, 95.0, '战斗节选', '预警、命中、击破和角色轮廓保持同一视觉层级。'),
    ('extract_result', 108.0, 121.866674, '撤离与结算', '结果页回收阶段、输出、承伤和能量花费。'),
]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()


def font(path: Path, size: int):
    return ImageFont.truetype(str(path), size)


def draw_bg(path: Path, title: str, note: str, regular: Path, bold: Path):
    im = Image.new('RGB', (1920, 1080), NAVY)
    d = ImageDraw.Draw(im)
    for x in range(20, 1920, 80):
        d.line((x, 0, x + 250, 1080), fill='#112d39', width=1)
    d.rectangle((34, 32, 42, 112), fill=MINT)
    d.text((62, 34), 'AEGIS ARENA', font=font(bold, 40), fill=WHITE)
    d.text((400, 46), 'PRISM FALL / v2.2 投递精剪版', font=font(regular, 23), fill=MINT)
    d.text((1648, 46), title, font=font(bold, 26), fill=WHITE)
    d.rectangle((36, 86, 1620, 978), outline='#537d80', width=2)
    d.rectangle((1644, 88, 1882, 978), fill=PANEL)
    d.rectangle((1644, 88, 1650, 978), fill=MINT)
    d.text((1670, 128), title, font=font(bold, 27), fill=WHITE)
    y = 214
    for line in [note, '真实引擎画面节选', '旁注只解释规则代价']:
        d.line((1670, y - 18, 1855, y - 18), fill=LINE, width=1)
        d.text((1670, y), line, font=font(regular, 20), fill=MUTED if line != note else WHITE)
        y += 70 if line == note else 52
    d.text((40, 1004), '真实实机节选 · 脚本输入驱动 · 未注入血量、伤害或胜负 · 非真人试玩结论', font=font(regular, 19), fill=MUTED)
    im.save(path)


def draw_intro(path: Path, regular: Path, bold: Path):
    im = Image.new('RGB', (1920, 1080), NAVY); d = ImageDraw.Draw(im)
    for x in range(20, 1920, 80): d.line((x, 0, x + 250, 1080), fill='#112d39', width=1)
    d.rectangle((40, 40, 48, 130), fill=MINT)
    d.text((90, 60), '作品集导览 / v2.2 投递精剪版', font=font(regular, 27), fill=AMBER)
    d.text((100, 215), 'AEGIS ARENA', font=font(bold, 100), fill=WHITE)
    d.text((108, 350), 'PRISM FALL / 战斗与关卡策划 Demo', font=font(bold, 43), fill=MINT)
    d.text((110, 445), '目标 → 战斗 → 升级 → 撤离 → 结算', font=font(bold, 45), fill=WHITE)
    d.text((110, 535), '真实 Unreal 实机画面 · 少量设计旁注 · 约 92 秒', font=font(regular, 29), fill=MUTED)
    for i, (n, label) in enumerate([('03', '行动阶段'), ('02', '升级节点'), ('01', '完整结算')]):
        x = 110 + i * 560; d.rectangle((x, 680, x + 488, 860), fill=PANEL)
        d.text((x + 28, 700), n, font=font(bold, 65), fill=MINT)
        d.text((x + 30, 790), label, font=font(regular, 29), fill=WHITE)
    d.text((40, 1004), '真实实机节选 · 画面按时间顺序剪辑 · 非真人试玩结论', font=font(regular, 20), fill=MUTED)
    im.save(path)


def draw_outro(path: Path, regular: Path, bold: Path):
    im = Image.new('RGB', (1920, 1080), NAVY); d = ImageDraw.Draw(im)
    for x in range(20, 1920, 80): d.line((x, 0, x + 250, 1080), fill='#112d39', width=1)
    d.rectangle((40, 40, 48, 130), fill=MINT)
    d.text((90, 60), '投递信息 / 作品集附件', font=font(regular, 27), fill=AMBER)
    d.text((100, 205), '一段可读的战斗闭环。', font=font(bold, 63), fill=WHITE)
    d.text((106, 320), 'Aegis Arena · PRISM FALL / v2.2 投递精剪版', font=font(bold, 34), fill=MINT)
    d.text((108, 430), '战斗反馈、目标推进、队友协作和资源取舍', font=font(regular, 32), fill=WHITE)
    d.text((108, 520), '项目基于 v2.1 实机录制；本次 v2.2 指视频结构与呈现优化。', font=font(regular, 26), fill=AMBER)
    d.text((108, 640), '范围：真实录制节选，不替代真人试玩、平衡性或整体 AI 结论。', font=font(regular, 25), fill=MUTED)
    d.text((108, 710), 'AI 辅助实现与制作；个人职责以设计说明为准。', font=font(regular, 24), fill=MUTED)
    d.text((40, 1004), 'Aegis Arena v2.2 · 1080p / 30fps · 实机画面为主', font=font(regular, 20), fill=MUTED)
    im.save(path)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument('--capture', type=Path, required=True)
    p.add_argument('--source-video', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--ffmpeg', type=Path, required=True)
    p.add_argument('--font-dir', type=Path, default=Path('C:/Windows/Fonts'))
    args = p.parse_args()
    capture = args.capture.resolve(); src_video = args.source_video.resolve(); out = args.output.resolve(); work = args.work.resolve()
    if not capture.is_dir() or not src_video.is_file() or not args.ffmpeg.is_file(): raise SystemExit('missing input')
    if work.exists(): shutil.rmtree(work)
    (work / 'frames').mkdir(parents=True)
    work.mkdir(parents=True, exist_ok=True)
    regular, bold = args.font_dir / 'msyh.ttc', args.font_dir / 'msyhbd.ttc'
    if not regular.is_file() or not bold.is_file(): raise SystemExit('Microsoft YaHei fonts missing')

    # Hard-link source PNGs so the output retains native frames without duplicating GBs of images.
    frame_manifest = []
    output_frame = 0
    out_time = 0.0
    for key, start, end, title, note in CLIPS:
        first, last = round(start * FPS), round(end * FPS)
        for source_index in range(first, last):
            source = capture / 'frames' / f'frame-{source_index:06d}.png'
            if not source.is_file(): raise SystemExit(f'missing native frame {source}')
            dest = work / 'frames' / f'frame-{output_frame:06d}.png'
            os.link(source, dest)
            frame_manifest.append({'outputFrame': output_frame, 'sourceFrame': source_index,
                                   'sourceSeconds': source_index / FPS, 'outputSeconds': out_time})
            output_frame += 1; out_time += 1 / FPS
        frame_manifest.append({'chapter': key, 'sourceStart': start, 'sourceEnd': end,
                               'outputStart': out_time - (last-first)/FPS, 'outputEnd': out_time,
                               'title': title, 'note': note})

    (work / 'native-frame-manifest.json').write_text(json.dumps(frame_manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    for key, start, end, title, note in CLIPS:
        draw_bg(work / f'bg-{key}.png', title, note, regular, bold)
    draw_intro(work / 'intro.png', regular, bold); draw_outro(work / 'outro.png', regular, bold)
    bg_concat = work / 'backgrounds.txt'
    with bg_concat.open('w', encoding='utf-8') as f:
        for key, start, end, *_ in CLIPS:
            f.write(f"file '{(work / f'bg-{key}.png').as_posix()}'\n")
            f.write(f'duration {end-start:.9f}\n')
        f.write(f"file '{(work / f'bg-{CLIPS[-1][0]}.png').as_posix()}'\n")
    # Intro, gameplay, outro video; audio selects the same source sections. The source film has 5s intro.
    gameplay = sum(end-start for _, start, end, *_ in CLIPS)
    total = INTRO + gameplay + OUTRO
    audio_segments = [(0.0, INTRO)] + [(5.0+start, 5.0+end) for _, start, end, *_ in CLIPS] + [(133.866673-OUTRO, 133.866673)]
    filt = []
    concat_audio = []
    for i, (start, end) in enumerate(audio_segments):
        filt.append(f'[4:a]atrim=start={start}:end={end},asetpts=PTS-STARTPTS[a{i}]'); concat_audio.append(f'[a{i}]')
    filt.append(''.join(concat_audio) + f'concat=n={len(audio_segments)}:v=0:a=1[a]')
    native = f'[0:v]scale=1580:889:flags=lanczos,setsar=1,setpts=PTS-STARTPTS[native]'
    filt.extend([native,
                 '[1:v]fps=30,tpad=stop_mode=clone:stop_duration=1,setpts=PTS-STARTPTS[bg]',
                 f'[bg][native]overlay=40:90:eof_action=repeat,trim=end_frame={output_frame},setpts=PTS-STARTPTS[game]',
                 f'[2:v]fps=30,trim=duration={INTRO},setpts=PTS-STARTPTS[intro]',
                 f'[3:v]fps=30,trim=duration={OUTRO},setpts=PTS-STARTPTS[outro]',
                 '[intro][game][outro]concat=n=3:v=1:a=0,format=yuv420p[v]'])
    cmd = [str(args.ffmpeg), '-hide_banner', '-y', '-threads', '2', '-filter_complex_threads', '2',
           '-framerate', '30', '-start_number', '0', '-i', work/'frames/frame-%06d.png',
           '-f', 'concat', '-safe', '0', '-i', bg_concat, '-loop', '1', '-framerate', '30', '-i', work/'intro.png',
           '-loop', '1', '-framerate', '30', '-i', work/'outro.png', '-i', src_video,
           '-filter_complex', ';'.join(filt), '-map', '[v]', '-map', '[a]', '-c:v', 'libx264', '-preset', 'medium',
           '-threads', '2', '-crf', '20', '-pix_fmt', 'yuv420p', '-r', '30', '-fps_mode', 'cfr',
           '-frames:v', str(round(total * FPS)), '-c:a', 'aac', '-b:a', '160k', '-ar', '48000', '-movflags', '+faststart', '-shortest', out]
    out.parent.mkdir(parents=True, exist_ok=True)
    log = work / 'encode.log'
    result = subprocess.run(cmd, stdout=log.open('w', encoding='utf-8'), stderr=subprocess.STDOUT)
    if result.returncode: raise SystemExit(f'ffmpeg failed; see {log}')
    # Keep an auditable manifest next to the video.
    edl = {'schemaVersion': 1, 'editorialVersion': '2.2', 'sourcePresentation': '2.1 native capture',
           'sourceCapture': str(capture), 'sourceVideo': str(src_video), 'nativeFrames': output_frame,
           'gameplaySeconds': gameplay, 'introSeconds': INTRO, 'outroSeconds': OUTRO,
           'durationSeconds': total, 'video': {'path': str(out), 'bytes': out.stat().st_size, 'sha256': sha256(out)},
           'disclosure': 'Chronological real gameplay selections from a verified v2.1 native capture. v2.2 denotes editorial structure/presentation optimization; no new ruleset or human playtest claim.',
           'clips': [{'id': key, 'sourceStart': start, 'sourceEnd': end, 'reason': note} for key, start, end, title, note in CLIPS]}
    (work / 'video-provenance.json').write_text(json.dumps(edl, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(edl['video'], ensure_ascii=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
