#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
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

    def test_direct_osd_uses_foreground_thread(self):
        control = source("control-image.c")
        player = source("player-image.c")
        worker = source("liboutput/stillimage.c")
        setup = source("setup-image.c")
        self.assertIn("cOsdProvider::NewOsd(0, 0)", control)
        self.assertIn("tArea area = {0, 0, width - 1, height - 1, 32}", control)
        self.assertIn("osd->CanHandleAreas(&area, 1)", control)
        self.assertIn("cImageControl::DrawPictureOsd", control)
        self.assertIn("m_pictureOsd->DrawImage", control)
        self.assertIn("TakeDirectPicture()", control)
        self.assertNotIn("cOsdProvider::NewOsd", player)
        self.assertIn("PublishDirectPicture()", player)
        self.assertIn("AV_PIX_FMT_BGRA", player)
        self.assertIn("if (player->DirectOutput())", worker)
        self.assertIn("m_nPictureOutput = 0", setup)
        self.assertIn('ParseInteger("PictureOutput"', setup)
        self.assertIn('SetupStore("PictureOutput"', setup)
        self.assertIn("return bQueueEmpty", player)

    def test_suiteosd_native_photo_services(self):
        bridge = source("image-next-service-v1.h")
        impl = source("image-next-service.c")
        plugin = source("image.c")
        player = source("control-image.c")
        for service in ("ImageNext-Browse-v1.0", "ImageNext-Thumbnail-v1.0",
                        "ImageNext-Play-v1.0"):
            self.assertIn(service, bridge)
        for action in ("BrowseSource(", "LoadThumbnail(", "StartPlayback("):
            self.assertIn(action, impl)
        self.assertIn("HandleImageNextSuiteService(Id, Data)", plugin)
        self.assertIn("WithinSource(", impl)
        self.assertIn("realpath(", impl)
        self.assertIn('segment == ".."', impl)
        self.assertIn("ImageNextCachedThumbnail(", impl)
        self.assertIn("cImageControl::SetSlideShow(slideshow, request.slideshow)", impl)
        self.assertIn("SetSlideShow(cSlideShow * pNewSlideShow, bool autoplay)", player)
        self.assertIn("m_bSlideShowActiv = autoplay", player)

    def test_build_without_skindesigner_and_version(self):
        makefile = source("Makefile")
        self.assertIn("WITH_SKINDESIGNER ?= auto", makefile)
        self.assertIn("-DHAVE_SKINDESIGNER", makefile)
        self.assertIn("LIBSKINDESIGNERAPIVERSION", makefile)
        self.assertIn("$(DEPFILE): Makefile", makefile)
        version = source("VERSION").strip()
        self.assertRegex(version, r"^\d+\.\d+\.\d+(?:-(?:dev|rc\d+))?$")
        self.assertIn("VERSION := $(strip $(shell cat VERSION))", makefile)
        self.assertIn('IMAGE_PLUGIN_VERSION=\'"$(VERSION)"\'', makefile)
        self.assertIn("static const char *VERSION        = IMAGE_PLUGIN_VERSION;", source("image.c"))


if __name__ == "__main__":
    unittest.main()
