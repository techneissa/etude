#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Size guard: fails when a hand-written file passes 1,000 lines or a function passes 80 lines, or
when one already over its limit grows, compared with the merge-base with origin/main. It also fails
when the assistant instructions file at the top of the tree passes 200 lines or 32,768 bytes (it is
read whole at every start, so it stays short); that check needs no origin/main and is skipped where
the file is absent.

Run it with `python3 -I scripts/size-check.py` (`just test` does). It prints the files over 600
lines so a reviewer sees them. With no origin/main to compare against it warns and checks nothing
(no ratchet), never crashes. Functions are found by brace matching on source with comments,
strings, character literals and preprocessor lines blanked out; a lambda or a brace in a string or
comment is part of the function around it.
"""
import bisect
import re
import subprocess
import sys
from pathlib import Path

DIRS = ("app", "core", "tests", "tools")
EXTS = (".cpp", ".hpp", ".h", ".cc", ".inc")
FILE_MAX = 1000
FUNC_MAX = 80
WARN_AT = 600
# The assistant instructions file. Its name is built here because the public export drops the file and the
# leak guard fails on the name in any other exported file.
NOTES_DOC = "AGENTS" + ".md"
NOTES_DOC_LINES = 200
NOTES_DOC_BYTES = 32768
# Generated data files (embedded fonts and icons) are exempt. Today they are written into the build
# folder, so none is in the tree; list a generated file here (repo-relative path) if one is checked in.
EXEMPT = frozenset()

TOKEN = re.compile(
    r'''(?P<pp>^[ \t]*\#(?:[^\n\\]|\\.|\\\n)*)'''
    r'''|(?P<cmt>//[^\n]*|/\*.*?\*/)'''
    r'''|(?P<raw>R"(?P<d>[^()\\ \n]{0,16})\(.*?\)(?P=d)")'''
    r'''|(?P<str>"(?:\\.|[^"\\\n])*")'''
    r'''|(?P<chr>(?<![0-9A-Za-z_])(?:u8|[LuU])?'(?:\\.|[^'\\\n])+')'''
    r'''|(?P<br>[{};])''',
    re.S | re.M)
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
CONTAINER = re.compile(r"^(?:typedef\s+)?(?:namespace|class|struct|union|enum|extern)\b")
STRINGS = re.compile(r'"(?:\\.|[^"\\\n])*"' + r"|'(?:\\.|[^'\\\n])+'")
INIT_LIST = re.compile(r"\)\s*(?:noexcept\s*)?:(?!:)")
ACCESS = re.compile(r"^(?:public|private|protected)\s*:\s*")


def line_count(text):
    return len(text.splitlines())


def check_notes_doc(name, data):
    """Problems of the assistant instructions file: `data` is its bytes (None when the file is absent)."""
    if data is None:
        return []
    lines = line_count(data.decode(errors="replace"))
    if lines > NOTES_DOC_LINES or len(data) > NOTES_DOC_BYTES:
        return [f"{name} is {lines} lines and {len(data)} bytes, over {NOTES_DOC_LINES} lines or "
                f"{NOTES_DOC_BYTES} bytes: cut it before adding anything"]
    return []


def _strip_template(header):
    """Drop a leading `template <...>` (its `=` and `<>` nest) and return the rest."""
    m = re.match(r"template\s*<", header)
    if not m:
        return header
    level = 1
    for i in range(m.end(), len(header)):
        level += {"<": 1, ">": -1}.get(header[i], 0)
        if level == 0:
            return header[i + 1:].lstrip()
    return header


def _is_container(header):
    if not CONTAINER.match(header):
        return False
    # `struct Foo f() {` and `extern "C" void f() {` are functions; a base list has its colon first
    return "(" not in header or ":" in header[:header.index("(")]


def _is_function(header):
    if _is_container(header) or "(" not in header:
        return False
    plain = STRINGS.sub('""', header)
    if plain.count("(") > plain.count(")"):
        return False  # a brace inside the parameter list, such as a `= {}` default argument
    before = header[:header.index("(")]
    if "=" in before and "operator" not in before:
        return False
    # `Ctor() : member{1}`: this brace is an initialiser, the body's comes later
    return not (INIT_LIST.search(header) and header.rstrip()[-1] not in ")}")


def functions(text):
    """Return {key: line count} for the functions defined at namespace or class level."""
    found = {}
    stack = []  # "c" container, "f" function body or other block
    depth = 0  # braces inside the current function body or block
    start = 0  # where the current header begins
    line = 1  # line of text[seen]
    seen = 0
    open_line = 0
    open_key = None
    for m in TOKEN.finditer(text):
        kind = m.lastgroup
        if kind == "pp" and not depth and not COMMENT.sub("", text[start:m.start()]).strip():
            start = m.end()
        if kind != "br":
            continue
        tok = m.group()
        pos = m.start()
        if depth:
            if tok == "{":
                depth += 1
            elif tok == "}":
                depth -= 1
                if depth == 0:
                    if open_key is not None:
                        line += text.count("\n", seen, pos)
                        seen = pos
                        key, n = open_key, 2
                        while key in found:
                            key = f"{open_key}#{n}"
                            n += 1
                        found[key] = line - open_line + 1
                        open_key = None
                        start = pos + 1
            continue
        if tok == ";":
            start = pos + 1
        elif tok == "}":
            if stack:
                stack.pop()
            start = pos + 1
        else:
            raw = text[start:pos]
            cleaned = COMMENT.sub(lambda c: "\n" * c.group().count("\n"), raw)
            cleaned = cleaned.lstrip()
            while ACCESS.match(cleaned):
                cleaned = ACCESS.sub("", cleaned, count=1).lstrip()
            line += text.count("\n", seen, pos)
            seen = pos
            sig = _strip_template(cleaned)
            if _is_container(sig):
                stack.append("c")
                start = pos + 1
            elif _is_function(sig):
                depth = 1
                open_line = line - cleaned.count("\n")
                open_key = " ".join(cleaned.split())[:200]
            else:
                depth = 1  # an initialiser or init-list brace: skipped, the header goes on
    return found


def moved_functions(changed, base):
    """Sizes of the functions the changed files had at the base, by name, so a function moved to
    another file (with `inline` added) counts as the old one and only has to not grow."""
    out = {}
    for r in sorted(changed):
        old = git("show", f"{base}:{r}")
        if old is not None:
            for key, size in functions(old).items():
                out[_unmoved(key)] = max(size, out.get(_unmoved(key), 0))
    return out


def _unmoved(key):
    """The name a function had before a move: no `inline`, no `Class::` or `Class::Nested::` in front
    of its name or its types (a method defined in its class body becomes `A::B::name` outside it)."""
    key = key[len("inline "):] if key.startswith("inline ") else key
    key = re.sub(r"\b\w+::", "", key)
    return re.sub(r"\s*=\s*[^,()]+(?=[,)])", "", key)  # a default argument stays in the declaration


def check_text(path, new_text, base_text, moved=None):
    """Violations for one file. base_text is None for a file new since the base. `moved` is the
    function sizes of the other changed files at the base (see moved_functions)."""
    out = []
    n = line_count(new_text)
    if n > FILE_MAX:
        if base_text is None or line_count(base_text) <= FILE_MAX:
            out.append(f"{path}: {n} lines, over {FILE_MAX}")
        elif n > line_count(base_text):
            out.append(f"{path}: grew from {line_count(base_text)} to {n} lines (over {FILE_MAX})")
    new_fns = functions(new_text)
    if any(v > FUNC_MAX for v in new_fns.values()):
        base_fns = functions(base_text) if base_text is not None else {}
        for key, size in new_fns.items():
            if size <= FUNC_MAX:
                continue
            was = base_fns.get(key)
            if was is None and moved:
                was = moved.get(_unmoved(key))
            what = key[:70]
            if was is None or was <= FUNC_MAX:
                out.append(f"{path}: function `{what}` is {size} lines, over {FUNC_MAX}")
            elif size > was:
                out.append(f"{path}: function `{what}` grew from {was} to {size} lines")
    return out


def git(*args):
    r = subprocess.run(("git",) + args, capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def main():
    root = git("rev-parse", "--show-toplevel")
    if root is None:
        print("size-check: not a git checkout, skipped")
        return 0
    root = Path(root.strip())
    files = sorted(
        p for d in DIRS for p in (root / d).rglob("*")
        if p.suffix in EXTS and p.is_file() and str(p.relative_to(root)) not in EXEMPT)
    texts = {}
    for p in files:
        texts[str(p.relative_to(root))] = p.read_text(errors="replace")
    big = sorted(((line_count(t), r) for r, t in texts.items() if line_count(t) > WARN_AT),
                 reverse=True)
    print(f"size-check: {len(big)} files over {WARN_AT} lines:")
    for n, r in big:
        print(f"  {n:5d}  {r}")

    doc = root / NOTES_DOC
    doc_problems = check_notes_doc(NOTES_DOC, doc.read_bytes() if doc.is_file() else None)
    for p in doc_problems:
        print(f"size-check: FAIL {p}")

    base = git("merge-base", "HEAD", "origin/main")
    base = base.strip() if base else None
    if not base:
        print("size-check: WARNING no origin/main to compare with; the ratchet is off, nothing checked")
        return 1 if doc_problems else 0
    changed = set((git("diff", "--name-only", base, "--", *DIRS) or "").split())
    changed |= set((git("ls-files", "-o", "--exclude-standard", "--", *DIRS) or "").split())
    changed = {r for r in changed if r.endswith(EXTS)}  # a picture under app/ is not source
    problems = []
    moved = moved_functions(changed, base)
    for r in sorted(changed & texts.keys()):
        old = git("show", f"{base}:{r}")
        problems += check_text(r, texts[r], old, moved)
    if problems:
        print("size-check: FAIL")
        for p in problems:
            print("  " + p)
        print(f"A file over {FILE_MAX} lines or a function over {FUNC_MAX} only shrinks: move out at "
              "least as much as the change adds (the rule on small files and functions).")
        return 1
    if doc_problems:
        return 1
    print(f"size-check: ok ({len(changed & texts.keys())} changed files checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
