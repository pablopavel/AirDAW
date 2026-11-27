#pragma once
#include <functional>

// Full screen for the main window. JUCE's kiosk mode ends as soon as the app loses the focus (a click on another
// monitor), so the main window does its own: a borderless window over the whole display it is on, which stays until
// Esc or the Full key. The app's window installs these; elsewhere they do nothing.
namespace ui::fullscreen {
inline std::function<bool()> isOn = [] { return false; };
inline std::function<void(bool)> set = [](bool) {};
inline void toggle() { set(!isOn()); }
}  // namespace ui::fullscreen
