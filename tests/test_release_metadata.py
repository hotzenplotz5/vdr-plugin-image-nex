#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check licensing, version single-source and packaging metadata."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def text(path):
    return (ROOT / path).read_text(encoding="utf-8")


class ReleaseMetadataTests(unittest.TestCase):
    def test_one_version_source(self):
        version = text("VERSION").strip()
        self.assertRegex(version, r"^\d+\.\d+\.\d+(?:-(?:dev|rc\d+))?$")
        self.assertIn("VERSION := $(strip $(shell cat VERSION))", text("Makefile"))
        self.assertIn("IMAGE_PLUGIN_VERSION", text("Makefile"))
        self.assertIn("static const char *VERSION        = IMAGE_PLUGIN_VERSION",
                      text("image.c"))
        self.assertIn(version, text("CHANGELOG.md"))

    def test_debian_snapshot_matches_upstream_version(self):
        version = text("VERSION").strip().replace("-", "~")
        deb = text("debian/changelog")
        header = deb.splitlines()[0]
        self.assertIn(f"vdr-plugin-image-next ({version}-1)", header)
        self.assertIn("UNRELEASED", header)

    def test_existing_gpl_or_later_grant_is_preserved(self):
        copying = text("COPYING")
        debian = text("debian/copyright")
        self.assertIn("GNU GENERAL PUBLIC LICENSE", copying)
        self.assertIn("Version 2, June 1991", copying)
        self.assertIn("GPL-2+", debian)
        self.assertIn("any later version", debian)
        for author in ("Andreas Brachold", "Kai Tobias Burwieck",
                       "Interpohl", "Onno Kreuzinger"):
            self.assertIn(author, debian)

    def test_skin_template_is_packaged_without_overwriting_skins(self):
        makefile = text("Makefile")
        self.assertIn("install-skin-template:", makefile)
        self.assertIn("install: install-lib install-i18n install-skin-template",
                      makefile)
        self.assertIn("$(DESTDIR)$(SKINDESIGNER_TEMPLATE_DIR)", makefile)
        self.assertIn("plug-image_next-grid.xml", makefile)
        self.assertIn("does not silently modify", text("debian/README.Debian"))

    def test_obsolete_debian_dependencies_are_removed(self):
        deps = text("debian/control")
        for obsolete in ("cdbs,", "libxvidcore-dev", "libmp3lame-dev",
                         "libskindesignerapi-dev\n", "vdr-plugin-image-next-dbg"):
            self.assertNotIn(obsolete, deps)
        self.assertIn("${vdr:Depends}", deps)
        self.assertIn("vdr-dev", deps)
        self.assertIn("Rules-Requires-Root: no", deps)


if __name__ == "__main__":
    unittest.main()
