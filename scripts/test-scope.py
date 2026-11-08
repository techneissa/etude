#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Which unit-test files a diff touches, for `just test` (the full run is `just test --all`).

    python3 -I scripts/test-scope.py [--base REF | --since REF]

The diff is the working tree against the merge base with REF (default origin/main): committed,
staged, unstaged and untracked files. With --since REF it is the working tree against REF itself
(HEAD for uncommitted work, the last pushed commit for a fix round), so a round tests only its own
edits. A test file is in scope when
  - it is named after a changed source (foo.cpp or foo.hpp: foo_test.cpp, foo_*_test.cpp);
  - it #includes a changed header directly, or, for a changed foo.cpp, the foo.hpp beside it (the
    code the header declares changed), or names a changed file by its path (the lints that read
    sources) or, for data under tests/ and scripts/test-data, by its file name;
  - it is itself changed;
  - it is a guard: a lint that scans the sources, always run.
Everything runs instead (mode `all`) when the scope cannot be worked out (no REF, no git) or when
CMake, the test main (`tests/main.cpp`), a dependency or a widely shared header changed (tokens,
kit, kit_internal, or a header that more than WIDE test files include). A CMakeLists.txt diff that
only adds or removes lines naming source or test files (foo.cpp, foo_test.cpp) does not count: the
files it names are in the diff themselves.

`just test` prints one line from this script's result, `test: scoped N test files: <file> (<why>);
... (M test cases)`, runs the size checks every time (code files and the assistant instructions
file) and settings-check only when settings code changed (settings_*, session_state*, pages*,
page_ids, scripts/settings-check.sh, tests/data/edge-pickup.mid). `just test --all` (or
`just test-all`) is the full run plus settings-check.

Every tests/*_test.cpp must be listed in tests/CMakeLists.txt, or it would never run: when one
is not, nothing runs and the exit status is 3.

Prints four lines: the mode (`scoped` or `all`), the doctest --source-file filter (empty for
`all`), `settings` or `no-settings` (settings-check runs only for settings code), and one line
saying which test files run and why.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parent.parent

# Lints that read the source tree: they run on every diff.
GUARDS = ("ui_token_lint_test", "accidental_guard_test", "touch_fire_guard_test")

# Headers every screen draws with: a change there is a change everywhere (techneissa, 2026-10-20).
SHARED_HEADERS = ("app/ui/tokens.hpp", "app/ui/kit.hpp", "app/ui/kit_internal.hpp")

# A header that more than this many test files include directly is shared too.
WIDE = 60

# A change to one of these can change what every test builds or links.
BUILD_FILES = re.compile(r"(^|/)(CMakeLists\.txt|CMakePresets\.json|vcpkg\.json)$|\.cmake$|^cmake/|^tests/main\.cpp$")

SOURCE_SUFFIXES = (".cpp", ".hpp", ".h", ".cc", ".mm")

# A CMake list entry: a source or test file name, with the closing parenthesis of its command.
LIST_ENTRY = re.compile(r"^[\w./+-]+\.(cpp|hpp|h|cc|mm)\)?$")


def is_list_only(lines):
    """True when every changed line of a CMakeLists.txt only names source or test files."""
    return all(all(LIST_ENTRY.match(tok) for tok in line.split()) for line in lines)

# settings-check draws every settings page: it runs when settings code, the check or its song
# changed.
SETTINGS_CODE = re.compile(
    r"(^|/)(settings_[a-z_]+|session_state[a-z_]*|pages[a-z_]*|page_ids)\.(cpp|hpp)$")
SETTINGS_FILES = ("scripts/settings-check.sh", "tests/data/edge-pickup.mid")


def is_settings_change(path):
    if path in SETTINGS_FILES:
        return True
    return bool(SETTINGS_CODE.search(path)) and not path.startswith("tests/")

# Where non-code inputs of the tests live; a test names them by file name.
DATA_DIRS = ("tests/data/", "tests/catalogue/", "scripts/test-data/", "deploy/")

INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


def stem_of(path):
    name = path.rsplit("/", 1)[-1]
    return name.rsplit(".", 1)[0]


def named_after(stem, test_stem):
    """foo -> foo_test, foo_<anything>_test."""
    return test_stem == stem + "_test" or (
        test_stem.startswith(stem + "_") and test_stem.endswith("_test"))


def plan(changed, tests, repo_files, cmake_lines=None):
    """The scope of a diff.

    changed: repo-relative paths of changed (or deleted) files.
    tests: {test stem: its text} for every tests/*_test.cpp.
    repo_files: every repo-relative path, to resolve an #include to its header.
    cmake_lines: {CMakeLists.txt path: its added and removed lines}; a list whose lines all
    name source or test files does not widen the scope.
    Returns {"mode": "scoped" | "all", "why": {test stem: reason}, "settings": bool,
    "all_reason": str}.
    """
    why = {}
    settings = any(is_settings_change(c) for c in changed)

    def add(test_stem, reason):
        why.setdefault(test_stem, reason)

    cmake_lines = cmake_lines or {}
    for c in changed:
        if c.endswith("CMakeLists.txt") and c in cmake_lines and is_list_only(cmake_lines[c]):
            continue
        if BUILD_FILES.search(c):
            return {"mode": "all", "why": {}, "settings": True, "all_reason": f"{c} changed"}
        if c in SHARED_HEADERS:
            return {"mode": "all", "why": {}, "settings": True, "all_reason": f"{c} is shared"}

    headers_by_name = {}
    for p in repo_files:
        if p.endswith((".hpp", ".h")):
            headers_by_name.setdefault(p.rsplit("/", 1)[-1], []).append(p)

    def resolve(include):
        # "../app/x.hpp" from a test is app/x.hpp.
        while include.startswith("../"):
            include = include[3:]
        base = include.rsplit("/", 1)[-1]
        return [p for p in headers_by_name.get(base, ()) if p == include or p.endswith("/" + include)]

    includes = {}
    for t, text in tests.items():
        includes[t] = {p for inc in INCLUDE.findall(text) for p in resolve(inc)}

    for g in GUARDS:
        if g in tests:
            why[g] = "guard"

    for c in changed:
        stem = stem_of(c)
        if c.startswith("tests/") and c.endswith("_test.cpp") and stem in tests:
            add(stem, "changed")
        if c.endswith(SOURCE_SUFFIXES):
            for t in tests:
                if named_after(stem, t):
                    add(t, f"name of {c.rsplit('/', 1)[-1]}")
        # A header changed, or the source of a header: the tests that include the header.
        headers = [c] if c.endswith((".hpp", ".h")) else (
            [p for p in headers_by_name.get(stem + ".hpp", ())] if c.endswith(".cpp") else [])
        for header in headers:
            includers = [t for t in tests if header in includes[t]]
            if len(includers) > WIDE:
                return {"mode": "all", "why": {}, "settings": True,
                        "all_reason": f"{header} is included by {len(includers)} test files"}
            for t in includers:
                add(t, f"includes {header.rsplit('/', 1)[-1]}")
        # A test that names the file by its path (a lint reading sources) or, for data, by name.
        key = c.rsplit("/", 1)[-1] if c.startswith(DATA_DIRS) else c
        if not c.endswith(".md"):
            for t, text in tests.items():
                if key in text:
                    add(t, f"names {key}")
    return {"mode": "scoped", "why": why, "settings": settings, "all_reason": ""}


def git(*args):
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def changed_files(base_ref, since=None):
    """(paths, {CMakeLists.txt: changed lines}, None) or (None, None, why the diff is unknown)."""
    if since:
        base = git("rev-parse", "--verify", since + "^{commit}")
    else:
        base = git("merge-base", base_ref, "HEAD")
    if base is None:
        return None, None, f"no {since or base_ref}"
    base = base.strip()
    tracked = git("diff", "--name-only", base)
    untracked = git("ls-files", "--others", "--exclude-standard")
    if tracked is None or untracked is None:
        return None, None, "git diff failed"
    cmake_lines = {}
    for path in tracked.split("\n"):
        if path.endswith("CMakeLists.txt"):
            diff = git("diff", "-U0", base, "--", path)
            if diff is not None:
                cmake_lines[path] = [l[1:] for l in diff.split("\n")
                                     if l[:1] in "+-" and l[:3] not in ("+++", "---")]
    return sorted(set(tracked.split("\n") + untracked.split("\n")) - {""}), cmake_lines, None


def unlisted_tests(tests, cmake_text):
    """Test stems that tests/CMakeLists.txt does not list (they would never be built or run)."""
    listed = set(re.findall(r"(?<![\w/])(\w+_test)\.cpp", cmake_text))
    return sorted(set(tests) - listed)


def load_tests():
    tests = {}
    for p in sorted((ROOT / "tests").glob("*_test.cpp")):
        tests[p.stem] = p.read_text(encoding="utf-8", errors="replace")
    return tests


def describe(result):
    why = result["why"]
    if not why:
        return "scoped: no test file"
    groups = {}
    for t, reason in sorted(why.items()):
        groups.setdefault(reason, []).append(t)
    parts = []
    for reason in sorted(groups, key=lambda r: (r == "guard", r)):
        parts.append(f"{', '.join(groups[reason])} ({reason})")
    return f"scoped {len(why)} test files: " + "; ".join(parts)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--since", metavar="REF",
                    help="diff against REF itself instead of the merge base with --base")
    args = ap.parse_args(argv)
    tests = load_tests()
    missing = unlisted_tests(tests, (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8"))
    if missing:
        print("test-scope: not in tests/CMakeLists.txt, so they never run: " +
              ", ".join(m + ".cpp" for m in missing), file=sys.stderr)
        return 3
    changed, cmake_lines, unknown = changed_files(args.base, args.since)
    if changed is None:
        print("all")
        print("")
        print("settings")
        print(f"all test files: scope unknown ({unknown})")
        return 0
    repo_files = (git("ls-files") or "").split("\n")
    result = plan(changed, tests, repo_files, cmake_lines)
    if result["mode"] == "all":
        print("all")
        print("")
        print("settings")
        print(f"all test files: {result['all_reason']}")
        return 0
    print("scoped")
    print(",".join(f"*/{t}.cpp" for t in sorted(result["why"])))
    print("settings" if result["settings"] else "no-settings")
    print(describe(result))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
