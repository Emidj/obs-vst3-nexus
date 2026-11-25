#pragma once
#include "dsp/rack.hpp"
namespace nexus::vst3 {
// Construct and invoke on the same serialized control thread used for lifecycle.
// Audio must be stopped before destruction of the rack.
RackProcessorFactory rack_factory();
}
