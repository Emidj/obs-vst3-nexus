# VST3 SDK integration — early phase 8 core
SDK 3.8.1. Exact revisions, file hashes and root licenses:
docs/dependencies/vst3/manifest.json and *-LICENSE.txt.
Checked out: base, pluginterfaces, public.sdk, cmake. No VSTGUI/doc/tutorial
submodule downloads. Preserve original per-file copyright/license notices.
The roots declare MIT; some SDK CMake templates retain BSD notices. Original
source notices and VST3 usage guidelines remain in the vendor tree.
No claim of certification or universal plug-in compatibility is made.

## Local patch
vst3sdk-nexus-unicode.patch changes Windows bundle-path decoding to UTF-16
from UTF-8, instead of constructing a filesystem path through the ANSI code
page. Its error path also preserves the original UTF-8 path.
Only public.sdk/source/vst/hosting/module_win32.cpp is modified.
Modern bundles and legacy single-file VST3 modules with non-ASCII names are
tested. Upstream ADelay source files remain unmodified; its projectversion.h
is generated in the build folder and identifies the internal fixture.

## Build/distribution scope
NEXUS_BUILD_VST3_CORE defaults OFF. Native OBS DLL does not link this SDK.
Early host/probe/fixtures build independently in build/vst3-core.
VSTGUI, SDK example applications, plug-in links, automatic module-info
generation and automatic validator execution are disabled.
Two fixture modules are built only under BUILD_TESTING, inside the project;
they are not installed or linked into system VST3 folders.
No external vendor plug-ins are bundled.
No SDK, host or fixture binary is added to native OBS installation rules.
