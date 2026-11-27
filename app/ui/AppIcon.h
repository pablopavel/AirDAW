// AirDAW's application icon ("Scope A"): an amber oscilloscope trace that draws an "A" on a dark screen.
// The build uses resources/icon/AirDAW-512.png and AirDAW-32.png (Windows .exe, taskbar and title bar icon);
// they are rendered from this code with:  AirDAW.exe --render-icon <folder>
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui {

// The icon at `size` pixels. `small` drops the screen grid and thickens the trace so it reads at 16-32 px.
juce::Image makeAppIcon(int size, bool small);
// Writes AirDAW-512.png and AirDAW-32.png into `folder`; false on failure.
bool renderAppIcons(const juce::File& folder);

}  // namespace ui
