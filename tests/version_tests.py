"""Integration checks for the version CLI using isolated repository fixtures."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parent.parent


class VersionCliTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "scripts").mkdir()
        (self.root / "AutoClicker").mkdir()
        shutil.copyfile(REPOSITORY / "scripts" / "version.py", self.root / "scripts" / "version.py")
        self.write_fixture()

    def write_fixture(self, version="1.2.3"):
        source_version = (REPOSITORY / "VERSION").read_text(encoding="utf-8").strip()
        (self.root / "VERSION").write_text(version + "\n", encoding="utf-8")
        header = (REPOSITORY / "AutoClicker" / "Version.h").read_text(encoding="utf-8")
        header = header.replace(source_version.replace(".", ",") + ",0", version.replace(".", ",") + ",0")
        header = header.replace(f'"{source_version}"', f'"{version}"')
        (self.root / "AutoClicker" / "Version.h").write_text(header, encoding="utf-8")
        manifest = (REPOSITORY / "AutoClicker" / "AutoClicker.manifest").read_text(encoding="utf-8")
        manifest = manifest.replace(f'version="{source_version}.0"', f'version="{version}.0"', 1)
        (self.root / "AutoClicker" / "AutoClicker.manifest").write_text(manifest, encoding="utf-8")
        shutil.copyfile(REPOSITORY / "AutoClicker" / "AutoClicker.rc", self.root / "AutoClicker" / "AutoClicker.rc")

    def run_cli(self, *arguments, success=True):
        result = subprocess.run(
            [sys.executable, str(self.root / "scripts" / "version.py"), *arguments],
            cwd=self.root.parent,
            text=True,
            capture_output=True,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertTrue(result.stderr)
        return result

    def snapshot(self):
        return {path: path.read_bytes() for path in self.root.rglob("*") if path.is_file()}

    def test_check_uses_script_location_and_accepts_matching_tag(self):
        result = self.run_cli("--check", "--tag", "v1.2.3")
        self.assertEqual(result.stdout, "1.2.3\n")
        self.assertEqual(result.stderr, "")

    def test_each_bump_resets_lower_components_and_synchronizes_metadata(self):
        for component, expected in (("patch", "1.2.4"), ("minor", "1.3.0"), ("major", "2.0.0")):
            with self.subTest(component=component):
                self.write_fixture()
                self.assertEqual(self.run_cli("--bump", component).stdout, expected + "\n")
                self.assertEqual((self.root / "VERSION").read_text(), expected + "\n")
                self.run_cli("--check", "--tag", "v" + expected)
                manifest = (self.root / "AutoClicker" / "AutoClicker.manifest").read_text()
                self.assertIn(f'version="{expected}.0"', manifest)
                self.assertIn('version="6.0.0.0"', manifest)  # Common Controls stays unchanged.

    def test_mismatched_and_noncanonical_tags_are_rejected_without_writes(self):
        before = self.snapshot()
        for tag in ("v1.2.4", "1.2.3", "v01.2.3", "v1.2.3.0", "v1.2.3-beta", "v1.2.3\n"):
            with self.subTest(tag=tag):
                self.run_cli("--check", "--tag", tag, success=False)
                self.assertEqual(self.snapshot(), before)

    def test_noncanonical_versions_are_rejected(self):
        invalid = ("", "v1.2.3", "01.2.3", "1.02.3", "1.2.03", "1.2", "1.2.3.0",
                   "1.2.-3", "1.2.65536", "65536.0.0", "1.2.3-beta", " 1.2.3",
                   "1.2.3 ", "\n1.2.3", "1.2.3\n\n", "1.2.３", "1." + "9" * 5000 + ".3")
        for value in invalid:
            with self.subTest(version=value[:80]):
                (self.root / "VERSION").write_text(value, encoding="utf-8")
                before = self.snapshot()
                self.run_cli("--check", success=False)
                self.assertEqual(self.snapshot(), before)

    def test_generated_header_drift_is_rejected(self):
        path = self.root / "AutoClicker" / "Version.h"
        for old, new in (("1,2,3,0", "1,2,4,0"), ('"1.2.3"', '"1.2.4"')):
            with self.subTest(old=old):
                self.write_fixture()
                path.write_text(path.read_text().replace(old, new))
                self.run_cli("--check", success=False)

    def test_manifest_drift_and_malformed_xml_are_rejected(self):
        path = self.root / "AutoClicker" / "AutoClicker.manifest"
        for old, new in (('version="1.2.3.0"', 'version="1.2.4.0"'),
                         ('name="MfcAutoClicker.App"', 'name="Unexpected.App"'),
                         ("</assembly>", "")):
            with self.subTest(old=old):
                self.write_fixture()
                path.write_text(path.read_text().replace(old, new, 1))
                before = self.snapshot()
                self.run_cli("--bump", "patch", success=False)
                self.assertEqual(self.snapshot(), before)

    def test_resource_literals_cannot_bypass_the_generated_version(self):
        path = self.root / "AutoClicker" / "AutoClicker.rc"
        for old, new in (('#include "Version.h"', '// #include "Version.h"'),
                         ("FILEVERSION AUTOCLICKER_VERSION_COMMA", "FILEVERSION 9,9,9,0"),
                         ("PRODUCTVERSION AUTOCLICKER_VERSION_COMMA", "PRODUCTVERSION 9,9,9,0"),
                         ('VALUE "FileVersion", AUTOCLICKER_VERSION_STRING', 'VALUE "FileVersion", "9.9.9"'),
                         ('VALUE "ProductVersion", AUTOCLICKER_VERSION_STRING', 'VALUE "ProductVersion", "9.9.9"')):
            with self.subTest(old=old):
                self.write_fixture()
                path.write_text(path.read_text().replace(old, new))
                self.run_cli("--check", success=False)

    def test_windows_component_limit_and_overflow_do_not_write_any_files(self):
        self.write_fixture("65535.65535.65535")
        self.run_cli("--check", "--tag", "v65535.65535.65535")
        before = self.snapshot()
        for component in ("major", "minor", "patch"):
            with self.subTest(component=component):
                self.run_cli("--bump", component, success=False)
                self.assertEqual(self.snapshot(), before)

    def test_higher_bumps_can_reset_lower_components_at_their_limit(self):
        for initial, component, expected in (("1.2.65535", "minor", "1.3.0"),
                                              ("1.65535.65535", "major", "2.0.0")):
            with self.subTest(initial=initial):
                self.write_fixture(initial)
                self.assertEqual(self.run_cli("--bump", component).stdout, expected + "\n")
                self.run_cli("--check", "--tag", "v" + expected)

    def test_conflicting_cli_options_are_rejected_without_writes(self):
        before = self.snapshot()
        self.run_cli("--bump", "patch", "--tag", "v1.2.4", success=False)
        self.run_cli("--check", "--bump", "patch", success=False)
        self.assertEqual(self.snapshot(), before)


if __name__ == "__main__":
    unittest.main()
