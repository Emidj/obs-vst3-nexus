# References and provenance

| Component | Original source | Pinned revision / license |
| --- | --- | --- |
| OBS Studio / libobs | https://github.com/obsproject/obs-studio | Host dependency; GPL-2.0-or-later |
| OBS filter API | https://docs.obsproject.com/reference-sources | Audio filter/source lifecycle reference |
| Qt Widgets | https://doc.qt.io/qt-6/qtwidgets-index.html | Host-provided runtime; applicable Qt licensing |
| VST3 SDK 3.8.1 | https://github.com/steinbergmedia/vst3sdk | 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96; MIT roots, retain per-file notices |
| iir1 1.11.1 | https://github.com/berndporr/iir1 | 164c12e7c7979dd016629adcfa188374caad3233; MIT |
| libebur128 | https://github.com/jiixyj/libebur128 | 67b33abe1558160ed76ada1322329b0e9e058b02; MIT, queue header BSD-3-Clause |

The precise VST3 submodule revisions and license hashes are recorded in [the SDK manifest](dependencies/vst3/manifest.json). The Windows Unicode path patch is retained at [vst3sdk-nexus-unicode.patch](../third_party/vst3sdk-nexus-unicode.patch). Dependency modification notices remain with their source.

VST is a trademark of Steinberg Media Technologies GmbH. Naming the API and its SDK does not imply endorsement. Third-party effect products and vendors are not listed as certified compatible products.
