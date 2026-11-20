import tempfile
import json
import unittest
from pathlib import Path
import zipfile

from package_release import DOCUMENTS, REQUIRED_NOTICES, make_package


class PackageTests(unittest.TestCase):
    def test_only_listed_files_and_current_notices_are_packaged(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "VERSION").write_text("1.0.0")
            build = dict(version="1.0.0", probe="OFF", revision="fixture")
            for name in (*DOCUMENTS, "licenses/README.md", "licenses/sdl3.txt",
                         "EV Nova/Nova.rez", "Pilots/private.plt", ".env", "game.exe"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("fixture")
            share = root / "dependencies"
            for name in REQUIRED_NOTICES:
                notice = share / name / "copyright"
                notice.parent.mkdir(parents=True)
                notice.write_text("actual dependency notice")
            output = root / "public.zip"
            make_package(root, root / "game.exe", share, output, build)
            with zipfile.ZipFile(output) as archive:
                names = archive.namelist()
                self.assertIn("EV Nova Remastered/tools/setup-game-data.ps1", names)
                self.assertEqual(archive.read("EV Nova Remastered/licenses/sdl3.txt"),
                                 b"actual dependency notice")
                self.assertFalse(any(n.endswith((".rez", ".plt", ".env")) for n in names))
                manifest = json.loads(archive.read("EV Nova Remastered/release.json"))
                self.assertEqual(manifest["version"], build["version"])
                self.assertEqual(len(manifest["files"]), len(names) - 1)
            (share / "sdl3/copyright").unlink()
            original = output.read_bytes()
            with self.assertRaises(ValueError):
                make_package(root, root / "game.exe", share, output, build)
            self.assertEqual(output.read_bytes(), original)

    def test_prerelease_packages_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "VERSION").write_text("1.0.0-preview.1")
            build = dict(version="1.0.0-preview.1", probe="OFF", revision="fixture")
            with self.assertRaisesRegex(ValueError, "regular major.minor.patch"):
                make_package(root, root / "game.exe", root, root / "public.zip", build)


if __name__ == "__main__":
    unittest.main()
