#!/usr/bin/env python3

"""Run Zig's Windows linker with release dead-code elimination enabled."""

from __future__ import annotations

import shlex
import subprocess
import sys


def fail(message: str, output: str = "") -> None:
    print(f"link_release.py: {message}", file=sys.stderr)
    if output:
        print(output, file=sys.stderr, end="" if output.endswith("\n") else "\n")
    raise SystemExit(1)


def main() -> None:
    if len(sys.argv) < 3:
        fail("expected a Zig C link command")

    zig_command = sys.argv[1:]
    probe = subprocess.run(
        [*zig_command, "-###"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    probe_output = probe.stdout + probe.stderr
    if probe.returncode != 0:
        fail("Zig could not prepare the link command", probe_output)

    linker_line = next(
        (line for line in reversed(probe_output.splitlines()) if line.startswith("lld-link ")),
        None,
    )
    if linker_line is None:
        fail("Zig did not report an lld-link command", probe_output)

    linker_args = shlex.split(linker_line)
    if not linker_args or linker_args[0] != "lld-link":
        fail("unexpected linker command", linker_line)

    # Zig 0.16 always enables /DEBUG for Windows links so it can emit a PDB.
    # lld-link consequently disables its normal release defaults and retains
    # every COMDAT in Zig's compiler runtime. Re-enable those optimizations
    # explicitly while invoking the linker bundled with this same Zig binary.
    command = [zig_command[0], "lld-link", "/OPT:REF", "/OPT:ICF", *linker_args[1:]]
    try:
        subprocess.run(command, check=True)
    except FileNotFoundError:
        fail(f"could not execute Zig compiler {zig_command[0]!r}")
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from error


if __name__ == "__main__":
    main()
