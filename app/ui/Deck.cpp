#include "Deck.h"

namespace ui {

static constexpr int kLogoW = 225;  // the AIRDAW logo on the left of the deck

// ---------------- TransportKeys ----------------
juce::Rectangle<float> TransportKeys::keyRect(int k) const {
  const float total = kNum * kKeyW + (kNum - 1) * kGap;
  const float x0 = (getWidth() - total) / 2.f;
  return {x0 + k * (float)(kKeyW + kGap), 9.f, (float)kKeyW, (float)kKeyH};
}

void TransportKeys::paint(juce::Graphics& g) {
  auto& t = T();
  // recessed well
  auto well = wellRect();
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff040404), 0, well.getY(), juce::Colour(0xff0f0f11), 0, well.getBottom(), false));
  g.fillRoundedRectangle(well, T().rad(9.f));
  g.setColour(juce::Colour(0x1affffff)); g.drawHorizontalLine((int)well.getBottom(), well.getX() + 8, well.getRight() - 8);

  const bool bl = t.glow > 0.f;  // backlit keys (Glow 0 in Display: plain keys and cream legends)
  const float L = juce::jlimit(0.f, 1.2f, lightLvl_);
  const auto lc = lightCol_;
  auto la = [&](float a) { return lc.withAlpha(t.glowAlpha(juce::jlimit(0.f, 1.f, a * L))); };
  if (bl) {
    // one even light source under the keys: the floor of the well is lit uniformly and dimly
    juce::Graphics::ScopedSaveState ss(g);
    juce::Path wp; wp.addRoundedRectangle(well, T().rad(9.f)); g.reduceClipRegion(wp);
    g.setColour(la(0.08f)); g.fillRect(well);
    // between the keys: faint slots of the same light
    for (int k = 0; k + 1 < kNum; ++k) {
      const float gx = (keyRect(k).getRight() + keyRect(k + 1).getX()) / 2.f;
      juce::ColourGradient sg(la(0.f), gx - 5.f, 0, la(0.f), gx + 5.f, 0, false);
      sg.addColour(0.5, la(0.26f)); sg.addColour(0.35, la(0.08f)); sg.addColour(0.65, la(0.08f));
      g.setGradientFill(sg); g.fillRect(gx - 5.f, well.getY() + 2.f, 10.f, well.getHeight() - 4.f);
    }
  }

  for (int k = 0; k < kNum; ++k) {
    const bool on = lit_[k], pressed = on || down_ == k;
    auto r = keyRect(k);
    if (!pressed) {
      if (bl) {  // light escaping from under the raised key's front edge, with a dark contact line
        g.setGradientFill(juce::ColourGradient(la(0.14f), 0, r.getBottom(), la(0.f), 0, r.getBottom() + 5.f, false));
        g.fillRoundedRectangle(r.withY(r.getBottom() - 2.f).withHeight(7.f).reduced(4.f, 0.f), 3.f);
        g.setColour(juce::Colours::black.withAlpha(0.8f)); g.fillRoundedRectangle(r.translated(0, 2), T().rad(5.f));
      } else {
        g.setColour(juce::Colour(0xff070708)); g.fillRoundedRectangle(r.translated(0, 4), T().rad(5.f));
        g.setColour(juce::Colours::black.withAlpha(0.7f)); g.fillRoundedRectangle(r.translated(0, 7), T().rad(5.f));
      }
    }
    else r = r.translated(0, 3);
    if (on) {
      const bool rec = k == Rec;
      juce::Colour a = rec ? juce::Colour(0xffffb4b4) : t.c(0xffffe3a3), b = rec ? juce::Colour(0xffff5a5f) : t.c(0xffffbe55),
                   c = rec ? juce::Colour(0xffc22b30) : t.c(0xffe3952a);
      t.drawGlow(g, r, rec ? juce::Colour(0xffe5484d) : t.am(), 24.f, 0.75f);
      juce::ColourGradient cg(a, 0, r.getY(), c, 0, r.getBottom(), false); cg.addColour(0.45, b);
      g.setGradientFill(cg);
    } else {
      juce::ColourGradient cg(juce::Colour(0xff5c5c62), 0, r.getY(), juce::Colour(0xff252528), 0, r.getBottom(), false);
      cg.addColour(0.14, juce::Colour(0xff404045)); cg.addColour(0.58, juce::Colour(0xff303034));
      g.setGradientFill(cg);
    }
    g.fillRoundedRectangle(r, T().rad(5.f));
    g.setColour(juce::Colour(on ? 0x1f000000 : 0x2a000000));
    for (float y = r.getY() + 3.f; y < r.getBottom(); y += 4.f) g.fillRect(r.getX() + 1.f, y, r.getWidth() - 2.f, 1.f);
    g.setColour(juce::Colour(on ? 0xccffffff : 0x6effffff)); g.drawHorizontalLine((int)r.getY() + 1, r.getX() + 3, r.getRight() - 3);
    if (bl && !on) {  // translucent key: its edges pick up a little of the light, evenly all round
      g.setColour(la(0.07f)); g.drawRoundedRectangle(r.reduced(1.f), T().rad(5.f), 1.5f);
    }
    g.setColour(juce::Colours::black); g.drawRoundedRectangle(r, T().rad(5.f), 1.f);
    // legend
    const auto c = r.getCentre();
    if (k == Rec) {
      juce::Rectangle<float> d(c.x - 13, c.y - 13, 26, 26);
      t.drawGlow(g, d.reduced(3), juce::Colour(0xffe5484d), 12.f, 0.7f);
      g.setGradientFill(juce::ColourGradient(juce::Colour(0xffff9a9a), d.getX() + 9, d.getY() + 8, juce::Colour(0xffa81e24),
                                             d.getRight(), d.getBottom(), true));
      g.fillEllipse(d);
      continue;
    }
    juce::Path p;
    switch (k) {
      case Rtz: p.addRectangle(c.x - 11, c.y - 8, 3, 16); p.addTriangle(c.x - 8, c.y, c.x + 1, c.y - 8, c.x + 1, c.y + 8);
                p.addTriangle(c.x + 1, c.y, c.x + 10, c.y - 8, c.x + 10, c.y + 8); break;
      case Play: p.addTriangle(c.x - 7, c.y - 10, c.x - 7, c.y + 10, c.x + 10, c.y); break;
      case Stop: p.addRectangle(c.x - 8, c.y - 8, 16.f, 16.f); break;
      case Loop: p = loopSymbol(c); break;
      case Click: p = clickSymbol(c); break;
      default: break;
    }
    if (bl && !on) drawFrostedLegend(g, p, lc, L);
    else { g.setColour(on ? juce::Colour(0xff2a1800) : Theme::cream()); g.fillPath(p); }
  }
}

// Loop: two arrows chasing each other round a rounded loop.
juce::Path TransportKeys::loopSymbol(juce::Point<float> c) {
  juce::Path line;
  line.startNewSubPath(c.x - 12.f, c.y + 3.f); line.lineTo(c.x - 12.f, c.y - 2.f);
  line.quadraticTo(c.x - 12.f, c.y - 7.f, c.x - 7.f, c.y - 7.f); line.lineTo(c.x + 7.f, c.y - 7.f);
  line.startNewSubPath(c.x + 12.f, c.y - 3.f); line.lineTo(c.x + 12.f, c.y + 2.f);
  line.quadraticTo(c.x + 12.f, c.y + 7.f, c.x + 7.f, c.y + 7.f); line.lineTo(c.x - 7.f, c.y + 7.f);
  juce::Path p;
  juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(p, line);
  p.addTriangle(c.x + 6.f, c.y - 12.f, c.x + 13.f, c.y - 7.f, c.x + 6.f, c.y - 2.f);   // arrow heading right along the top
  p.addTriangle(c.x - 6.f, c.y + 2.f, c.x - 13.f, c.y + 7.f, c.x - 6.f, c.y + 12.f);   // and left along the bottom
  return p;
}

// Click: a metronome (wedge-shaped case, pendulum swung to one side, weight on the arm).
juce::Path TransportKeys::clickSymbol(juce::Point<float> c) {
  juce::Path outline;
  outline.startNewSubPath(c.x - 4.f, c.y - 11.f); outline.lineTo(c.x + 4.f, c.y - 11.f);
  outline.lineTo(c.x + 10.f, c.y + 11.f); outline.lineTo(c.x - 10.f, c.y + 11.f); outline.closeSubPath();
  outline.startNewSubPath(c.x - 7.f, c.y + 5.f); outline.lineTo(c.x + 7.f, c.y + 5.f);   // the face's lower edge
  outline.startNewSubPath(c.x, c.y + 5.f); outline.lineTo(c.x + 8.f, c.y - 10.f);       // pendulum
  juce::Path p;
  juce::PathStrokeType(2.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded).createStrokedPath(p, outline);
  juce::Path weight; weight.addRectangle(-2.5f, -2.f, 5.f, 4.f);
  p.addPath(weight, juce::AffineTransform::rotation(0.49f).translated(c.x + 4.5f, c.y - 3.5f));
  return p;
}

// Frosted-glass legend: most of the light from the even source below comes out here. Only the window itself is lit:
// hot glass in the lamp's colour with a near-white core, fine frost grain, a dark cut along its top edge, and a
// hairline of light escaping right at the cut's edge (as a real backlit legend shows) instead of a halo.
// The lamp behind the legends is strong: they stay bright when stopped and glow hot while playing.
void TransportKeys::drawFrostedLegend(juce::Graphics& g, const juce::Path& p, juce::Colour lc, float L) {
  auto& t = T();
  const auto b = p.getBounds().expanded(1.f);
  const float lv = juce::jlimit(0.f, 1.4f, 0.6f + 0.55f * L);
  g.setColour(lc.withAlpha(t.glowAlpha(0.26f * lv)));  // edge bleed
  g.strokePath(p, juce::PathStrokeType(2.f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt), juce::AffineTransform::translation(0.f, 0.5f));
  // the cut: a dark lip along the top edge gives it depth
  g.setColour(juce::Colours::black.withAlpha(0.45f)); g.fillPath(p, juce::AffineTransform::translation(0.f, -1.f));
  {
    juce::Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(p);
    g.setColour(juce::Colour(0xfffff6e6).interpolatedWith(lc, juce::jlimit(0.f, 1.f, 0.25f + 0.3f * lv))); g.fillRect(b);
    g.setColour(juce::Colours::white.withAlpha(juce::jlimit(0.f, 0.5f, 0.12f + 0.25f * (lv - 0.6f)))); g.fillRect(b);
    juce::Random rng(17);  // frost grain (same pattern every frame)
    const int n = (int)(b.getWidth() * b.getHeight() / 5.f);
    for (int i = 0; i < n; ++i) {
      const float x = b.getX() + rng.nextFloat() * b.getWidth(), y = b.getY() + rng.nextFloat() * b.getHeight();
      g.setColour(rng.nextBool() ? juce::Colours::white.withAlpha(0.16f * rng.nextFloat()) : juce::Colours::black.withAlpha(0.05f * rng.nextFloat()));
      g.fillRect(x, y, 1.f, 1.f);
    }
  }
  g.setColour(juce::Colours::white.withAlpha(0.14f + 0.12f * lv)); g.strokePath(p, juce::PathStrokeType(0.6f));
}

void TransportKeys::mouseDown(const juce::MouseEvent& e) {
  down_ = -1;
  for (int k = 0; k < kNum; ++k) if (keyRect(k).contains(e.position)) down_ = k;
  repaint();
}
void TransportKeys::mouseUp(const juce::MouseEvent& e) {
  const int k = down_; down_ = -1; repaint();
  if (k >= 0 && keyRect(k).contains(e.position) && onKey) onKey((Key)k);
}

// ---------------- Deck ----------------
Deck::Deck(AppCtx& ctx) : ctx_(ctx) {
  for (auto* c : std::initializer_list<juce::Component*>{&pos_, &rate_, &lat_, &dsp_, &keys_}) addAndMakeVisible(c);
  keys_.onKey = [this](TransportKeys::Key k) { if (onTransport) onTransport(k); };
  buttons_ = {&tap_, &listen_, &midi_, &plugins_, &projectBtn, &metersBtn, &displayBtn, &full_};
  tap_.setTriggeredOnMouseDown(true);  // the tap lands when the button goes down, not when it is released
  tap_.setTooltip("Tap tempo: tap in time (2 taps or more) to set the metronome tempo. Shows the current tempo.");
  tap_.onClick = [this] { ctx_.tapTempo(); };
  tap_.setButtonText("Tap 120.0");
  for (auto* b : buttons_) { addAndMakeVisible(*b); b->getProperties().set("kind", "key"); b->getProperties().set("fontH", 11.0); }
  midi_.setTooltip("MIDI Learn: click a control, then move a knob, fader or pad on your controller. Right-click a control to unmap it.");
  midi_.onClick = [this] { if (onMidi) onMidi(); };
  plugins_.setTooltip("Airwindows plugin library: installed build, updates and roll back");
  plugins_.onClick = [this] { if (onPlugins) onPlugins(); };
  tagMidi(tap_, "tap"); tagMidi(listen_, "listen");
  listen_.setTooltip("Listen mode: big master, the reference suite and every listening tool (drag tools to arrange)");
  listen_.onClick = [this] { if (onListen) onListen(); };
  projectBtn.setTooltip("New, open and save projects (Ctrl+N / Ctrl+O / Ctrl+S), audio settings");
  for (auto* l : {&rate_, &lat_, &dsp_}) { l->setTooltip("Click for audio device, metronome and auto-save settings"); l->onClick = [this] { if (onAudio) onAudio(); }; }
  projectBtn.onClick = [this] { if (onProject) onProject(); };
  metersBtn.onClick = [this] { if (onMeters) onMeters(); };
  displayBtn.onClick = [this] { if (onDisplay) onDisplay(); };
  full_.onClick = [this] { if (onFull) onFull(); };
}

std::vector<std::pair<juce::String, juce::Rectangle<int>>> TransportKeys::midiRegions() const {
  static const char* ids[kNum] = {"transport.rtz", "transport.play", "transport.stop", "transport.rec", "transport.loop", "transport.click"};
  std::vector<std::pair<juce::String, juce::Rectangle<int>>> v;
  for (int k = 0; k < kNum; ++k) v.push_back({ids[k], keyRect(k).toNearestInt()});
  return v;
}

void Deck::setMidiState(bool learning, bool armed) {
  midi_.setToggleState(learning, juce::dontSendNotification);
  const juce::String t = armed ? juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f Move a knob\xe2\x80\xa6")) : juce::String("MIDI Learn");
  if (midi_.getButtonText() != t) { midi_.setButtonText(t); resized(); }
}

void Deck::paintOverChildren(juce::Graphics& g) {
  auto& t = T();
  if (t.screws) { t.drawScrew(g, {11.f, 11.f}); t.drawScrew(g, {(float)getWidth() - 11.f, 11.f}); }  // mockup body.scr #deck
  if (!pluginsBadge_ || !plugins_.isVisible()) return;  // amber dot on Plugins when an update is available (mockup #upd)
  const auto b = plugins_.getBounds().toFloat();
  juce::Rectangle<float> d(b.getRight() - 14.f, b.getCentreY() - 4.f, 8.f, 8.f);
  t.drawGlow(g, d, t.am(), 9.f, 0.8f);
  g.setColour(t.am()); g.fillEllipse(d);
}

static juce::String clockText(double sec) {
  const int ms = (int)std::fmod(sec * 1000.0, 1000.0), s = (int)sec % 60, m = (int)sec / 60;
  return juce::String::formatted("%02d:%02d.%03d", m, s, ms);
}

void Deck::sync() {
  auto* e = ctx_.engine();
  if (e) {
    auto& tr = e->transport();
    if (tr.countingIn()) {
      const double beat = e->sampleRate() * 60.0 / juce::jlimit(20.0, 400.0, (double)e->metronome().bpm.load());
      pos_.setText("COUNT-IN " + juce::String((int)std::ceil((double)tr.countInLeft.load() / beat)));
    } else pos_.setText(clockText((double)tr.pos.load() / e->sampleRate()));
    keys_.setLit(TransportKeys::Play, tr.playing.load() && !tr.recording.load());
    keys_.setLit(TransportKeys::Rec, tr.recording.load());
    keys_.setLit(TransportKeys::Loop, tr.loopOn.load());
    keys_.setLit(TransportKeys::Click, e->metronome().enabled.load());
    const auto tt = "Tap " + juce::String(e->metronome().bpm.load(), 1);
    if (tap_.getButtonText() != tt) tap_.setButtonText(tt);
  }
  {  // the light under the transport keys follows its state, easing between levels
    const bool live = e && ctx_.audioRunning;
    const bool rec = ambPreview_ == 2 || (live && e->transport().recording.load());
    const bool play = ambPreview_ == 1 || (live && e->transport().playing.load());
    const float target = rec ? 0.85f : play ? 0.7f : 0.32f;
    amb_ += (target - amb_) * 0.08f;
    ambRed_ += ((rec ? 1.f : 0.f) - ambRed_) * 0.08f;
    const float speed = rec ? 0.15f : 0.07f;  // ~1.4 s pulse recording, ~3 s breath playing (sync runs ~30x/s)
    if (rec || play) ambPhase_ = std::fmod(ambPhase_ + speed, juce::MathConstants<float>::twoPi);
    const float depth = rec ? 0.16f : play ? 0.08f : 0.f;
    ambNow_ = amb_ * (1.f + depth * std::sin(ambPhase_));
    keys_.setBacklight(ambNow_, T().am().interpolatedWith(juce::Colour(0xffe5484d), juce::jlimit(0.f, 1.f, ambRed_)));
  }
  auto* dev = ctx_.devices.getCurrentAudioDevice();
  if (!ctx_.audioRunning || !dev) {
    rate_.setText("NO AUDIO", juce::Colour(0xffff5050)); lat_.setText("-"); dsp_.setText("-");
  } else {
    const double sr = ctx_.bridge.sampleRate(); const int buf = ctx_.bridge.bufferSamples();
    const double rt = 1000.0 * (buf + ctx_.bridge.deviceLatencySamples()) / sr;
    rate_.setText(juce::String(sr / 1000.0, sr == std::floor(sr / 1000.0) * 1000.0 ? 0 : 1) + "k " +
                  juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + " 32f");
    lat_.setText(juce::String(buf) + " smp " + juce::String(juce::CharPointer_UTF8("\xc2\xb7 ")) + juce::String(rt, 1) + "ms");
    dsp_.setText(juce::String(juce::roundToInt(ctx_.devices.getCpuUsage() * 100.0)) + "%");
  }
}

void Deck::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  t.drawBrush(g, r);
  g.setColour(juce::Colours::black); g.fillRect(r.removeFromBottom(2.f));
  // logo
  auto lg = getLocalBounds().reduced(16, 0).removeFromLeft(kLogoW).withHeight(getHeight()).toFloat();
  lg = lg.withSizeKeepingCentre(lg.getWidth(), 56.f);
  auto logoFont = Theme::spaced(Theme::uiFont(30.f, true), 0.3f);
  auto top = lg.removeFromTop(36.f);
  const float fullW = juce::GlyphArrangement::getStringWidth(logoFont, "AIRDAW");
  if (fullW > top.getWidth()) logoFont = logoFont.withHeight(30.f * top.getWidth() / fullW);  // wide typefaces still fit
  g.setFont(logoFont);
  const float airW = juce::GlyphArrangement::getStringWidth(logoFont, "AIR");
  g.setColour(Theme::cream()); g.drawText("AIR", top, juce::Justification::bottomLeft, false);
  t.drawGlowText(g, "DAW", top.withTrimmedLeft(airW), juce::Justification::bottomLeft, t.am(), 1.2f);
  g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(11.f), 0.16f));
  g.drawFittedText("Airwindows Preferred Recording", lg.withTrimmedTop(3.f).toNearestInt(), juce::Justification::topLeft, 1, 0.8f);
}

static constexpr int kKeysW = TransportKeys::kNum * TransportKeys::kKeyW + (TransportKeys::kNum - 1) * TransportKeys::kGap + 60;
// Right side of the deck: the readouts centred on top, below them the menu keys, all the same size and evenly spread
// (one row of eight, or two rows of four when the window is narrow).
static constexpr int kLcdH = 40, kMenuKeyH = 34, kMenuGap = 6, kMenuKeyMinW = 76;
static int menuCols(int width, int n) { return width >= n * kMenuKeyMinW + (n - 1) * kMenuGap ? n : (n + 1) / 2; }
static int rightWidth(int deckWidth) { return (deckWidth - 32 - kKeysW) / 2 - 12; }

int Deck::preferredHeight(int width) const {
  const int n = (int)buttons_.size(), cols = menuCols(rightWidth(width), n), rows = (n + cols - 1) / cols;
  return juce::jmax(100, 20 + kLcdH + 8 + rows * kMenuKeyH + (rows - 1) * kMenuGap);
}

void Deck::resized() {
  auto r = getLocalBounds().reduced(16, 10);
  const int keysW = kKeysW;
  auto centre = r.withSizeKeepingCentre(keysW, r.getHeight());
  keys_.setBounds(centre.withSizeKeepingCentre(keysW, 78));
  // left: logo (painted), position LCD
  auto left = r.withRight(centre.getX());
  left.removeFromLeft(kLogoW);
  pos_.setBounds(left.withSizeKeepingCentre(juce::jmin(left.getWidth(), 300), 74));
  // right: readouts centred, menu keys below
  auto right = r.withLeft(centre.getRight() + 12);
  auto lcds = right.removeFromTop(kLcdH);
  const int lw[3] = {108, 182, 64}, lcdTotal = lw[0] + lw[1] + lw[2] + 2 * 8;
  int x = lcds.getCentreX() - lcdTotal / 2, k = 0;
  for (auto* l : {&rate_, &lat_, &dsp_}) { l->setBounds(x, lcds.getY(), lw[k], kLcdH); x += lw[k++] + 8; }
  right.removeFromTop(8);
  const int n = (int)buttons_.size(), cols = menuCols(right.getWidth(), n);
  const int kw = (right.getWidth() - (cols - 1) * kMenuGap) / cols;
  for (int i = 0; i < n; ++i)
    buttons_[(size_t)i]->setBounds(right.getX() + (i % cols) * (kw + kMenuGap), right.getY() + (i / cols) * (kMenuKeyH + kMenuGap), kw, kMenuKeyH);
}

}  // namespace ui
