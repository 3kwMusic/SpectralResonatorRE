# Third-party notices

Copyright in each third-party component remains with its respective holders.
The project's AGPL-3.0-only grant does not replace those components' licenses.
Original notices are retained under `licenses/` and in the complete dependency
source included with the corresponding-source distribution.

## JUCE 8.0.4

- Upstream: https://github.com/juce-framework/JUCE/tree/8.0.4
- Copyright: Raw Material Software Limited and the component authors.
- This project's distribution uses JUCE's AGPLv3 option, not a claim that JUCE
  is MIT-licensed or that a proprietary JUCE license is included.
- Upstream terms: `licenses/JUCE-LICENSE.md`; AGPLv3 text: `LICENSE`.

## VST 3 SDK 3.7.12, as bundled with JUCE 8.0.4

- Copyright: Steinberg Media Technologies GmbH and the named contributors.
- This distribution uses the GPL version 3 option for SDK files offering that
  option. GPLv3 and AGPLv3 permit this combination under their section 13;
  the respective licenses continue to govern their components.
- The GPLv3 text is in `licenses/GPL-3.0-only.txt`. Original SDK and subcomponent
  notices are in `licenses/VST3-*-LICENSE.txt`.
- The MIT license of later SDK releases is not applied to this older SDK.
- VST is a trademark of Steinberg Media Technologies GmbH. No endorsement or
  trademark license is implied.

## Components supplied within JUCE

These notices cover the libraries available to the Windows VST3/Standalone
build. Actual inclusion may depend on module configuration and linker removal.
Their complete source and file-level notices are preserved in the source bundle.

| Component | License / notice copy |
| --- | --- |
| FLAC | BSD-style, `licenses/FLAC.txt` |
| Ogg / Vorbis | BSD-style, `licenses/Ogg-Vorbis.txt` |
| HarfBuzz | Old MIT and file-level notices, `licenses/HarfBuzz.txt` |
| SheenBidi | Apache-2.0, `licenses/SheenBidi.txt` |
| libpng | libpng license, `licenses/libpng.txt` |
| JPEG library | Independent JPEG Group terms, `licenses/JPEG.txt` |
| zlib | zlib license, `licenses/zlib.txt` |
| LV2, Lilv, Serd, Sord, Sratom | Per-component notices in `licenses/LV2-*.txt` |

Other JUCE-supported targets or optional modules can add dependencies. Their
notices must be reviewed before enabling or distributing those configurations.
JUCE's own dependency list is retained in `licenses/JUCE-LICENSE.md`.

## Scope

Build and validation tools such as CMake, Visual Studio and pluginval are not
included in the plugin. Ordinary use of this audio effect does not impose its
software license on users' music or recordings. Rights in any third-party input
material remain separate.
