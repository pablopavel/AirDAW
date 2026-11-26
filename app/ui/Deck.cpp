#include "Deck.h"

namespace ui {

// ---------------- TransportKeys ----------------
juce::Rectangle<float> TransportKeys::keyRect(int k) const {
  const float total = kNum * kKeyW + (kNum - 1) * kGap;
  const float x0 = (getWidth() - total) / 2.f;
  return {x0 + k * (float)(kKeyW + kGap), 9.f, (float)kKeyW, (float)kKeyH};
}

void TransportKeys::paint(juce::Graphics& g) {
  auto& t = T();
  auto all = keyRect(0).getUnion(keyRect(kNum - 1));
  // warm halo behind the keys
  if (t.glow > 0.f) {
    auto halo = all.expanded(56.f, 26.f);
    g.setGradientFill(juce::ColourGradient(t.am().withAlpha(t.glowAlpha(0.32f)), halo.getCentreX(), halo.getY() + halo.getHeight() * 0.62f,
                                           t.am().withAlpha(0.f), halo.getRight(), halo.getCentreY(), true));
    g.fillEllipse(halo);
  }
  // recessed well
  auto well = all.expanded(10.f, 8.f).withTrimmedBottom(-2.f);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff040404), 0, well.getY(), juce::Colour(0xff0f0f11), 0, well.getBottom(), false));
  g.fillRoundedRectangle(well, T().rad(9.f));
  g.setColour(juce::Colour(0x1affffff)); g.drawHorizontalLine((int)well.getBottom(), well.getX() + 8, well.getRight() - 8);

  for (int k = 0; k < kNum; ++k) {
    const bool on = lit_[k], pressed = on || down_ == k;
    auto r = keyRect(k);
    if (!pressed) { g.setColour(juce::Colour(0xff070708)); g.fillRoundedRectangle(r.translated(0, 4), T().rad(5.f));
                    g.setColour(juce::Colours::black.withAlpha(0.7f)); g.fillRoundedRectangle(r.translated(0, 7), T().rad(5.f)); }
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
    g.setColour(juce::Colours::black); g.drawRoundedRectangle(r, T().rad(5.f), 1.f);
    // symbol
    const auto c = r.getCentre();
    const juce::Colour sym = on ? (k == Rec ? juce::Colours::white : juce::Colour(0xff2a1800)) : Theme::cream();
    g.setColour(sym);
    juce::Path p;
    switch (k) {
      case Rtz: p.addRectangle(c.x - 11, c.y - 8, 3, 16); p.addTriangle(c.x - 8, c.y, c.x + 1, c.y - 8, c.x + 1, c.y + 8);
                p.addTriangle(c.x + 1, c.y, c.x + 10, c.y - 8, c.x + 10, c.y + 8); g.fillPath(p); break;
      case Play: p.addTriangle(c.x - 7, c.y - 10, c.x - 7, c.y + 10, c.x + 10, c.y); g.fillPath(p); break;
      case Stop: g.fillRect(c.x - 8, c.y - 8, 16.f, 16.f); break;
      case Rec: {
        juce::Rectangle<float> d(c.x - 13, c.y - 13, 26, 26);
        t.drawGlow(g, d.reduced(3), juce::Colour(0xffe5484d), 12.f, 0.7f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffff9a9a), d.getX() + 9, d.getY() + 8, juce::Colour(0xffa81e24),
                                               d.getRight(), d.getBottom(), true));
        g.fillEllipse(d); break;
      }
      case Loop: g.setFont(Theme::uiFont(20.f)); g.drawText("LOOP", r, juce::Justification::centred); break;
      case Click: g.setFont(Theme::uiFont(17.f)); g.drawText("CLICK", r, juce::Justification::centred); break;
      default: break;
    }
  }
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
  reels_.isPlaying = [this] { auto* e = ctx_.engine(); return e && e->transport().playing.load() && ctx_.audioRunning; };
  for (auto* c : std::initializer_list<juce::Component*>{&reels_, &pos_, &rate_, &lat_, &dsp_, &keys_}) addAndMakeVisible(c);
  keys_.onKey = [this](TransportKeys::Key k) { if (onTransport) onTransport(k); };
  buttons_ = {&tap_, &listen_, &midi_, &plugins_, &projectBtn, &metersBtn, &displayBtn, &full_};
  tap_.setTriggeredOnMouseDown(true);  // the tap lands when the button goes down, not when it is released
  tap_.setTooltip("Tap tempo: tap in time (2 taps or more) to set the metronome tempo. Shows the current tempo.");
  tap_.onClick = [this] { ctx_.tapTempo(); };
  tap_.setButtonText("Tap 120.0");
  for (auto* b : buttons_) addAndMakeVisible(*b);
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
  const juce::String t = armed ? juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f Move a controller\xe2\x80\xa6")) : juce::String("MIDI Learn");
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
  auto lg = getLocalBounds().reduced(16, 0).removeFromLeft(200).withHeight(getHeight()).toFloat();
  lg = lg.withSizeKeepingCentre(lg.getWidth(), 40.f);
  g.setFont(Theme::spaced(Theme::uiFont(19.f, true), 0.35f));
  auto top = lg.removeFromTop(24.f);
  const float airW = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), "AIR");
  g.setColour(Theme::cream()); g.drawText("AIR", top, juce::Justification::bottomLeft, false);
  t.drawGlowText(g, "DAW", top.withTrimmedLeft(airW), juce::Justification::bottomLeft, t.am(), 1.2f);
  g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(9.5f), 0.2f));
  g.drawText("Airwindows Preferred Recording", lg, juce::Justification::topLeft, false);
}

// Right-aligned rows of readouts and buttons (mockup .dr2: flex-wrap, justify flex-end); returns the height used.
struct FlowItem { juce::Component* c; int w, h; };
static int flow(const std::vector<FlowItem>& items, juce::Rectangle<int> area, bool place, int vgap = 8) {
  const int gap = 8;
  int y = area.getY(), total = 0;
  std::vector<FlowItem> row; int rowW = 0;
  auto flush = [&] {
    int rowH = 0; for (auto& i : row) rowH = juce::jmax(rowH, i.h);
    int x = area.getRight() - rowW;
    for (auto& i : row) { if (place) i.c->setBounds(x, y + (rowH - i.h) / 2, i.w, i.h); x += i.w + gap; }
    y += rowH + vgap; total += rowH + vgap; row.clear(); rowW = 0;
  };
  for (auto& i : items) {
    const int need = (row.empty() ? 0 : gap) + i.w;
    if (!row.empty() && rowW + need > area.getWidth()) flush();
    rowW += (row.empty() ? 0 : gap) + i.w; row.push_back(i);
  }
  if (!row.empty()) flush();
  return total - vgap;
}

static constexpr int kKeysW = TransportKeys::kNum * TransportKeys::kKeyW + (TransportKeys::kNum - 1) * TransportKeys::kGap + 60;

static std::vector<FlowItem> deckItems(std::vector<juce::Component*> lcds, std::vector<juce::TextButton*>& buttons, bool narrow) {
  std::vector<FlowItem> v;
  const int lw[] = {108, 182, 64};
  for (size_t i = 0; i < lcds.size(); ++i) v.push_back({lcds[i], lw[i], 40});
  for (auto* b : buttons) v.push_back({b, b->getBestWidthForHeight(30) + (narrow ? 10 : 18), narrow ? 26 : 30});
  return v;
}

int Deck::preferredHeight(int width) const {
  auto bs = buttons_;
  const bool narrow = width < 1500;
  auto items = deckItems({const_cast<Lcd*>(&rate_), const_cast<Lcd*>(&lat_), const_cast<Lcd*>(&dsp_)}, bs, narrow);
  const int rightW = juce::jmax(300, (width - 32 - kKeysW) / 2);
  return juce::jmax(100, 20 + flow(items, {0, 0, rightW, 1000}, false, narrow ? 5 : 8));
}

void Deck::resized() {
  auto r = getLocalBounds().reduced(16, 10);
  const int keysW = kKeysW;
  auto centre = r.withSizeKeepingCentre(keysW, r.getHeight());
  keys_.setBounds(centre.withSizeKeepingCentre(keysW, 78));
  // left: logo (painted), reels, position LCD
  auto left = r.withRight(centre.getX());
  left.removeFromLeft(200);
  if (left.getWidth() >= 420 && T().reels) { reels_.setVisible(true); reels_.setBounds(left.removeFromLeft(150).withSizeKeepingCentre(150, 70)); left.removeFromLeft(14); }
  else reels_.setVisible(false);  // narrow window: the reels make way for the position readout
  pos_.setBounds(left.withSizeKeepingCentre(juce::jmin(left.getWidth(), 300), 74));
  // right: readouts and buttons flowing in right-aligned rows
  auto right = r.withLeft(centre.getRight());
  const bool narrow = getWidth() < 1500;
  flow(deckItems({&rate_, &lat_, &dsp_}, buttons_, narrow), right, true, narrow ? 5 : 8);
}

}  // namespace ui
