// The individual reference-suite / Listen tools (design: docs/airdaw-v4.html, reference suite and Listen mode).
#pragma once
#include "ToolSet.h"
#include "../../midi/MidiControl.h"

namespace ui {

std::unique_ptr<Tool> createTool(const juce::String& id);

}  // namespace ui
