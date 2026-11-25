#!/usr/bin/env python3
"""Dependency-free source contract checks for the VDR image-next fork.

Run: python3 -m unittest discover -s tests -v
These checks do not replace compilation or an OSD integration test.
"""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def source(name):
    return (ROOT / name).read_text(encoding="utf-8")


class ImageGridContractTests(unittest.TestCase):
    def test_skindesigner_template_matches_registered_view(self):
        plugin = source("image.c")
        name = re.search(r'structure->name\s*=\s*"([^"]+)"', plugin).group(1)
        view = re.search(r'RegisterRootView\("([^"]+)"\)', plugin).group(1)
        template = ROOT / "skindesigner" / f"plug-{name}-{view}.xml"
        self.assertTrue(template.is_file())
        root = ET.parse(template).getroot()
        self.assertEqual(root.tag, "displayplugin")
        self.assertEqual([e.attrib["name"] for e in root.findall("grid")], ["imagegrid"])
        self.assertEqual(
            [e.attrib["name"] for e in root.findall("viewelement")],
            ["background", "header"],
        )

        for grid in root.findall("grid"):
            for area in grid.findall("area"):
                for attr in ("x", "y", "width", "height", "layer"):
                    self.assertIn(attr, area.attrib)

        required = ("{title}", "{thumbnail}", "{albumname}", "{is_folder}", "{current}")
        for token in required:
            self.assertIn(token, plugin)
        for token in ("{title}", "{thumbnail}", "{albumname}", "{current}"):
            self.assertIn(token, template.read_text(encoding="utf-8"))

    def test_grid_geometry_uses_normalized_coordinates(self):
        grid = source("menu-image.c")
        self.assertIn("double itemWidth = 1.0 / columns", grid)
        self.assertIn("double itemHeight = 1.0 / rows", grid)
        self.assertNotIn("double itemWidth = 100.0 / columns", grid)
        self.assertIn("imagegrid->SetGrid(idxOnPage, x, y, itemWidth, itemHeight)", grid)
        self.assertIn("int firstVisible = startRow * columns", grid)

    def test_native_fallback_is_not_skindesigner_dependent(self):
        plugin = source("image.c")
        grid = source("menu-image.c")
        self.assertIn("return new cMenuImageGrid(ImageSources.GetSource())", plugin)
        self.assertIn("fallbackGrid = new cMenuImageGrid(source)", grid)
        self.assertIn("if (fallbackGrid) return fallbackGrid->ProcessKey(Key)", grid)
        self.assertIn("#ifdef HAVE_SKINDESIGNER", source("menu-image.h"))

    def test_thumbnails_are_bounded_and_refreshable(self):
        grid = source("menu-image.c")
        self.assertIn("MAX_CACHE_BYTES = 64U * 1024U * 1024U", grid)
        self.assertIn("ThumbCacheKey(req.path, req.w, req.h) != req.key", grid)
        self.assertIn("req.generation != g_CacheGeneration.load()", grid)
        self.assertIn("mkstemp(tempThumbPath)", grid)
        self.assertNotIn("/tmp/vdr_thumb_%u_%zu.jpg", grid)

    def test_exif_orientation_reaches_all_image_displays(self):
        player = source("player-image.c")
        self.assertEqual(player.count("DefaultRotate()"), 3)
        self.assertEqual(player.count("DefaultMirror()"), 3)
        self.assertIn("if (pShell->nMirror == 1)", player)
        self.assertIn("else if (pShell->nMirror == 2)", player)
        self.assertIn("GetRotationFromExifData(path, rotation, mirror)", source("menu-image.c"))

    def test_no_implicit_root_source_or_folder_cover_writes(self):
        self.assertNotIn('new cFileSource("/", "Root"', source("image.c"))
        data = source("data.c")
        self.assertNotIn("ExtractExifThumbnail(firstJpgPath", data)
        self.assertNotIn("ExifThread->AddTask", data)

    def test_build_without_skindesigner_and_version(self):
        makefile = source("Makefile")
        self.assertIn("WITH_SKINDESIGNER ?= auto", makefile)
        self.assertIn("-DHAVE_SKINDESIGNER", makefile)
        self.assertIn("LIBSKINDESIGNERAPIVERSION", makefile)
        self.assertIn("$(DEPFILE): Makefile", makefile)
        self.assertIn('VERSION        = "0.6.2-dev"', source("image.c"))


if __name__ == "__main__":
    unittest.main()
