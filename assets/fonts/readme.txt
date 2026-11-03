DrivOnix interface fonts
========================

GNU Unifont 18.0.01 is used for the retro interface and Unicode game/file names.
unifont.otf supplies the Basic Multilingual Plane; unifont_upper.otf supplies
upper-plane glyphs. Both are kept as downloaded; manifest.json pins their bytes
and the original source URLs. The frontend must retain font bytes in memory
while CSFML fonts created from memory remain alive.

The font files are dual-licensed. DrivOnix distributes them under SIL OFL-1.1;
the original OFL and complete upstream license/copyright notice are included.
No commercial game artwork or ROM data is included with these fonts.

Upstream: https://unifoundry.com/unifont/

Retronix HD and Modern use the unmodified hinted Roboto Regular and Medium
fonts from googlefonts/roboto-2 commit
38062f4b4a0be4346d07a928408da21602545e9e. Roboto is distributed under
Apache-2.0; roboto-license.txt is the exact upstream license. Copyright and
font naming records remain embedded in the original files. The older pinned
Roboto source is intentional so builds never depend on a moving font release.

roboto-coverage.json contains the exact intersection of the Unicode cmap
entries with nonzero glyph IDs in both Roboto weights. fontcmap.py generates
that table using only the Python standard library; verify.py checks all source
hashes and reconstructs the coverage offline. The frontend uses this table to
choose Roboto or the retained Unifont BMP/upper fallback, without a newer
CSFML glyph-query API. This supplies glyph fallback; it does not add complex
script shaping, bidirectional layout, or color emoji support.

Roboto upstream: https://github.com/googlefonts/roboto-2
