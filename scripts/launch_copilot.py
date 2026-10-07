"""Launch the actual game with decrypted provider settings only in its environment."""
import argparse
from pathlib import Path
import subprocess
import sys
from aegis_ai_config import build_environment, configure


def build_launch_command(game_exe, extras=()):
    game = Path(game_exe)
    if not game.is_absolute() or not game.is_file() or game.suffix.lower() != ".exe":
        raise ValueError("Choose an existing absolute game .exe path")
    if game.name.lower().startswith("unrealeditor"):
        raise ValueError("Choose the packaged game executable")
    if any(not isinstance(arg, str) or "\0" in arg for arg in extras):
        raise ValueError("Invalid game argument")
    return [str(game.resolve()), *extras]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-exe", type=Path)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--configure", action="store_true")
    parser.add_argument("extras", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    if args.configure:
        configure(args.config)
    if args.game_exe is None:
        if args.configure:
            return 0
        parser.error("--game-exe is required unless opening --configure")
    extras = args.extras[1:] if args.extras[:1] == ["--"] else args.extras
    try:
        command = build_launch_command(args.game_exe, extras)
        environment = build_environment(args.config)
        subprocess.Popen(command, env=environment, cwd=Path(command[0]).parent)
        return 0
    except (OSError, ValueError):
        print("Could not launch. Check the game path and saved provider settings with --configure.", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
