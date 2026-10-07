"""Add local, relocatable Copilot launch helpers to an existing Windows package.

Copies code only. The Windows-user DPAPI provider settings stay outside the package.
"""
import argparse
from pathlib import Path
import shutil
import sys


def install(windows, configuration):
    windows = Path(windows).resolve(strict=True)
    executable = ('AegisArena.exe' if configuration == 'Development'
                  else 'AegisArena-Win64-Shipping.exe')
    relative_game = 'AegisArena\\Binaries\\Win64\\' + executable
    if not (windows / 'AegisArena' / 'Binaries' / 'Win64' / executable).is_file():
        raise ValueError('The selected packaged game executable is missing')
    support = windows / 'Copilot'
    support.mkdir(exist_ok=False)
    for name in ('aegis_ai_config.py', 'launch_copilot.py'):
        shutil.copyfile(Path(__file__).parent / name, support / name)
    # Prefer this installation; after moving to another PC, try the Python launcher.
    preamble = ('@echo off\nsetlocal\n'
                f'set "AEGIS_PYTHON={sys.executable}"\n'
                'set "AEGIS_PY_ARGS="\n'
                '"%AEGIS_PYTHON%" -c "import sys, tkinter; sys.exit(sys.version_info < (3, 10))" >nul 2>nul\n'
                'if not errorlevel 1 goto python_ready\n'
                'set "AEGIS_PYTHON=py"\nset "AEGIS_PY_ARGS=-3"\n'
                'py -3 -c "import sys, tkinter; sys.exit(sys.version_info < (3, 10))" >nul 2>nul\n'
                'if not errorlevel 1 goto python_ready\n'
                'set "AEGIS_PYTHON=python"\nset "AEGIS_PY_ARGS="\n'
                'python -c "import sys, tkinter; sys.exit(sys.version_info < (3, 10))" >nul 2>nul\n'
                'if not errorlevel 1 goto python_ready\n'
                'echo Install Python 3.10 or newer with Tkinter to use the AI launcher.\n'
                'pause\nexit /b 2\n:python_ready\n')
    for name, args in (
            ('Play Aegis Copilot.cmd', f'--game-exe "%~dp0{relative_game}"'),
            ('Configure Aegis AI.cmd', '--configure')):
        body = (preamble + '"%AEGIS_PYTHON%" %AEGIS_PY_ARGS% "%~dp0Copilot\\launch_copilot.py" ' + args + '\n'
                'if errorlevel 1 (\necho Open "Configure Aegis AI.cmd", save settings, then launch again.\npause\n)\n')
        (windows / name).write_text(body, encoding='utf-8', newline='\r\n')
    (support / 'READ ME.txt').write_text(
        'AEGIS ARENA 1.3 / SQUAD COPILOT\n\n'
        '1. Open Configure Aegis AI.cmd, enter your provider key and save.\n'
        '2. Open Play Aegis Copilot.cmd. Existing settings are reused.\n'
        '3. Deploy with Enter. Tab opens the paused command deck.\n'
        '   Type a goal or choose a preset; Enter asks the model.\n'
        '   Tab or Esc returns to combat. Z / X / C take back command.\n'
        '4. Esc opens the pause menu; X or Quit leaves the game.\n'
        '   If the command deck is open, close it first.\n\n'
        'Python 3.10+ with Tkinter is required for these launch helpers.\n'
        'The API key is stored with Windows current-user DPAPI outside this folder.\n'
        'Each submitted goal makes one provider request and may incur a normal API fee.\n'
        'Cloud use requires internet. A failed request returns to Classic Guard.\n'
        'You can also run AegisArena.exe directly for the ordinary game.\n'
        'No local model or API credential is included in this package.\n', encoding='utf-8')
    print(f'Copilot launch helpers installed: {windows}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--windows', required=True, type=Path)
    parser.add_argument('--configuration', required=True, choices=('Development', 'Shipping'))
    arguments = parser.parse_args()
    install(arguments.windows, arguments.configuration)
