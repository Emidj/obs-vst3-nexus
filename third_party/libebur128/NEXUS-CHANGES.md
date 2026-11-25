# NEXUS integration
Pinned upstream revision and hashes: manifest.json. Offline build after checkout.
Upstream libebur128 is MIT. Its bundled sys/queue.h has a separate BSD-3-Clause
notice. Both notices are retained and included by the install rules.
Only ebur128.c, ebur128.h and the bundled queue header are compiled/used.

## Concurrency patch
The pinned upstream init function rewrites shared histogram tables and gate
constants on every instance construction. Another instance can read those
tables while processing, so serializing constructors alone is insufficient.
nexus-tables-once.patch moves that exact initialization into the private
integration function nexus_ebur128_initialize_tables().
The C++ wrapper invokes it through std::call_once before any ebur128_init/use.
All calls to this library must use the wrapper. Direct external init is unsupported.
No DSP formula or coefficient is changed by this patch.

## Bounded storage
The wrapper enables S + I + HISTOGRAM, never LRA or True Peak.
Histogram storage is allocated before audio. This avoids per-block linked-list
allocation for integrated loudness. No reset/change-format/destroy is performed
in process(). Construction/destruction require stopped audio.
This does not establish hard realtime guarantees or complete EBU certification.

## Test-only instrumentation
nexus_ebur128_guarded compiles the same C file with an injected test header.
malloc/calloc/realloc/free and C++ new/delete are counted on the watched audio
thread. The normal library and plugin have no allocator hooks.
