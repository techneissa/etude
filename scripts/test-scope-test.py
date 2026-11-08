#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixture cases for test-scope.py. Run: python3 -I scripts/test-scope-test.py"""
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("test_scope", Path(__file__).with_name("test-scope.py"))
ts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ts)

REPO_FILES = [
    "app/song_library.hpp",
    "app/song_library.cpp",
    "core/include/etude/transport.hpp",
    "core/src/transport.cpp",
    "core/include/etude/song_builder.hpp",
    "app/ui/layout_bar.hpp",
    "app/ui/kit.hpp",
    "app/ui/tokens.hpp",
    "app/song_controller.hpp",
    "tests/song_controller_test_support.hpp",
    "app/loop_wake.hpp",
]

TESTS = {
    "transport_test": '#include "etude/transport.hpp"\n',
    "transport_wait_test": '#include "doctest/doctest.h"\n',
    "transport_rate_test": "",
    "transport_lookup": "",  # not a test file name: never loaded, here to show the name rule is exact
    "transports_test": "",
    "bar_layout_test": '#include "ui/layout_bar.hpp"\n',
    "song_controller_test": '#include "song_controller.hpp"\n',
    "song_controller_load_test": '#include "song_controller_test_support.hpp"\n',
    "ui_token_lint_test": "",
    "accidental_guard_test": "",
    "touch_fire_guard_test": "",
    "sources_controller_test": 'read("app/ui/pages_add_songs.cpp");\n',
    "share_setup_script_test": 'read("deploy/pi/setup.sh");\n',
    "beat_lines_test": 'open("/scripts/test-data/" + "pickup.mid");\n',
    "library_files_test": '#include "song_library.hpp"\n',
    "wake_test": '#include "../app/loop_wake.hpp"\n',
}
del TESTS["transport_lookup"]


def plan(*changed, tests=TESTS, cmake=None):
    return ts.plan(list(changed), tests, REPO_FILES, cmake)


def ran(result):
    return set(result["why"])


class NameMatch(unittest.TestCase):
    def test_source_runs_its_own_tests_and_the_numbered_ones(self):
        r = plan("core/src/transport.cpp")
        self.assertEqual(ran(r) - set(ts.GUARDS),
                         {"transport_test", "transport_wait_test", "transport_rate_test"})

    def test_header_runs_the_same_name_match(self):
        r = plan("core/include/etude/transport.hpp")
        self.assertIn("transport_wait_test", ran(r))
        self.assertIn("transport_test", ran(r))

    def test_a_longer_stem_is_not_a_match(self):
        self.assertNotIn("transports_test", ran(plan("core/src/transport.cpp")))

    def test_the_name_rule_matches_the_whole_stem(self):
        self.assertTrue(ts.named_after("song_controller", "song_controller_load_test"))
        self.assertFalse(ts.named_after("song", "songbook_test"))
        self.assertFalse(ts.named_after("song_controller_load", "song_controller_test"))


class Includes(unittest.TestCase):
    def test_a_changed_header_runs_the_tests_that_include_it(self):
        r = plan("app/ui/layout_bar.hpp")
        self.assertEqual(r["why"]["bar_layout_test"], "includes layout_bar.hpp")

    def test_a_changed_test_support_header_runs_its_includers(self):
        self.assertIn("song_controller_load_test", ran(plan("tests/song_controller_test_support.hpp")))

    def test_a_changed_test_runs_itself(self):
        r = plan("tests/transport_rate_test.cpp")
        self.assertEqual(r["why"]["transport_rate_test"], "changed")

    def test_a_header_included_by_too_many_tests_runs_everything(self):
        tests = {f"t{i}_test": '#include "etude/song_builder.hpp"\n' for i in range(ts.WIDE + 1)}
        r = plan("core/include/etude/song_builder.hpp", tests=tests)
        self.assertEqual(r["mode"], "all")
        self.assertIn("included by", r["all_reason"])

    def test_a_header_included_by_few_tests_stays_scoped(self):
        tests = {f"t{i}_test": '#include "etude/song_builder.hpp"\n' for i in range(ts.WIDE)}
        self.assertEqual(plan("core/include/etude/song_builder.hpp", tests=tests)["mode"], "scoped")


class SourceFile(unittest.TestCase):
    """A changed foo.cpp also runs the tests that include foo.hpp (the code it declares changed)."""

    def test_a_source_runs_the_includers_of_its_header(self):
        r = plan("app/song_library.cpp")
        self.assertEqual(r["why"]["library_files_test"], "includes song_library.hpp")

    def test_a_header_less_source_still_runs_by_name(self):
        r = plan("core/src/transport.cpp")
        self.assertIn("transport_test", ran(r))

    def test_a_source_whose_header_is_everywhere_runs_everything(self):
        tests = {f"t{i}_test": '#include "song_library.hpp"\n' for i in range(ts.WIDE + 1)}
        self.assertEqual(plan("app/song_library.cpp", tests=tests)["mode"], "all")

    def test_a_parent_directory_include_is_followed(self):
        self.assertEqual(plan("app/loop_wake.hpp")["why"]["wake_test"], "includes loop_wake.hpp")
        self.assertEqual(ts.stem_of("app/ui/ink_measure.cpp"), "ink_measure")


class RealTree(unittest.TestCase):
    """The examples that missed their tests: the real tests/ and the real app/."""

    @classmethod
    def setUpClass(cls):
        cls.tests = ts.load_tests()
        cls.files = [p for p in (ts.git("ls-files") or "").split("\n") if p]

    def scope(self, path):
        return ran(ts.plan([path], self.tests, self.files))

    def test_song_library_cpp(self):
        got = self.scope("app/song_library.cpp")
        for t in ("abc_library_test", "library_files_test", "song_lengths_test",
                  "song_controller_load_test"):
            self.assertIn(t, got)

    def test_ink_measure_cpp(self):
        self.assertIn("morph_glyphs_test", self.scope("app/ui/ink_measure.cpp"))

    def test_every_test_file_is_in_cmake(self):
        text = (ts.ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertEqual(ts.unlisted_tests(self.tests, text), [])


class Cmake(unittest.TestCase):
    def test_an_unlisted_test_is_found(self):
        text = "add_executable(t a_test.cpp b_test.cpp)\n"
        self.assertEqual(ts.unlisted_tests({"a_test": "", "b_test": "", "c_test": ""}, text), ["c_test"])

    def test_a_longer_name_does_not_list_a_shorter_one(self):
        self.assertEqual(ts.unlisted_tests({"a_test": ""}, "x_a_test.cpp\n"), ["a_test"])


class Guards(unittest.TestCase):
    def test_guards_run_for_any_diff_even_an_empty_one(self):
        for changed in ([], ["README.md"], ["core/src/transport.cpp"]):
            self.assertTrue(set(ts.GUARDS) <= ran(plan(*changed)), changed)

    def test_a_lint_that_names_a_source_by_path_runs_when_it_changes(self):
        r = plan("app/ui/pages_add_songs.cpp")
        self.assertEqual(r["why"]["sources_controller_test"], "names app/ui/pages_add_songs.cpp")

    def test_data_files_are_found_by_name(self):
        self.assertIn("beat_lines_test", ran(plan("scripts/test-data/pickup.mid")))
        self.assertIn("share_setup_script_test", ran(plan("deploy/pi/setup.sh")))

    def test_docs_run_only_the_guards(self):
        self.assertEqual(ran(plan("README.md", "docs/x.md")), set(ts.GUARDS))


class RunsEverything(unittest.TestCase):
    def test_cmake_dependencies_and_the_test_main(self):
        for path in ("CMakeLists.txt", "tests/CMakeLists.txt", "core/CMakeLists.txt",
                     "cmake/Dependencies.cmake", "tests/main.cpp"):
            self.assertEqual(plan(path)["mode"], "all", path)

    def test_tokens_and_kit(self):
        for path in ts.SHARED_HEADERS:
            self.assertEqual(plan(path)["mode"], "all", path)

    def test_a_kit_source_is_not_shared(self):
        self.assertEqual(plan("app/ui/kit_rows.cpp")["mode"], "scoped")


class CmakeLists(unittest.TestCase):
    LIST = {"tests/CMakeLists.txt": ["  new_test.cpp", "  app/other.cpp)"]}

    def test_a_list_only_change_stays_scoped(self):
        r = plan("tests/CMakeLists.txt", "tests/transport_test.cpp", cmake=self.LIST)
        self.assertEqual(r["mode"], "scoped")
        self.assertIn("transport_test", ran(r))

    def test_a_removed_name_stays_scoped(self):
        r = plan("CMakeLists.txt", cmake={"CMakeLists.txt": ["    core/src/transport.cpp"]})
        self.assertEqual(r["mode"], "scoped")

    def test_a_flags_change_runs_everything(self):
        r = plan("tests/CMakeLists.txt",
                 cmake={"tests/CMakeLists.txt": ["target_compile_options(t PRIVATE -Wall)"]})
        self.assertEqual(r["mode"], "all")

    def test_a_mixed_change_runs_everything(self):
        r = plan("tests/CMakeLists.txt",
                 cmake={"tests/CMakeLists.txt": ["  new_test.cpp", "find_package(Foo)"]})
        self.assertEqual(r["mode"], "all")

    def test_a_change_without_its_lines_runs_everything(self):
        self.assertEqual(plan("tests/CMakeLists.txt")["mode"], "all")

    def test_another_build_file_beside_a_list_change_still_runs_everything(self):
        for other in ("vcpkg.json", "CMakePresets.json", "cmake/Dependencies.cmake", "tests/main.cpp"):
            self.assertEqual(plan("tests/CMakeLists.txt", other, cmake=self.LIST)["mode"], "all", other)


class Settings(unittest.TestCase):
    def test_settings_code_asks_for_settings_check(self):
        for path in ("app/ui/settings_catalogue.cpp", "app/ui/pages.cpp", "app/ui/pages_midi_ports.cpp",
                     "core/include/etude/settings_doc.hpp", "core/src/session_state.cpp",
                     "app/settings_store.cpp"):
            self.assertTrue(plan(path)["settings"], path)

    def test_the_check_and_its_song_ask_too(self):
        for path in ("scripts/settings-check.sh", "tests/data/edge-pickup.mid"):
            self.assertTrue(plan(path)["settings"], path)

    def test_other_code_does_not(self):
        for path in ("core/src/transport.cpp", "app/ui/layout_bar.hpp", "tests/settings_doc_test.cpp"):
            self.assertFalse(plan(path)["settings"], path)


class Describe(unittest.TestCase):
    def test_the_line_names_each_test_and_why(self):
        line = ts.describe(plan("app/ui/layout_bar.hpp"))
        self.assertIn("bar_layout_test (includes layout_bar.hpp)", line)
        self.assertIn("(guard)", line)
        self.assertEqual(line.count("\n"), 0)


class InARepo(unittest.TestCase):
    """The script itself, in a scratch repo with an origin/main."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.d)
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        self.git = lambda *a: subprocess.run(
            ["git", "-c", "user.name=t", "-c", "user.email=t@t", *a],
            cwd=self.d, env=env, capture_output=True, text=True, check=True)
        for sub in ("scripts", "tests", "core/src"):
            Path(self.d, sub).mkdir(parents=True)
        shutil.copy(Path(__file__).with_name("test-scope.py"), Path(self.d, "scripts"))
        Path(self.d, "core/src/transport.cpp").write_text("int a;\n")
        Path(self.d, "core/src/other.cpp").write_text("int b;\n")
        for t in ("transport_test", "other_test", "ui_token_lint_test", "accidental_guard_test"):
            Path(self.d, "tests", t + ".cpp").write_text("")
        Path(self.d, "tests", "CMakeLists.txt").write_text(
            "add_executable(t transport_test.cpp other_test.cpp ui_token_lint_test.cpp\n"
            "  accidental_guard_test.cpp new_test.cpp)\n")
        self.git("init", "-q", "-b", "main")
        self.git("add", "-A")
        self.git("commit", "-qm", "base")
        self.git("update-ref", "refs/remotes/origin/main", "HEAD")
        self.env = env

    def run_scope(self, *args):
        r = subprocess.run([sys.executable, "-I", "scripts/test-scope.py", *args], cwd=self.d,
                           env=self.env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return r.stdout.split("\n")[:4]

    def test_a_working_tree_edit_runs_its_tests_and_the_guards(self):
        Path(self.d, "core/src/transport.cpp").write_text("int a2;\n")
        mode, flt, settings, line = self.run_scope()
        self.assertEqual(mode, "scoped")
        self.assertEqual(flt.split(","), ["*/accidental_guard_test.cpp", "*/transport_test.cpp",
                                          "*/ui_token_lint_test.cpp"])
        self.assertEqual(settings, "no-settings")
        self.assertIn("transport_test (name of transport.cpp)", line)

    def test_a_committed_edit_counts_too(self):
        Path(self.d, "core/src/other.cpp").write_text("int b2;\n")
        self.git("commit", "-qam", "edit")
        self.assertIn("*/other_test.cpp", self.run_scope()[1])

    def test_a_new_untracked_file_counts(self):
        Path(self.d, "tests/new_test.cpp").write_text("")
        self.assertIn("*/new_test.cpp", self.run_scope()[1])

    def test_a_test_file_missing_from_cmake_fails_loudly(self):
        Path(self.d, "tests/lost_test.cpp").write_text("")
        r = subprocess.run([sys.executable, "-I", "scripts/test-scope.py"], cwd=self.d,
                           env=self.env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 3)
        self.assertIn("lost_test.cpp", r.stderr)
        self.assertEqual(r.stdout, "")

    def test_cmake_runs_everything_and_says_so(self):
        Path(self.d, "CMakeLists.txt").write_text("project(x)\n")
        mode, flt, settings, line = self.run_scope()
        self.assertEqual((mode, flt, settings), ("all", "", "settings"))
        self.assertIn("CMakeLists.txt changed", line)

    def test_without_origin_main_it_runs_everything_and_says_so(self):
        self.git("update-ref", "-d", "refs/remotes/origin/main")
        mode, flt, _, line = self.run_scope()
        self.assertEqual((mode, flt), ("all", ""))
        self.assertIn("scope unknown (no origin/main)", line)

    def test_base_can_be_named(self):
        Path(self.d, "core/src/other.cpp").write_text("int b2;\n")
        self.git("commit", "-qam", "edit")
        self.git("update-ref", "refs/heads/older", "HEAD~1")
        self.assertIn("*/other_test.cpp", self.run_scope("--base", "older")[1])

    def test_a_list_only_cmake_change_stays_scoped_in_a_repo(self):
        cm = Path(self.d, "tests/CMakeLists.txt")
        cm.write_text(cm.read_text().replace("new_test.cpp)", "new_test.cpp\n  newer_test.cpp)"))
        Path(self.d, "tests/newer_test.cpp").write_text("")
        mode, flt, _, _ = self.run_scope()
        self.assertEqual(mode, "scoped")
        self.assertIn("*/newer_test.cpp", flt)

    def test_a_flags_change_to_cmake_runs_everything_in_a_repo(self):
        cm = Path(self.d, "tests/CMakeLists.txt")
        cm.write_text(cm.read_text() + "target_compile_options(t PRIVATE -Wall)\n")
        self.assertEqual(self.run_scope()[0], "all")

    def test_since_diffs_against_that_ref_not_the_merge_base(self):
        Path(self.d, "core/src/transport.cpp").write_text("int a2;\n")
        self.git("commit", "-qam", "first round")
        Path(self.d, "core/src/other.cpp").write_text("int b2;\n")
        flt = self.run_scope("--since", "HEAD")[1]
        self.assertIn("*/other_test.cpp", flt)
        self.assertNotIn("transport_test", flt)
        self.assertIn("*/transport_test.cpp", self.run_scope()[1])

    def test_since_a_missing_ref_runs_everything(self):
        mode, _, _, line = self.run_scope("--since", "nope")
        self.assertEqual(mode, "all")
        self.assertIn("no nope", line)


if __name__ == "__main__":
    unittest.main()
