#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixture cases for size-check.py. Run: python3 -I scripts/size-check-test.py"""
import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("size_check", Path(__file__).with_name("size-check.py"))
sc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sc)


def lines(n):
    return "".join(f"int v{i};\n" for i in range(n))


def fn(name, body_lines, extra=""):
    body = "".join(f"  x += {i};\n" for i in range(body_lines))
    return f"int {name}(int x) {{\n{extra}{body}  return x;\n}}\n"  # body_lines + 3 lines


class FileSize(unittest.TestCase):
    def test_new_file_at_1000_passes_and_1001_fails(self):
        self.assertEqual(sc.check_text("a.cpp", lines(1000), None), [])
        self.assertEqual(len(sc.check_text("a.cpp", lines(1001), None)), 1)

    def test_old_big_file_growing_fails(self):
        self.assertEqual(len(sc.check_text("a.cpp", lines(1301), lines(1300))), 1)

    def test_old_big_file_same_or_shrinking_passes(self):
        self.assertEqual(sc.check_text("a.cpp", lines(1300), lines(1300)), [])
        self.assertEqual(sc.check_text("a.cpp", lines(1200), lines(1300)), [])

    def test_small_file_crossing_the_ceiling_fails(self):
        self.assertEqual(len(sc.check_text("a.cpp", lines(1001), lines(900))), 1)


class FunctionSize(unittest.TestCase):
    def test_function_at_80_passes_and_81_fails(self):
        self.assertEqual(sc.check_text("a.cpp", fn("f", 77), None), [])
        self.assertEqual(len(sc.check_text("a.cpp", fn("f", 78), None)), 1)

    def test_old_long_function_growing_fails_shrinking_passes(self):
        self.assertEqual(len(sc.check_text("a.cpp", fn("f", 100), fn("f", 99))), 1)
        self.assertEqual(sc.check_text("a.cpp", fn("f", 99), fn("f", 100)), [])
        self.assertEqual(sc.check_text("a.cpp", fn("f", 100), fn("f", 100)), [])

    def test_moved_long_function_passes_unless_it_grew(self):
        old = sc.functions(fn("f", 100))
        new = fn("f", 100).replace("int f(", "inline int f(")
        self.assertEqual(sc.check_text("b.hpp", new, None, old), [])
        grown = fn("f", 101).replace("int f(", "inline int f(")
        self.assertEqual(len(sc.check_text("b.hpp", grown, None, old)), 1)
        self.assertEqual(len(sc.check_text("b.hpp", new, None, {})), 1)
        member = fn("f", 100).replace("int f(", "int C::f(")
        self.assertEqual(sc.check_text("b.cpp", member, None, old), [])

    def test_function_inside_namespace_and_class(self):
        t = "namespace n {\nstruct S {\n" + fn("m", 90) + "};\n}\n"
        self.assertEqual(sc.functions(t), {"int m(int x)": 93})

    def test_lambda_does_not_split_or_end_the_function(self):
        body = "  auto g = [&](int y) {\n    if (y) { y++; }\n    return y;\n  };\n"
        t = fn("f", 60, extra=body * 3)  # 3 lambdas of 4 lines
        self.assertEqual(sc.functions(t), {"int f(int x)": 60 + 3 + 12})

    def test_braces_in_strings_chars_and_comments(self):
        extra = ('  const char* a = "}}}";\n  char c = \'}\';\n  // }\n  /* } { */\n'
                 '  const char* r = R"x(} {)x";\n  int big = 1\'000;\n')
        t = fn("f", 10, extra=extra)
        self.assertEqual(sc.functions(t), {"int f(int x)": 10 + 3 + 6})
        self.assertEqual(len(sc.functions(t + fn("g", 5))), 2)

    def test_init_list_and_namespace_initialiser_are_not_functions(self):
        t = "int arr[] = {1, 2, 3};\nstruct P { int a; P() : a{1} {}\n};\nint h() { return 1; }\n"
        self.assertEqual(sorted(sc.functions(t)), ["P() : a{1}", "int h()"])

    def test_preprocessor_braces_ignored(self):
        t = "#define OPEN {\n#define BOTH \\\n  { }\n" + fn("f", 3)
        self.assertEqual(sc.functions(t), {"int f(int x)": 6})

    def test_doctest_case_is_keyed_by_its_name(self):
        t = 'TEST_CASE("one") {\n  CHECK(1);\n}\nTEST_CASE("two") {\n  CHECK(2);\n  CHECK(3);\n}\n'
        self.assertEqual(sc.functions(t), {'TEST_CASE("one")': 3, 'TEST_CASE("two")': 4})

    def test_default_argument_braces_do_not_hide_the_body(self):
        t = ("void f(int x, const std::function<bool()>& a = {}) {\n"
             + "  x++;\n" * 90 + "}\n")
        self.assertEqual(len(sc.check_text("a.cpp", t, None)), 1)

    def test_prefixed_char_literals(self):
        extra = "  wchar_t a = L'}';\n  char8_t b = u8'{';\n  char16_t c = u'}';\n"
        t = fn("f", 5, extra=extra) + fn("g", 5)
        self.assertEqual(sorted(sc.functions(t).values()), [8, 11])

    def test_function_with_a_struct_or_extern_in_its_header(self):
        t = "struct Foo f() {\n  return {};\n}\nextern \"C\" void g() {\n  h();\n}\n"
        self.assertEqual(sc.functions(t), {"struct Foo f()": 3, 'extern "C" void g()': 3})
        t = 'extern "C" {\nvoid k() {\n  h();\n}\n}\nstruct S : B<decltype(f())> {\n  void m() {}\n};\n'
        self.assertEqual(sc.functions(t), {"void k()": 3, "void m()": 1})

    def test_template_default_argument(self):
        t = "template <typename T = int>\nT f(T x) {\n  return x;\n}\n"
        self.assertEqual(sc.functions(t), {"template <typename T = int> T f(T x)": 4})


def doc(lines_, bytes_):
    """Bytes of `lines_` lines that is `bytes_` long (each line ends in a newline)."""
    return b"x" * (bytes_ - lines_) + b"\n" * lines_


class NotesDoc(unittest.TestCase):
    def test_at_the_limits_passes_one_over_fails(self):
        self.assertEqual(sc.check_notes_doc("D", doc(200, 32768)), [])
        self.assertEqual(len(sc.check_notes_doc("D", doc(201, 32768))), 1)
        self.assertEqual(len(sc.check_notes_doc("D", doc(200, 32769))), 1)

    def test_the_message_names_the_limits(self):
        msg = sc.check_notes_doc("D", doc(200, 32769))[0]
        self.assertIn("200 lines", msg)
        self.assertIn("32768 bytes", msg)

    def test_absent_file_passes(self):
        self.assertEqual(sc.check_notes_doc("D", None), [])

    def test_the_run_fails_one_byte_over_and_passes_at_the_limit(self):
        with tempfile.TemporaryDirectory() as d:
            env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
            run = lambda *a: subprocess.run(a, cwd=d, env=env, capture_output=True, text=True)
            run("git", "init", "-q")
            script = str(Path(__file__).with_name("size-check.py"))
            Path(d, sc.NOTES_DOC).write_bytes(doc(200, 32768))
            self.assertEqual(run(sys.executable, "-I", script).returncode, 0)
            Path(d, sc.NOTES_DOC).write_bytes(doc(200, 32769))
            r = run(sys.executable, "-I", script)
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("over 200 lines or 32768 bytes", r.stdout)


class NoBase(unittest.TestCase):
    def test_without_origin_main_it_warns_and_exits_zero(self):
        with tempfile.TemporaryDirectory() as d:
            env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
            run = lambda *a: subprocess.run(a, cwd=d, env=env, capture_output=True, text=True)
            run("git", "init", "-q")
            Path(d, "core").mkdir()
            Path(d, "core", "big.cpp").write_text(lines(1500))
            run("git", "add", "-A")
            run("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "x")
            r = run(sys.executable, "-I", str(Path(__file__).with_name("size-check.py")))
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("WARNING", r.stdout)


if __name__ == "__main__":
    unittest.main()
