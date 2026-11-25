# Third-party notices

OBS VST3 NEXUS 1.0: original code is GPL-2.0-or-later (LICENSE / LICENSES.md).
Third-party notices below remain applicable. OBS/Qt runtimes and commercial
effects are not redistributed. The source package includes the linked dependency sources.

## iir1 — MIT
Copyright (c) 2009 Vinnie Falco; Copyright (c) 2012 Bernd Porr.
Revision 164c12e7c7979dd016629adcfa188374caad3233. Full notice: third_party/iir1/COPYING and retained source headers. Installer notice: iir1-MIT.txt.

## libebur128 — MIT and BSD-3-Clause
Copyright (c) 2011 Jan Kokemüller. Revision 67b33abe1558160ed76ada1322329b0e9e058b02. Full MIT notice: third_party/libebur128/COPYING. The queue header retains the Regents of the University of California BSD-3-Clause notice. Local changes remain documented in NEXUS-CHANGES.md and manifest.json. The single-effect filter no longer instantiates the inherited loudness meter; attribution is retained for shared/historical source.

## VST3 SDK 3.8.1 — MIT roots
Steinberg Media Technologies GmbH. Root revision 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96. Exact submodule revisions and license hashes: docs/dependencies/vst3/manifest.json. Five full license texts are preserved there and in SDK roots. Per-file BSD and other notices remain applicable. The explicit Windows Unicode path patch is provided at third_party/vst3sdk-nexus-unicode.patch.

The source tree contains SDK examples and internal fixture source for development, but no compiled VST3 effects are distributed. The production installer contains only the NEXUS host DLL, its scan helper and legal/documentation files. OBS and Qt runtimes and third-party effect binaries are not included.
