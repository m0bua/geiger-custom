"""Run the serial uploader while hiding its per-page progress chatter."""

import re
import subprocess
import sys


PROGRESS = re.compile(
    r"\r?Wrote (?:and verified )?address 0x[0-9A-Fa-f]+ "
    r"\((?P<percent>[0-9.]+)%\)\s*"
)
next_progress = 10


def emit_line(line: str) -> None:
    global next_progress
    match = PROGRESS.search(line)
    if match:
        percent = float(match.group("percent"))
        while percent >= next_progress and next_progress <= 100:
            print(f"Wrote and verified {next_progress}%.", flush=True)
            next_progress += 10
        return
    if line:
        print(line, flush=True)


def main() -> int:
    global next_progress
    next_progress = 10
    if len(sys.argv) < 2:
        print("Missing serial uploader command.", file=sys.stderr)
        return 2
    process = subprocess.Popen(sys.argv[1:], stdout=subprocess.PIPE,
                               stderr=None, text=True, errors="replace")
    buffer = ""
    assert process.stdout is not None
    while True:
        character = process.stdout.read(1)
        if character == "":
            break
        if character in "\r\n":
            emit_line(buffer)
            buffer = ""
        else:
            buffer += character
    emit_line(buffer)
    return process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
