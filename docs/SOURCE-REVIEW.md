# Source review for version 1.0

The single-effect filter inherited an unused loudness meter from the earlier Audio Rack implementation. The released interface exposes no loudness panel. The single-effect build now excludes the meter member, creation, processing and its unused status callback. The historical rack retains its own meter behavior.

Historical rack and equalizer UI targets are excluded from a default build unless explicitly needed by another requested target. Shared graph lifecycle, VST3 state, bypass, recovery and latency code is retained because the production host uses it. This is a targeted cleanup, not a claim that every potential unused SDK symbol has been eliminated.

The cleaned source was compiled and checked with the existing regression suites. The single-effect module was also exercised independently with libobs 31.1.1, 32.2.2 and 33.0.1. These checks do not establish universal third-party effect compatibility or long-session performance. Internal reports, logs, fixture binaries and third-party effect identities are excluded from the public package.
