"""Verify configured shell compile ownership and optional unstripped symbols.

Configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON (Makefiles/Ninja). This is a
build-artifact check, not a source-list assertion. --nm adds an independent
macOS/Linux executable check; stripped binaries are not supported by that mode.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


class BoundaryError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise BoundaryError(message)


def check(build, testing, nm=None):
    database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    targets = {name: [] for name in (
        "marrow_editor_shell", "marrow_editor_shell_core", "marrow_editor_shell_smoke")}
    for entry in database:
        command = entry.get("command", " ".join(entry.get("arguments", [])))
        for target in targets:
            if re.search(r"CMakeFiles[/\\]" + target + r"\.dir[/\\]", command):
                targets[target].append((Path(entry["file"]).name, command))
    for target in ("marrow_editor_shell", "marrow_editor_shell_core"):
        entries = targets[target]
        require(entries, f"no compiler invocations for {target}")
        require(not any(name.startswith("shell_smoke") for name, _ in entries),
                f"smoke sources leaked into {target}")
        require(not any("MARROW_ENABLE_HEADLESS_SMOKE" in cmd for _, cmd in entries),
                f"headless dispatch definition leaked into {target}")
    require([name for name, _ in targets["marrow_editor_shell"]] == ["shell_main.cpp"],
            "product must compile only its host entry point")
    core = [name for name, _ in targets["marrow_editor_shell_core"]]
    require(core.count("shell_frame.cpp") == 1, "coordinator must compile once in the shared core")
    require("shell_main.cpp" not in core, "entry point leaked into the shared core")
    smoke = targets["marrow_editor_shell_smoke"]
    if testing:
        require(smoke, "BUILD_TESTING=ON lacks smoke compiler invocations")
        require(any(name == "shell_smoke.cpp" for name, _ in smoke), "missing actual smoke runner")
        require(any(name == "shell_smoke_frame_contract.cpp" for name, _ in smoke),
                "missing real-ImGui coordinator contract")
        require(all("MARROW_ENABLE_HEADLESS_SMOKE=1" in cmd for _, cmd in smoke),
                "smoke host lacks its private dispatch definition")
        require(not any(name == "shell_frame.cpp" for name, _ in smoke),
                "smoke recompiles rather than links the shared coordinator")
    else:
        require(not smoke, "BUILD_TESTING=OFF still compiles authoring smoke sources")
        require(not (build / "marrow_editor_shell_smoke").exists(),
                "unexpected smoke executable in the fresh product-only build")

    product = build / "marrow_editor_shell"
    require(product.is_file(), f"product executable is missing: {product}")
    if nm:
        def symbols(path):
            result = subprocess.run([nm, "-C", str(path)], capture_output=True,
                                    text=True, timeout=30, check=True)
            return result.stdout
        product_symbols = symbols(product)
        require("draw_shell_frame(" in product_symbols,
                "product does not link the production coordinator")
        require("run_headless_smoke(" not in product_symbols and
                "validate_shared_shell_frame_contract(" not in product_symbols,
                "product links authoring smoke symbols")
        if testing:
            smoke_symbols = symbols(build / "marrow_editor_shell_smoke")
            for symbol in ("draw_shell_frame(", "run_headless_smoke(",
                           "validate_shared_shell_frame_contract("):
                require(symbol in smoke_symbols, f"smoke executable lacks {symbol}")
    print(f"Shell build boundary passed: BUILD_TESTING={'ON' if testing else 'OFF'}; "
          f"core={len(core)} translation units, product=1, smoke={len(smoke)}; "
          f"symbol check={'enabled' if nm else 'not requested'}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--testing", choices=("on", "off"), required=True)
    parser.add_argument("--nm", help="nm or llvm-nm executable for unstripped macOS/Linux binaries")
    args = parser.parse_args()
    try:
        check(args.build_dir.resolve(), args.testing == "on", args.nm)
    except (BoundaryError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Shell build boundary FAILED: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
