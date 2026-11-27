#include "AppIcon.h"

namespace ui {

juce::Image makeAppIcon(int size, bool small) {
  juce::Image img(juce::Image::ARGB, size, size, true);
  juce::Graphics g(img);
  g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
  g.addTransform(juce::AffineTransform::scale((float)size / 256.f));  // drawn on a 256 x 256 grid
  const juce::Colour amber(0xffffb347), hot(0xfffff3d6);

  // the screen: a dark warm-brown tile, rounded
  const juce::Rectangle<float> tile = small ? juce::Rectangle<float>(4.f, 4.f, 248.f, 248.f) : juce::Rectangle<float>(10.f, 10.f, 236.f, 236.f);
  const float corner = small ? 44.f : 52.f;
  juce::Path screen; screen.addRoundedRectangle(tile, corner);
  if (!small) { g.setColour(juce::Colours::black.withAlpha(0.45f)); g.fillPath(screen, juce::AffineTransform::translation(0.f, 4.f)); }
  juce::ColourGradient bg(juce::Colour(0xff2a1c0c), 128.f, 116.f, juce::Colour(0xff0b0704), 128.f + 150.f, 116.f + 60.f, true);
  g.setGradientFill(bg); g.fillPath(screen);

  {
    juce::Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(screen);
    if (!small) {  // graticule
      g.setColour(amber.withAlpha(0.10f));
      for (float v = 32.f; v < 256.f; v += 32.f) { g.fillRect(v - 1.f, 20.f, 2.f, 216.f); g.fillRect(20.f, v - 1.f, 216.f, 2.f); }
    }
    // the trace: a flat line that rises to a peak and falls again, with a crossbar blip: an "A"
    juce::Path trace;
    trace.startNewSubPath(22.f, 196.f); trace.lineTo(64.f, 196.f); trace.lineTo(128.f, 50.f); trace.lineTo(192.f, 196.f); trace.lineTo(234.f, 196.f);
    trace.startNewSubPath(96.f, 142.f); trace.lineTo(160.f, 142.f);
    auto stroke = [&](float w, juce::Colour c) {
      g.setColour(c); g.strokePath(trace, juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    stroke(small ? 36.f : 30.f, amber.withAlpha(small ? 0.2f : 0.14f));  // phosphor bloom
    stroke(small ? 20.f : 11.f, amber);
    stroke(small ? 6.f : 3.5f, hot.withAlpha(0.9f));
    // glass: a faint sheen across the top
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.07f), 0.f, tile.getY(), juce::Colours::transparentWhite, 0.f, 120.f, false));
    g.fillRect(tile);
  }
  g.setColour(juce::Colours::white.withAlpha(0.10f));
  g.strokePath(screen, juce::PathStrokeType(small ? 4.f : 2.f), juce::AffineTransform::translation(0.f, small ? 0.f : 1.f));
  g.setColour(juce::Colours::black.withAlpha(0.8f)); g.strokePath(screen, juce::PathStrokeType(small ? 4.f : 2.f));
  return img;
}

bool renderAppIcons(const juce::File& folder) {
  folder.createDirectory();
  auto write = [&](const juce::Image& im, const juce::String& name) {
    auto f = folder.getChildFile(name); f.deleteFile();
    juce::FileOutputStream os(f);
    juce::PNGImageFormat png;
    return os.openedOk() && png.writeImageToStream(im, os);
  };
  return write(makeAppIcon(512, false), "AirDAW-512.png") && write(makeAppIcon(32, true), "AirDAW-32.png");
}

}  // namespace ui
