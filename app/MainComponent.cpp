#include "MainComponent.h"
#include "Vst3Insert.h"
#include "airdaw/render.h"
#include <map>
#include <thread>

using ui::T;
static constexpr int kMeterSlot = 7;  // last master insert slot: the meter listens to the final mix
static constexpr int kFooterH = 26, kOverviewH = 100, kMixerMin = 380, kInsertsMin = 150, kDockMin = 150;

// Recreates hosted plugins from session ids ("vst3:<path>"); Airwindows effects are built in.
static std::unique_ptr<airdaw::InsertProcessor> pluginFactory(const std::string& id, double sr) {
  if (id.rfind("vst3:", 0) != 0) return nullptr;
  juce::String err;
  return Vst3Insert::load(juce::File(juce::String(id.substr(5))), sr, err);
}

std::unique_ptr<juce::PropertiesFile> MainComponent::makeProps(bool snapshot) {
  juce::PropertiesFile::Options opt;
  // Test snapshots use their own settings file, so they can never change the user's settings.
  opt.applicationName = snapshot ? "AirDAW-snapshot" : "AirDAW"; opt.filenameSuffix = ".settings"; opt.osxLibrarySubFolder = "Application Support";
  opt.folderName = "AirDAW";
  if (snapshot) opt.getDefaultFile().deleteFile();  // every test run starts from the defaults
  return std::make_unique<juce::PropertiesFile>(opt);
}

MainComponent::MainComponent(bool snapshotMode) : snapshotMode_(snapshotMode) {
  if (!snapshotMode_) {  // diagnostics log, replaced at each launch
    const auto lf = props_->getFile().getParentDirectory().getChildFile("AirDAW.log");
    lf.deleteFile();
    log_ = std::make_unique<juce::FileLogger>(lf, "AirDAW started " + juce::Time::getCurrentTime().toString(true, true, true, true), 0);
    juce::Logger::setCurrentLogger(log_.get());
  }
  const double startT = juce::Time::getMillisecondCounterHiRes();
  auto stamp = [startT](const juce::String& what) {
    juce::Logger::writeToLog(juce::String((int)(juce::Time::getMillisecondCounterHiRes() - startT)) + " ms  " + what);
  };
  // Airwindows library: the built-in copy or an installed newer build (before any effect exists).
  if (snapshotMode_) {  // test snapshots keep their own library folder; a build can be preset for a picture
    ui::pluginlib::setRoot(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("airdaw-snapshot-plugins"));
    const auto b = juce::SystemStats::getEnvironmentVariable("AIRDAW_TEST_PLUGINS_ACTIVE", {});
    if (b.isNotEmpty()) props_->setValue("plugins.active", b);
  }
  pluginsPanel_.loadError = ui::pluginlib::activate(*props_);
  stamp("Airwindows library: " + ui::pluginlib::runningBuild() + (pluginsPanel_.loadError.isNotEmpty() ? " (" + pluginsPanel_.loadError + ")" : juce::String()));
  T().load(*props_);
  lnf_.refreshColours();
  juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
  juce::Desktop::getInstance().setGlobalScaleFactor(T().scalePct / 100.f);
  T().addChangeListener(this);
  tooltips_ = std::make_unique<juce::TooltipWindow>(this, 600);

  bridge_.setInsertFactory(pluginFactory);
  // Reopen the last audio device (e.g. ASIO) unless taking a test snapshot.
  std::unique_ptr<juce::XmlElement> saved = snapshotMode_ ? nullptr : props_->getXmlValue("audioDevice");
  stamp("opening audio device " + (saved ? saved->getStringAttribute("deviceType") + " \"" + saved->getStringAttribute("audioOutputDeviceName") + "\"" : juce::String("(default)")));
  juce::String err = devices_.initialise(2, 2, saved.get(), true);  // Black Lion Revolution 2x2 is 2-in/2-out
  // A driver that took this long to fail is stuck: retrying would freeze the window again (see updateDeviceStatus).
  if (devices_.getCurrentAudioDevice() == nullptr && juce::Time::getMillisecondCounterHiRes() - startT > 1500.0) reconnectStopped_ = true;
  stamp("audio device: " +(devices_.getCurrentAudioDevice() ? devices_.getCurrentAudioDevice()->getName() + " @ " + juce::String(devices_.getCurrentAudioDevice()->getCurrentSampleRate()) : juce::String("none")) + (err.isNotEmpty() ? " (" + err + ")" : juce::String()));
  if (err.isNotEmpty()) { juce::Logger::writeToLog("Audio init: " + err); initError_ = err; }
  devices_.addAudioCallback(&bridge_);
  devices_.addChangeListener(this);

  ctx_.note = [this](const juce::String& s) { note(s); };
  ctx_.requireAudio = [this](const char* w) { return requireAudio(w); };
  ctx_.showInserts = [this](int ch, int slot) {
    if (inserts_.isVisible() && inserts_.channel() == ch && slot < 0) { inserts_.close(); return; }
    inserts_.show(ch, slot);
  };
  ctx_.removeTrack = [this](int t) { removeTrack(t); };
  mixer_.onRemovedChanged = [this] { setProjectValue("removedTracks", mixer_.removedTracks()); };
  ctx_.isCardOpen = [this](int ch, int slot) { return inserts_.isOpen(ch, slot); };
  ctx_.insertPanelChannel = [this] { return inserts_.isVisible() ? inserts_.channel() : -1; };
  ctx_.layoutChanged = [this] { resized(); };
  ctx_.openPluginWindow = [this](int ch, int slot) { openPluginWindow(ch, slot); };
  mixer_.master().onResetLoudness = [this] { resetMeters(); };
  tools_.onLayoutChanged = [this] { dock_.rebuildSuite(); listenRow_.rebuild(); resized(); };
  dock_.rebuildSuite();
  ctx_.resetPeaks = [this] { if (hub_) hub_->resetPeaks(); };
  ctx_.loadReference = [this] { chooseReference(); };
  deck_.onListen = [this] { setListen(!listen_); };
  addChildComponent(listenRow_);
  ctx_.showPluginInfo = [this](const juce::String& name) {
    if (!pluginInfo_) pluginInfo_ = std::make_unique<ui::PluginInfoWindow>();
    pluginInfo_->show(name);
  };
  // Deferred: the move rebuilds the insert cards, one of which is still handling the drop.
  ctx_.moveInsert = [this](int ch, int from, int to) {
    if (midi_) midi_->insertMoved(ch, from, to);
    juce::Component::SafePointer<MainComponent> sp(this);
    juce::MessageManager::callAsync([sp, ch, from, to] { if (sp) sp->inserts_.moveInsert(ch, from, to); });
  };

  deck_.onTransport = [this](ui::TransportKeys::Key k) { transport(k); };
  deck_.onProject = [this] { showProjectMenu(); };
  deck_.onAudio = [this] { showAudioSettings(); };
  deck_.onDisplay = [this] { toggleDisplayPanel(); };
  deck_.onMeters = [this] { dockShown_ = !dockShown_; props_->setValue("dockShown", dockShown_); resized(); };
  deck_.onFull = [this] {
    auto& d = juce::Desktop::getInstance();
    d.setKioskModeComponent(d.getKioskModeComponent() ? nullptr : getTopLevelComponent(), false);
  };
  dock_.onLoadMeter = [this] { chooseMeterPlugin(); };
  inserts_.setVisible(false);
  // The divider drags from the height the dock has on screen. With the insert panel open the dock keeps a height of
  // its own, so it can be resized there too without changing its usual height.
  splitter_.current = [this] { return dock_.isVisible() ? dock_.getHeight() + 10 : inserts_.isVisible() ? dockHeightIns_ : dockHeight_; };
  splitter_.onDrag = [this](int h) {
    h = juce::jlimit(kDockMin, 1400, h);
    if (inserts_.isVisible()) { dockHeightIns_ = h; props_->setValue("dockHeightIns", h); }
    else { dockHeight_ = h; props_->setValue("dockHeight", h); }
    resized();
  };
  splitter_.onDragState = [this](bool on) { dock_.setLiveResize(on); };
  splitter_.setTooltip("Drag to resize the meter section");
  display_.setVisible(false);
  pluginsPanel_.setVisible(false);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&deck_, &overview_, &inserts_, &mixer_, &splitter_, &dock_})
    addAndMakeVisible(c);
  addChildComponent(takes_);
  addChildComponent(display_);
  addChildComponent(pluginsPanel_);
  deck_.onPlugins = [this] { togglePluginsPanel(); };
  pluginsPanel_.onUpdateAvailable = [this](bool on) { deck_.setPluginsBadge(on); };
  pluginsPanel_.layoutChanged = [this] { resized(); };
  inserts_.setVisible(false);

  dockHeight_ = props_->getIntValue("dockHeight", 330);
  dockHeightIns_ = props_->getIntValue("dockHeightIns", 0);
  dockShown_ = props_->getBoolValue("dockShown", true);
  autoOn_ = props_->getBoolValue("autosaveOn", false);
  const int secs[] = {0, 60, 120, 300, 600, 900, 1800};
  const int savedSec = props_->getIntValue("autosaveSec", 300);
  for (int i = 1; i <= 6; ++i) if (secs[i] == savedSec) autoId_ = i;

  setWantsKeyboardFocus(true);
  addKeyListener(this);
  {  // MIDI Learn
    ui::MidiControl::Host h;
    h.engine = [this] { return bridge_.engine(); };
    h.root = [this] { return (juce::Component*)this; };
    h.press = [this](const juce::String& id) {
      static const char* keys[] = {"transport.rtz", "transport.play", "transport.stop", "transport.rec", "transport.loop", "transport.click"};
      for (int k = 0; k < 6; ++k) if (id == keys[k]) { transport((ui::TransportKeys::Key)k); return true; }
      if (id == "tap") { ctx_.tapTempo(); return true; }
      return false;
    };
    h.projectData = [this] { return projectData(); };
    h.setProjectValue = [this](const juce::String& k, const juce::var& v) { setProjectValue(k, v); };
    h.note = [this](const juce::String& s) { note(s); };
    h.changed = [this] { midiChanged(); };
    stamp("MIDI: opening controllers");
    midi_ = std::make_unique<ui::MidiControl>(devices_, *props_, std::move(h), !snapshotMode_);
    stamp("MIDI: ready");
    learnOverlay_ = std::make_unique<ui::MidiLearnOverlay>(*midi_, *this);
    addChildComponent(*learnOverlay_);
    learnOverlay_->onShowList = [this] { showMidiMappings(); };
    learnOverlay_->passThrough = [this] { return getLocalArea(&deck_.midiButton(), deck_.midiButton().getLocalBounds()); };
    deck_.onMidi = [this] { midi_->setLearning(!midi_->learning()); };
  }
  setSize(1600, 1000);
  startTimerHz(30);
}

MainComponent::~MainComponent() {
  stopTimer();
  juce::Logger::writeToLog("AirDAW closing");
  T().removeChangeListener(this);
  devices_.removeChangeListener(this);
  pluginWindow_.reset();
  pluginInfo_.reset();
  settings_.reset();
  dock_.setPlugin(nullptr);
  devices_.removeAudioCallback(&bridge_);
  if (!snapshotMode_) props_->saveIfNeeded();
  juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
  juce::Logger::setCurrentLogger(nullptr);
}

// ---------------------------------------------------------------- look
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* src) {
  if (src == &T()) { applyTheme(); return; }
  // Remember the device setup (e.g. ASIO, 96 kHz, 64 samples), but only while a device is actually open: when the
  // interface drops out (a USB hiccup, another device plugged in) the setup goes empty, and saving that would make
  // every later launch start with no device.
  if (src == &devices_ && !snapshotMode_ && devices_.getCurrentAudioDevice() != nullptr) {
    if (auto xml = devices_.createStateXml()) { props_->setValue("audioDevice", xml.get()); props_->saveIfNeeded(); }
  }
}

void MainComponent::applyTheme() {
  lnf_.refreshColours();
  juce::Desktop::getInstance().setGlobalScaleFactor(T().scalePct / 100.f);
  if (!snapshotMode_) T().save(*props_);
  display_.syncFromTheme();
  sendLookAndFeelChange();
  resized(); deck_.resized(); mixer_.restyle();  // trim width, strip rows (Buttons option)
  if (!T().pixelShift) setTransform({});
  if (auto* top = getTopLevelComponent()) top->repaint();
  if (settings_) settings_->repaint();
}

void MainComponent::togglePluginsPanel() {
  pluginsPanel_.setVisible(!pluginsPanel_.isVisible());
  if (pluginsPanel_.isVisible() && display_.isVisible()) toggleDisplayPanel();
  if (pluginsPanel_.isVisible()) pluginsPanel_.toFront(false);
  resized();
}

void MainComponent::toggleDisplayPanel() {
  if (!display_.isVisible() && pluginsPanel_.isVisible()) pluginsPanel_.setVisible(false);
  display_.setVisible(!display_.isVisible());
  deck_.displayBtn.setToggleState(display_.isVisible(), juce::dontSendNotification);
  if (display_.isVisible()) { display_.syncFromTheme(); display_.toFront(false); }
  resized();
}

void MainComponent::showAudioSettings() {
  if (!settings_) {
    settings_ = std::make_unique<ui::SettingsWindow>(ctx_, [this](ui::SettingsContent& c) {
      c.onAutoSave = [this](bool on, int id) { autoOn_ = on; autoId_ = juce::jlimit(1, 6, id); applyAutoSaveSettings();
                                                note(on ? "Auto-save on" : "Auto-save off"); };
      c.autoSaveState = [this] { return std::pair<bool, int>(autoOn_, autoId_); };
      c.autoSaveInfo = [this] { return lastAutoSave_; };
    });
  }
  settings_->setVisible(true);
  settings_->toFront(true);
}

// ---------------------------------------------------------------- layout & paint
void MainComponent::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds();
  g.fillAll(juce::Colours::black);
  const int trimW = t.trimW(), railH = t.railH();
  auto col = r.reduced(trimW, 0).withTrimmedBottom(railH);
  g.setColour(t.bg()); g.fillRect(col);
  // warm wash from the top centre (mockup #col background)
  g.setGradientFill(juce::ColourGradient(t.am().withAlpha(t.glowAlpha(0.16f)), (float)col.getCentreX(), (float)col.getY() - col.getHeight() * 0.1f,
                                         juce::Colours::transparentBlack, (float)col.getCentreX(), (float)col.getY() + col.getHeight() * 0.55f, true));
  g.fillRect(col);
  // side trim and bottom rail (Display > Side trim)
  t.drawTrim(g, r.withWidth(trimW), true, true);
  t.drawTrim(g, r.withLeft(r.getRight() - trimW), true, false);
  t.drawTrim(g, col.withY(col.getBottom()).withHeight(railH), false, false);
  if (t.screws) {  // mockup body.scr footer
    const float fy = (float)col.getBottom() - kFooterH / 2.f + 1.f;
    t.drawScrew(g, {(float)col.getX() + 9.f, fy}); t.drawScrew(g, {(float)col.getRight() - 9.f, fy});
  }
  // footer: device status (left, red when audio is not running) and the latest notice (right)
  auto foot = col.removeFromBottom(kFooterH);
  g.setColour(juce::Colours::black); g.fillRect(foot.removeFromTop(2));
  foot = foot.reduced(T().screws ? 22 : 16, 0);
  g.setFont(ui::Theme::spaced(ui::Theme::uiFont(11.5f), 0.08f));
  const bool showNote = juce::Time::getMillisecondCounter() < noticeUntil_;
  if (showNote) {
    g.setColour(noticeWarn_ ? juce::Colour(0xffff5050) : t.am());
    g.drawText(notice_, foot.removeFromRight(foot.getWidth() / 2), juce::Justification::centredRight, true);
  }
  g.setColour(audioRunning_ ? ui::Theme::dim() : juce::Colour(0xffff5050));
  g.drawText(status_, foot, juce::Justification::centredLeft, true);
}

void MainComponent::resized() {
  if (learnOverlay_) learnOverlay_->setBounds(getLocalBounds());
  auto r = getLocalBounds().reduced(T().trimW(), 0).withTrimmedBottom(T().railH() + kFooterH);
  deck_.setBounds(r.removeFromTop(deck_.preferredHeight(r.getWidth())));
  overview_.setBounds(r.removeFromTop(getHeight() >= 950 ? kOverviewH : 70));  // short windows: slimmer overview
  takes_.setVisible(takes_.wanted());
  if (takes_.isVisible()) takes_.setBounds(r.removeFromTop(ui::TakesBar::kHeight));
  if (display_.isVisible())
    display_.setBounds(r.getRight() - ui::DisplayPanel::kWidth - 16, deck_.getBottom() - 6, ui::DisplayPanel::kWidth,
                       juce::jmin(display_.preferredHeight(), getHeight() - deck_.getBottom() - 20));
  if (pluginsPanel_.isVisible())
    pluginsPanel_.setBounds(r.getRight() - ui::PluginsPanel::kWidth - 16, deck_.getBottom() - 6, ui::PluginsPanel::kWidth,
                            juce::jmin(pluginsPanel_.preferredHeight(), getHeight() - deck_.getBottom() - 20));
  r = r.reduced(12, 0);
  r.removeFromTop(10);
  // Space is shared mixer-first (it keeps kMixerMin). Without the insert panel the dock gets its height. With it, the
  // dock keeps a height of its own (dragged there; by default what the panel leaves, at least kDockMin) and the panel
  // gets the rest up to what it needs (it scrolls when it gets less).
  const int avail = r.getHeight() - 10;
  int ins = 0, dock = 0;
  if (inserts_.isVisible()) {
    const int insPref = inserts_.preferredHeight();
    if (dockShown_) {
      int want = dockHeightIns_ > 0 ? dockHeightIns_ : juce::jmin(dockHeight_, avail - kMixerMin - insPref - 24);
      dock = juce::jmin(juce::jmax(kDockMin, want), avail - kMixerMin - kInsertsMin - 24);
      if (dock < kDockMin) dock = 0;
    }
    ins = juce::jmin(insPref, juce::jmax(kInsertsMin, avail - kMixerMin - 10 - (dock > 0 ? dock + 14 : 0)));
  } else if (dockShown_) {
    dock = juce::jmin(dockHeight_, avail - kMixerMin - 14);
    if (dock < kDockMin) dock = 0;
  }
  if (inserts_.isVisible()) { inserts_.setBounds(r.removeFromTop(ins)); r.removeFromTop(10); }
  listenRow_.setVisible(listen_);
  if (listen_) {  // Listen mode: big master left; reference suite top right; every other tool in a row below
    mixer_.setBounds(r.removeFromLeft(390)); r.removeFromLeft(12);
    listenRow_.setBounds(r.removeFromBottom(juce::jlimit(210, 300, r.getHeight() * 2 / 5)).withTrimmedBottom(10));
    r.removeFromBottom(10);
    dock_.setVisible(true); splitter_.setVisible(false); dock_.setBounds(r);
    deck_.metersBtn.setToggleState(true, juce::dontSendNotification);
    return;
  }
  if (dock > 0) {
    dock_.setVisible(true); splitter_.setVisible(true);
    dock_.setBounds(r.removeFromBottom(dock).withTrimmedBottom(10)); splitter_.setBounds(r.removeFromBottom(14));
  } else { dock_.setVisible(false); splitter_.setVisible(false); }
  deck_.metersBtn.setToggleState(dockShown_, juce::dontSendNotification);
  mixer_.setBounds(r);
}

// ---------------------------------------------------------------- transport & keys
void MainComponent::transport(ui::TransportKeys::Key k) {
  auto* e = bridge_.engine(); if (!e) return;
  auto& tr = e->transport();
  switch (k) {
    case ui::TransportKeys::Rtz: tr.seek(0); break;
    case ui::TransportKeys::Play:
      if (tr.playing.load() && !tr.recording.load()) { tr.stop(); break; }  // second press pauses
      if (requireAudio("Play")) tr.play();
      break;
    case ui::TransportKeys::Stop:
      if (!tr.playing.load()) { tr.seek(0); break; }  // stop while stopped returns to the start
      tr.stop(); e->flushRecording(); collectTakes();
      break;
    case ui::TransportKeys::Rec: {
      if (tr.recording.load()) { e->stopRecording(); break; }  // punch out, keep playing
      if (!requireAudio("Recording")) return;
      bool any = false;
      for (int t = 0; t < airdaw::kMaxTracks; ++t) any |= e->track(t).armed.load();
      if (!any) { note("No track is armed: press R on a track first", true); return; }
      auto dir = sessionFolder().getChildFile("audio"); dir.createDirectory();
      e->startRecording(dir.getFullPathName().toStdString());
      break;
    }
    case ui::TransportKeys::Click:
      e->metronome().enabled = !e->metronome().enabled.load();
      if (e->metronome().enabled.load() && requireAudio("Metronome"))
        note("Metronome on: " + juce::String(e->metronome().bpm.load(), 1) + " BPM (tempo, beats and count-in: Project > Audio settings)");
      break;
    case ui::TransportKeys::Loop:
      if (tr.loopOut.load() <= tr.loopIn.load()) { note("Shift+drag on the overview to set a loop region"); break; }
      tr.loopOn = !tr.loopOn.load();
      break;
    default: break;
  }
}

bool MainComponent::keyPressed(const juce::KeyPress& k, juce::Component*) {
  const auto m = k.getModifiers();
  if (m.isCommandDown() && k.getKeyCode() == 'S') { saveSession(m.isShiftDown()); return true; }
  if (m.isCommandDown() && k.getKeyCode() == 'O') { openSession(); return true; }
  if (m.isCommandDown() && k.getKeyCode() == 'N') { newSession(); return true; }
  if (m.isCommandDown() && k.getKeyCode() == 'E') { exportMix(); return true; }
  if (k == juce::KeyPress::spaceKey) {
    auto* e = bridge_.engine();
    transport(e && e->transport().playing.load() ? ui::TransportKeys::Stop : ui::TransportKeys::Play);
    return true;
  }
  if (k == juce::KeyPress::escapeKey) {
    if (display_.isVisible()) { toggleDisplayPanel(); return true; }
    if (pluginsPanel_.isVisible()) { togglePluginsPanel(); return true; }
    if (inserts_.isVisible()) { inserts_.close(); return true; }
  }
  return false;
}

void MainComponent::showProjectMenu() {
  juce::PopupMenu m;
  m.addSectionHeader("Project: " + sessionName());
  m.addItem(1, "New", true, false); m.addItem(2, juce::String(juce::CharPointer_UTF8("Open\xe2\x80\xa6")));
  m.addItem(3, "Save", true, false); m.addItem(4, juce::String(juce::CharPointer_UTF8("Save As\xe2\x80\xa6")));
  m.addSeparator();
  m.addItem(8, juce::String(juce::CharPointer_UTF8("Export mix\xe2\x80\xa6")) + "   (Ctrl+E)");
  m.addSeparator();
  m.addItem(7, juce::String(juce::CharPointer_UTF8("Audio settings\xe2\x80\xa6")));
  m.addItem(5, juce::String(juce::CharPointer_UTF8("Load Hit Record Meter\xe2\x80\xa6")));
  m.addItem(6, "Show project folder", sessionFile_ != juce::File());
  m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&deck_.projectBtn), [this](int r) {
    switch (r) {
      case 1: newSession(); break; case 2: openSession(); break; case 3: saveSession(false); break; case 4: saveSession(true); break;
      case 5: chooseMeterPlugin(); break; case 6: sessionFolder().revealToUser(); break; case 7: showAudioSettings(); break; case 8: exportMix(); break; default: break;
    }
  });
}

// ---------------------------------------------------------------- status
void MainComponent::note(const juce::String& s, bool warn) {
  notice_ = s; noticeWarn_ = warn || s.startsWith("WARNING") || s.containsIgnoreCase("FAILED");
  noticeUntil_ = juce::Time::getMillisecondCounter() + 6000;
  repaint(0, getHeight() - T().railH() - kFooterH, getWidth(), kFooterH);
}

bool MainComponent::requireAudio(const char* what) {
  if (audioRunning_) return true;
  note(juce::String(what) + " needs a running audio device: see the status at the bottom left, or open Audio", true);
  return false;
}

void MainComponent::updateDeviceStatus() {
  auto* dev = devices_.getCurrentAudioDevice();
  const double now = juce::Time::getMillisecondCounterHiRes();
  const auto cb = bridge_.callbacks.load(std::memory_order_relaxed);
  if (cb != lastCallbacks_) { lastCallbacks_ = cb; lastCallbackSeen_ = now; }
  audioRunning_ = dev != nullptr && dev->isPlaying() && cb != 0 && now - lastCallbackSeen_ < 1000.0;
  ctx_.audioRunning = audioRunning_;
  // The saved device dropped out or failed to open: try it again, waiting longer each time (5 s, 10 s, 20 s ... 60 s).
  // Opening a driver blocks the window, so an attempt that takes more than ~1 s means the driver itself is stuck:
  // then AirDAW stops trying (the interface needs replugging / power-cycling) instead of freezing over and over.
  reconnecting_ = false;
  if (dev != nullptr) { reconnectDelay_ = 5000.0; reconnectStopped_ = false; }
  juce::String savedType, savedOut, savedIn;
  if (dev == nullptr && !snapshotMode_) {
    if (auto saved = props_->getXmlValue("audioDevice")) {
      savedType = saved->getStringAttribute("deviceType");
      savedOut = saved->getStringAttribute("audioOutputDeviceName"); savedIn = saved->getStringAttribute("audioInputDeviceName");
      if (savedType.isNotEmpty() && (savedOut.isNotEmpty() || savedIn.isNotEmpty()) && !reconnectStopped_) {
        reconnecting_ = true;
        if (lastReconnect_ == 0) lastReconnect_ = now;  // the startup attempt counts as the first one
        if (now - lastReconnect_ > reconnectDelay_) {
          const double t0 = juce::Time::getMillisecondCounterHiRes();
          devices_.setCurrentAudioDeviceType(savedType, true);
          auto setup = devices_.getAudioDeviceSetup();
          setup.outputDeviceName = savedOut; setup.inputDeviceName = savedIn;
          setup.sampleRate = saved->getDoubleAttribute("audioDeviceRate", setup.sampleRate);
          setup.bufferSize = saved->getIntAttribute("audioDeviceBufferSize", setup.bufferSize);
          setup.useDefaultInputChannels = setup.useDefaultOutputChannels = true;
          const auto err = devices_.setAudioDeviceSetup(setup, true);
          const double took = juce::Time::getMillisecondCounterHiRes() - t0;
          juce::Logger::writeToLog("Reconnect " + savedType + " \"" + savedOut + "\": " + (err.isEmpty() ? juce::String("ok") : err) +
                                   " (" + juce::String((int)took) + " ms)");
          lastReconnect_ = juce::Time::getMillisecondCounterHiRes();
          reconnectDelay_ = juce::jmin(60000.0, reconnectDelay_ * 2.0);
          if (err.isEmpty() && devices_.getCurrentAudioDevice()) {
            note("Audio device reconnected: " + (savedOut.isNotEmpty() ? savedOut : savedIn));
            dev = devices_.getCurrentAudioDevice(); reconnecting_ = false;
          } else if (took > 1000.0) { reconnectStopped_ = true; reconnecting_ = false; }
        }
      }
    }
  }
  juce::String text;
  if (dev == nullptr && reconnecting_) {
    text = "AUDIO DEVICE NOT AVAILABLE: " + savedType + " \"" + savedOut + "\". Trying again in a moment (check the interface's USB cable and power).";
  } else if (dev == nullptr && reconnectStopped_) {
    text = "AUDIO DEVICE NOT ANSWERING: " + savedType + " \"" + savedOut + "\" is stuck. Unplug the interface (or switch it off) for 10 seconds, "
           "plug it back in, then choose it under Audio.";
  } else if (dev == nullptr) {
    auto setup = devices_.getAudioDeviceSetup();
    auto name = setup.outputDeviceName.isNotEmpty() ? setup.outputDeviceName : setup.inputDeviceName;
    text = "AUDIO DEVICE FAILED TO OPEN: " + devices_.getCurrentAudioDeviceType() + (name.isNotEmpty() ? " \"" + name + "\"" : juce::String()) +
           (initError_.isNotEmpty() ? " (" + initError_ + ")" : juce::String()) +
           ". No sound, no recording. Close other audio apps, replug the interface, or choose another device under Audio.";
  } else if (!audioRunning_) {
    text = "AUDIO DEVICE NOT RUNNING: " + dev->getTypeName() + " \"" + dev->getName() + "\" is open but not delivering audio. "
           "Close other audio apps, replug the interface, or try another buffer size under Audio.";
  } else {
    text = dev->getTypeName() + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + dev->getName();
  }
  if (text != status_) { status_ = text; repaint(0, getHeight() - T().railH() - kFooterH, getWidth(), kFooterH); }
}

// ---------------------------------------------------------------- projects
juce::File MainComponent::sessionFolder() const {
  if (sessionFile_ != juce::File()) return sessionFile_.getParentDirectory();
  return scratchFolder();  // unsaved session: takes and auto-saves go here until Save As makes a project folder
}
juce::String MainComponent::sessionName() const {
  return sessionFile_ != juce::File() ? sessionFile_.getFileNameWithoutExtension() : juce::String("Untitled");
}
juce::File MainComponent::projectsRoot() {
  return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("AirDAW");
}
juce::File MainComponent::scratchFolder() { return projectsRoot().getChildFile("Untitled"); }

void MainComponent::setSessionFile(const juce::File& f) {
  sessionFile_ = f;
  if (autoSaver_) autoSaver_->setLocation(sessionFolder().getChildFile("autosave").getFullPathName().toStdString(), sessionName().toStdString());
  if (auto* top = getTopLevelComponent()) top->setName("AirDAW - " + sessionName());
  lastAutoSave_ = autoOn_ ? "No auto-save yet for " + sessionName() : juce::String("Auto-save is off");
  ctx_.notes.load(sessionFolder().getChildFile("listening-notes.json"));  // listening notes live beside the project
}

void MainComponent::midiChanged() {
  const bool on = midi_ && midi_->learning();
  deck_.setMidiState(on, on && midi_->armed().isNotEmpty());
  if (learnOverlay_ && on != learnOverlay_->isVisible()) {
    learnOverlay_->setBounds(getLocalBounds());
    learnOverlay_->setVisible(on);
    if (on) { learnOverlay_->toFront(false); learnOverlay_->start(); } else learnOverlay_->stop();
  }
  if (learnOverlay_ && on) learnOverlay_->repaint();
  if (midiWindow_ && midiWindow_->isVisible()) midiWindow_->rebuild();
}

void MainComponent::showMidiMappings() {
  if (!midi_) return;
  if (!midiWindow_) midiWindow_ = std::make_unique<ui::MidiMappingsWindow>(*midi_);
  midiWindow_->rebuild();
  midiWindow_->setVisible(true);
  midiWindow_->toFront(true);
}

void MainComponent::setListen(bool on) {
  listen_ = on;
  mixer_.setListenMode(on);
  deck_.setListenLit(on);
  listenRow_.rebuild();
  dock_.rebuildSuite();
  resized();
}

void MainComponent::applyAutoSaveSettings() {
  const int secs[] = {0, 60, 120, 300, 600, 900, 1800};
  airdaw::AutoSaveSettings s;
  s.enabled = autoOn_; s.intervalSec = secs[juce::jlimit(1, 6, autoId_)]; s.keep = 10;
  if (autoSaver_) autoSaver_->configure(s);
  if (!s.enabled) lastAutoSave_ = "Auto-save is off";
  else if (!lastAutoSave_.startsWith("Last auto-save")) lastAutoSave_ = "No auto-save yet for " + sessionName() + " (first one after the interval)";
  if (!snapshotMode_) { props_->setValue("autosaveOn", s.enabled); props_->setValue("autosaveSec", s.intervalSec); props_->saveIfNeeded(); }
}

void MainComponent::newSession() {
  juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon)
      .withTitle("New project").withMessage("Discard the current mixer state and start a new project?")
      .withButton("New project", 1).withButton("Cancel", 0),
      [this](int r) {
        if (r != 1) return;
        auto* e = bridge_.engine(); if (!e) return;
        e->transport().stop();
        airdaw::Engine blank(e->sampleRate());
        auto tmp = (juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("airdaw_blank.airdaw")).getFullPathName().toStdString();
        airdaw::saveSession(blank, tmp); airdaw::loadSession(*e, tmp);
        inserts_.close(); ctx_.viewTrack = -1; resetMeters();
        setSessionFile({}); note("New project");
        restoreProjectReference(false);
        if (midi_) midi_->loadProjectMappings();
      });
}

void MainComponent::saveSession(bool saveAs) {
  auto* e = bridge_.engine(); if (!e) return;
  if (!saveAs && sessionFile_ != juce::File()) {
    auto r = airdaw::saveSession(*e, sessionFile_.getFullPathName().toStdString());
    if (r.ok) note("Saved " + sessionFile_.getFileName()); else note("SAVE FAILED: " + r.error);
    return;
  }
  if (e->transport().recording.load() || e->transport().countingIn()) { note("Stop recording before Save As (it moves take files)", true); return; }
  auto root = projectsRoot(); root.createDirectory();
  const auto suggested = sessionFile_ == juce::File() ? juce::String("My Song") : sessionName() + " copy";
  chooser_ = std::make_shared<juce::FileChooser>("Save project as (AirDAW creates a folder with this name)",
                                                 root.getChildFile(suggested + ".airdaw"), "*.airdaw");
  chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
      [this](const juce::FileChooser& c) { auto f = c.getResult(); if (f != juce::File()) saveProjectAs(f.withFileExtension("airdaw")); });
}

// Every project is a folder:  <Name>/<Name>.airdaw,  <Name>/audio/ (takes),  <Name>/autosave/.
// Takes the session uses are brought into <Name>/audio: moved out of the Untitled scratch folder
// (they belong to this unsaved session), copied from anywhere else (another project still needs them).
void MainComponent::saveProjectAs(const juce::File& chosen) {
  auto* e = bridge_.engine(); if (!e) return;  // fetched now: the engine may have been rebuilt while the dialog was open
  if (e->transport().recording.load() || e->transport().countingIn()) { note("Stop recording before Save As", true); return; }
  const auto name = chosen.getFileNameWithoutExtension();
  auto dir = chosen.getParentDirectory();
  if (dir.getFileName() != name) dir = dir.getChildFile(name);  // user picked the parent folder: create the project folder
  const auto file = dir.getChildFile(name + ".airdaw");
  if (file.existsAsFile() && file != sessionFile_) { note("A project called \"" + name + "\" already exists there: choose another name", true); return; }
  auto audio = dir.getChildFile("audio");
  if (!audio.createDirectory()) { note("SAVE FAILED: cannot create " + audio.getFullPathName()); return; }

  const bool fromScratch = sessionFile_ == juce::File();
  int moved = 0, copied = 0, failed = 0;
  std::map<juce::String, juce::String> relocated;  // old path -> new path (a file is only moved once)
  for (int t = 0; t < airdaw::kMaxTracks; ++t)
    for (int i = 0; i < e->numTakes(t); ++i) {
      const juce::File src(juce::String(e->takeAt(t, i)->path));
      if (src.isAChildOf(audio)) continue;
      auto it = relocated.find(src.getFullPathName());
      if (it == relocated.end()) {
        if (!src.existsAsFile()) { ++failed; continue; }
        auto dst = audio.getChildFile(src.getFileName());
        if (dst.exists()) dst = dst.getNonexistentSibling(false);
        const bool move = fromScratch && src.isAChildOf(scratchFolder());
        if (!(move ? src.moveFileTo(dst) : src.copyFileTo(dst))) { ++failed; continue; }
        ++(move ? moved : copied);
        it = relocated.emplace(src.getFullPathName(), dst.getFullPathName()).first;
      }
      e->setTakePath(t, i, it->second.toStdString());
    }

  auto r = airdaw::saveSession(*e, file.getFullPathName().toStdString());
  if (!r.ok) { note("SAVE FAILED: " + r.error); return; }
  if (fromScratch) {  // this session's crash copies follow it, so they never prompt for recovery in Untitled again
    auto oldAuto = scratchFolder().getChildFile("autosave"), newAuto = dir.getChildFile("autosave");
    for (auto& a : oldAuto.findChildFiles(juce::File::findFiles, false, "Untitled_autosave_*.airdaw"))
      if (newAuto.createDirectory()) a.moveFileTo(newAuto.getChildFile(a.getFileName()));
  }
  const auto oldNotes = ctx_.notes;  // listening notes travel with the project
  setSessionFile(file);
  if (ctx_.notes.items.empty() && !oldNotes.items.empty()) {
    ctx_.notes.items = oldNotes.items; ctx_.notes.save();
    if (fromScratch) oldNotes.file.deleteFile();
  }
  juce::String msg = "Saved project " + name;
  if (moved) msg << ", moved " << moved << " take(s) into its audio folder";
  if (copied) msg << ", copied " << copied << " take(s) into its audio folder";
  if (failed) msg << ". WARNING: " << failed << " take file(s) missing or could not be copied";
  note(msg);
}

void MainComponent::openSession() {
  chooser_ = std::make_shared<juce::FileChooser>("Open project", sessionFile_ == juce::File() ? projectsRoot() : sessionFolder().getParentDirectory(), "*.airdaw");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
      [this](const juce::FileChooser& c) { auto f = c.getResult(); if (f != juce::File()) openSessionFile(f); });
}

void MainComponent::openSessionFile(const juce::File& f) {
  auto* e = bridge_.engine(); if (!e) return;
  auto r = airdaw::loadSession(*e, f.getFullPathName().toStdString());
  if (!r.ok) { note("OPEN FAILED: " + r.error); return; }
  inserts_.close(); ctx_.viewTrack = -1; resetMeters();
  setSessionFile(f);
  restoreProjectReference(true);
  if (midi_) midi_->loadProjectMappings();
  note(r.warnings.empty() ? "Opened " + f.getFileName() : "Opened with " + juce::String((int)r.warnings.size()) + " warning(s): " + r.warnings[0],
       !r.warnings.empty());
}

void MainComponent::checkRecovery() {
  // If an auto-save is newer than the session file (crash/power loss), offer to restore it.
  auto dir = sessionFolder().getChildFile("autosave").getFullPathName().toStdString();
  auto latest = airdaw::AutoSaver::latestAutosave(dir, sessionName().toStdString());
  if (latest.empty()) return;
  juce::File lf(latest);
  if (sessionFile_ != juce::File() && sessionFile_.getLastModificationTime() >= lf.getLastModificationTime()) return;
  juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon)
      .withTitle("Recover auto-save?")
      .withMessage("An auto-saved copy from " + lf.getLastModificationTime().toString(true, true) + " is newer than your last save. Restore it?")
      .withButton("Restore", 1).withButton("Ignore", 0),
      [this, lf](int r) {
        if (r != 1) return;
        if (auto* e = bridge_.engine()) {
          auto res = airdaw::loadSession(*e, lf.getFullPathName().toStdString());
          note(res.ok ? "Recovered auto-save" : "RECOVERY FAILED: " + res.error);
          if (res.ok) { restoreProjectReference(true); if (midi_) midi_->loadProjectMappings(); }
        }
      });
}

void MainComponent::collectTakes() {
  auto* e = bridge_.engine(); if (!e) return;
  for (auto& t : e->pollFinishedTakes()) {
    int idx = e->addTake(t.track, t.path, t.start);
    if (idx >= 0) {
      // A loop or punch pass recorded over a longer take is comped into its region, so the rest of the song keeps
      // playing; otherwise (first take, or a pass over a take of the same span) it becomes the track's take.
      const auto* tk = e->takeAt(t.track, idx);
      const auto* base = e->track(t.track).take.load();
      const juce::int64 a = tk->start, z = tk->start + (juce::int64)tk->l.size();
      const bool longerBase = base && (base->start < a - 1 || base->start + (juce::int64)base->l.size() > z + 1);
      if (longerBase) e->setCompSection(t.track, a, z, tk); else e->setActiveTake(t.track, idx);
    }
    if (t.droppedFrames) note("WARNING: disk fell behind, " + juce::String((int)t.droppedFrames) + " frames dropped on track " + juce::String(t.track + 1));
  }
  for (auto& err : e->pollRecordingErrors()) note("RECORDING FAILED: " + juce::String::fromUTF8(err.c_str()) + " - the take was not saved", true);
}

// ---------------------------------------------------------------- mix export
// Renders on its own engine (a copy of the current state, unsaved changes included) on a background
// thread, so the live engine and the audio device are untouched. Deletes itself when finished.
class ExportJob : public juce::ThreadWithProgressWindow {
 public:
  ExportJob(std::unique_ptr<airdaw::Engine> e, juce::File out, airdaw::RenderOptions o, bool reveal, std::function<void(const juce::String&, bool)> report)
      : ThreadWithProgressWindow("Exporting mix", true, true, 10000, "Cancel"), e_(std::move(e)), out_(std::move(out)), o_(o), reveal_(reveal),
        report_(std::move(report)) {
    setStatusMessage("Rendering " + out_.getFileName() + juce::String(juce::CharPointer_UTF8("\xe2\x80\xa6")));
  }
  void run() override {
    r_ = airdaw::renderMix(*e_, out_.getFullPathName().toStdString(), o_, [this](double f) { setProgress(f); return !threadShouldExit(); });
  }
  void threadComplete(bool cancelled) override {
    e_.reset();  // hosted plugins are released on the message thread
    if (r_.ok) {
      const float pk = r_.peak > 0.f ? 20.f * std::log10(r_.peak) : -200.f;
      report_("Exported " + out_.getFileName() + "  (" + juce::String((double)r_.frames / o_.sampleRate, 1) + " s, peak " + juce::String(pk, 1) + " dBFS" +
                  (pk > -0.1f ? ", CLIPPING: lower the master" : "") + ")", pk > -0.1f);
      if (reveal_) out_.revealToUser();
    } else report_(cancelled || r_.error == "cancelled" ? juce::String("Export cancelled") : "EXPORT FAILED: " + juce::String(r_.error), !cancelled);
    delete this;
  }
 private:
  std::unique_ptr<airdaw::Engine> e_;
  juce::File out_;
  airdaw::RenderOptions o_;
  airdaw::RenderResult r_;
  bool reveal_ = true;
  std::function<void(const juce::String&, bool)> report_;
};

static juce::String mmssText(double s) { return juce::String::formatted("%02d:%02d", (int)s / 60, (int)s % 60); }

void MainComponent::exportMix() {
  auto* e = bridge_.engine(); if (!e) return;
  if (e->transport().recording.load()) { note("Stop recording before exporting", true); return; }
  const double sr = e->sampleRate();
  const auto end = airdaw::songEnd(*e);
  if (end <= 0) { note("Nothing to export yet: no track has a take playing", true); return; }
  const auto li = e->transport().loopIn.load(), lo = e->transport().loopOut.load();
  bool solo = false, muted = false;
  for (int t = 0; t < airdaw::kMaxTracks; ++t) { solo |= e->track(t).solo.load(); muted |= e->track(t).mute.load() && e->track(t).take.load() != nullptr; }
  juce::String msg = "Render the mix of all playing takes to a stereo WAV file. The export is what you hear: faders, inserts, mutes and solos apply.";
  if (solo) msg << "\n\nNOTE: a track is soloed, so only soloed tracks will be in the export.";
  else if (muted) msg << "\n\nNote: some tracks with takes are muted and will be silent in the export.";
  auto* w = new juce::AlertWindow("Export mix", msg, juce::MessageBoxIconType::NoIcon);
  juce::StringArray ranges{"Whole song (" + mmssText((double)end / sr) + " + 2 s tail)"};
  if (lo > li) ranges.add("Loop region (" + mmssText((double)li / sr) + juce::String(juce::CharPointer_UTF8("\xe2\x80\x93")) + mmssText((double)lo / sr) + ")");
  w->addComboBox("range", ranges, "Range");
  w->addComboBox("format", {"WAV 24-bit (for mastering and sharing)", "WAV 32-bit float (lossless, larger)"}, "Format");
  w->addButton("Export...", 1, juce::KeyPress(juce::KeyPress::returnKey));
  w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
    if (r != 1) return;
    const bool loop = w->getComboBoxComponent("range")->getSelectedItemIndex() == 1;
    const int bits = w->getComboBoxComponent("format")->getSelectedItemIndex() == 1 ? 32 : 24;
    auto dir = (sessionFile_ == juce::File() ? projectsRoot().getChildFile("Exports") : sessionFolder().getChildFile("exports"));
    dir.createDirectory();
    chooser_ = std::make_shared<juce::FileChooser>("Export mix as", dir.getChildFile(sessionName() + (loop ? " loop" : " mix") + ".wav"), "*.wav");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, loop, bits](const juce::FileChooser& c) { auto f = c.getResult(); if (f != juce::File()) startExport(f.withFileExtension("wav"), loop, bits); });
  }), true);
}

void MainComponent::startExport(const juce::File& out, bool loopRange, int bits) {
  auto* e = bridge_.engine(); if (!e) return;
  // Copy the current state (unsaved changes too) into a fresh engine.
  auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("airdaw_export.airdaw");
  if (!airdaw::saveSession(*e, tmp.getFullPathName().toStdString()).ok) { note("EXPORT FAILED: cannot snapshot the session", true); return; }
  auto copy = std::make_unique<airdaw::Engine>(e->sampleRate());
  copy->setInsertFactory(pluginFactory);
  auto lr = airdaw::loadSession(*copy, tmp.getFullPathName().toStdString());
  tmp.deleteFile();
  if (!lr.ok) { note("EXPORT FAILED: " + juce::String(lr.error), true); return; }
  copy->setWorkerThreads(-1);
  airdaw::RenderOptions o;
  o.bits = bits;
  if (loopRange) { o.start = e->transport().loopIn.load(); o.end = e->transport().loopOut.load(); o.tailSeconds = 0.0; }
  else { o.start = 0; o.end = airdaw::songEnd(*copy); o.tailSeconds = 2.0; }
  o.sampleRate = e->sampleRate();
  juce::Component::SafePointer<MainComponent> sp(this);
  auto* job = new ExportJob(std::move(copy), out, o, !snapshotMode_, [sp](const juce::String& msg, bool warn) { if (sp) sp->note(msg, warn); });
  job->launchThread();
}

// ---------------------------------------------------------------- plugins
void MainComponent::chooseMeterPlugin() {
  // The Airwindows Hit Record Meter is a separate VST3 (not part of Consolidated); the user picks the file once.
  auto start = juce::File(props_->getValue("meterPath", juce::File("C:\\Program Files\\Common Files\\VST3").getFullPathName()));
  chooser_ = std::make_shared<juce::FileChooser>("Select the Hit Record Meter .vst3", start.isDirectory() ? start : start.getParentDirectory(), "*.vst3");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                        juce::FileBrowserComponent::canSelectDirectories,  // some platforms ship VST3 as a bundle folder
      [this](const juce::FileChooser& c) { auto f = c.getResult(); if (f != juce::File()) loadMeter(f); });
}

void MainComponent::loadMeter(const juce::File& vst3, bool quiet) {
  auto* e = bridge_.engine(); if (!e) return;
  juce::String err;
  auto ins = Vst3Insert::load(vst3, e->sampleRate(), err);
  if (!ins) { if (!quiet) note("Meter load failed: " + err, true); return; }
  if (!snapshotMode_) { props_->setValue("meterPath", vst3.getFullPathName()); props_->saveIfNeeded(); }
  if (!e->setInsertProcessor(ui::kMasterId, kMeterSlot, std::move(ins))) { note("Could not install the meter", true); return; }
  if (!quiet) note("Hit Record Meter loaded on master insert " + juce::String(kMeterSlot + 1));
  if (!dockShown_) { dockShown_ = true; resized(); }
}

void MainComponent::openPluginWindow(int ch, int slot) {
  auto* e = bridge_.engine(); if (!e) return;
  auto* v = dynamic_cast<Vst3Insert*>(e->insertAt(ch, slot));
  if (!v) return;
  pluginWindow_.reset();
  pluginWindow_ = std::make_unique<PluginWindow>(v->plugin(), [this] {
    juce::MessageManager::callAsync([this] { pluginWindow_.reset(); });  // never delete a window inside its own callback
  });
}

// ---------------------------------------------------------------- timer
void MainComponent::timerCallback() {
  ++tick_;
  // Pixel shift (OLED burn-in care, mockup): the whole window moves by up to 2 px every 45 s.
  if (T().pixelShift && !snapshotMode_ && tick_ % (45 * 30) == 0) {
    juce::Random rng;
    setTransform(juce::AffineTransform::translation((float)rng.nextInt({-2, 3}), (float)rng.nextInt({-2, 3})));
  }
  auto* e = bridge_.engine();
  if (e && e != autoEngine_) {  // first run, or the engine was rebuilt for a new sample rate
    autoEngine_ = e;
    pluginWindow_.reset();
    dock_.setPlugin(nullptr);   // the hosted plugins belong to the old engine
    inserts_.close();
    bridge_.releaseRetired();   // nothing on screen refers to the replaced engine any more
    autoSaver_ = std::make_unique<airdaw::AutoSaver>(*e);
    hub_ = std::make_unique<ui::AnalysisHub>(e->sampleRate());  // filters depend on the rate
    mixLoud_ = std::make_unique<airdaw::LoudnessMeter>(e->sampleRate());
    refLoud_ = std::make_unique<airdaw::LoudnessMeter>(e->sampleRate());
    e->monitor().refSync = props_->getBoolValue("reference.sync", false);
    tapBuf_.assign(2 * 16384, 0.f);
    e->setMasterTapEnabled(true);
    // The reference is decoded at the engine's rate, so a new engine (new rate) decodes it again.
    if (!snapshotMode_) restoreProjectReference(false);
    setSessionFile(sessionFile_);
    applyAutoSaveSettings();
    if (pendingOpen_ != juce::File()) { const auto f = pendingOpen_; pendingOpen_ = juce::File(); openSessionFile(f); }
    if (!snapshotMode_ && !recoveryChecked_) { recoveryChecked_ = true; checkRecovery(); }
    if (!snapshotMode_ && !pluginsChecked_) {  // once per launch: library problems, scheduled update check
      pluginsChecked_ = true;
      if (pluginsPanel_.loadError.isNotEmpty()) note("Airwindows: using the built-in build (" + pluginsPanel_.loadError + ")", true);
      juce::Component::SafePointer<MainComponent> sp(this);
      juce::Timer::callAfterDelay(4000, [sp] { if (sp) sp->pluginsPanel_.startupCheck(); });
    }
    // Bring the Hit Record Meter back if it was set up before and the session has none.
    const juce::File mp(props_->getValue("meterPath"));
    if (!e->insertAt(ui::kMasterId, kMeterSlot) && mp.exists()) loadMeter(mp, true);
  }
  updateDeviceStatus();
  if (e) pumpAnalysis(*e);
  deck_.sync();
  overview_.sync();
  mixer_.sync();
  if (tick_ % 2 == 0) inserts_.sync();
  if (takes_.wanted() != takes_.isVisible()) resized();  // a loop region appeared/disappeared, or T was toggled
  if (tick_ % 3 == 0) { takes_.sync(); dock_.syncMonitor(); }
  if (e && tick_ % 3 == 0) {
    collectTakes();
    e->collectGarbage();
    auto* v = dynamic_cast<Vst3Insert*>(e->insertAt(ui::kMasterId, kMeterSlot));
    dock_.setPlugin(v ? v->plugin() : nullptr);
    e->monitor().listenSlot = v ? kMeterSlot : -1;  // the Hit Record Meter measures what is heard (the reference on B)
    if (autoSaver_) {
      auto saved = autoSaver_->tick(juce::Time::getMillisecondCounterHiRes() / 1000.0);
      if (!saved.empty()) {
        const auto when = juce::Time::getCurrentTime().toString(false, true);
        note("Auto-saved " + when);
        lastAutoSave_ = "Last auto-save " + when + "  ->  " + juce::File(juce::String(saved)).getParentDirectory().getFullPathName();
      }
    }
  }
  if (tick_ % 15 == 0 && juce::Time::getMillisecondCounter() > noticeUntil_ && notice_.isNotEmpty()) {
    notice_.clear(); repaint(0, getHeight() - T().railH() - kFooterH, getWidth(), kFooterH);
  }
}

// Two analysis streams: the master strip always measures the mix (master tap); the listening tools measure what
// is heard (heard tap: the mix, or the reference while A/B is on B). Integrated loudness / LRA accumulate while
// that source plays, and start over when the song plays from its beginning or A/B is switched.
void MainComponent::pumpAnalysis(airdaw::Engine& e) {
  if (!hub_ || !mixLoud_ || snapshotFrozen_) return;
  auto& tr = e.transport();
  auto& mon = e.monitor();
  const bool playing = tr.playing.load() && !tr.countingIn();
  const bool onB = mon.listenB.load() && e.reference() != nullptr;
  if (playing && !wasPlaying_ && (double)tr.pos.load() < e.sampleRate() * 0.05) resetMeters();
  if (onB != wasOnB_) hub_->resetProgramme();  // the tools start over on the other source
  wasPlaying_ = playing; wasOnB_ = onB;
  for (size_t got; (got = e.readMasterTap(tapBuf_.data(), tapBuf_.size() / 2)) > 0;) mixLoud_->process(tapBuf_.data(), got, playing);
  const bool heardPlaying = onB ? mon.refPlaying.load() : playing;
  for (size_t got; (got = e.readHeardTap(tapBuf_.data(), tapBuf_.size() / 2)) > 0;) hub_->feed(tapBuf_.data(), got, heardPlaying);
  hub_->playheadSeconds = (double)tr.pos.load() / e.sampleRate();
  // The reference as it plays (A or B), for the side-by-side levels. A fresh meter each time it starts.
  const auto* ref = e.reference();
  const bool refPlaying = ref && mon.refPlaying.load();
  if (refPlaying && !wasRefPlaying_) refLoud_ = std::make_unique<airdaw::LoudnessMeter>(e.sampleRate());
  wasRefPlaying_ = refPlaying;
  for (size_t got; (got = e.readRefTap(tapBuf_.data(), tapBuf_.size() / 2)) > 0;) refLoud_->process(tapBuf_.data(), got, true);
  if (tick_ % 2 == 0) {
    const auto mix = mixLoud_->read();
    mixer_.master().setLoudness(mix);
    mixIntegrated_ = mix.integrated;
    if (!snapshotLevels_) {
      ctx_.mixLevels = {mix.momentary, mix.shortTerm, mix.integrated, mix.truePeakNow};
      const auto rl = refLoud_->read();
      ctx_.refLevels = {rl.momentary, rl.shortTerm, ref ? ref->lufs : -200.f, rl.truePeakNow};
      ctx_.refLive = refPlaying;
    }
    hub_->refresh(); tools_.updateAll(*hub_);
  }
  // Reference level match: the reference's integrated loudness (measured when loaded) to the mix's.
  if (tick_ % 15 == 0)
    if (ref) {
      float g = 0.f;
      if (props_->getBoolValue("reference.match", true) && mixIntegrated_ > -70.f && ref->lufs > -70.f)
        g = juce::jlimit(-30.f, 30.f, mixIntegrated_ - ref->lufs);
      mon.refGainDb = g;
    }
}

void MainComponent::resetMeters() {
  if (hub_) hub_->resetProgramme();
  if (mixLoud_) mixLoud_->resetIntegrated();
}

void MainComponent::chooseReference() {
  const juce::File start(props_->getValue("reference.dir", props_->getValue("reference.path", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getFullPathName())));
  chooser_ = std::make_shared<juce::FileChooser>("Choose a reference track", start.isDirectory() ? start : start.getParentDirectory(),
                                                 "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
    const auto f = c.getResult(); if (f == juce::File()) return;
    if (!snapshotMode_) { props_->setValue("reference.dir", f.getParentDirectory().getFullPathName()); props_->saveIfNeeded(); }
    setProjectValue("reference", f.getFullPathName());  // each project remembers its own reference
    decodeReference(f, false);
  });
}

// Decodes a reference (WAV/AIFF/FLAC/MP3/OGG) on a background thread, resamples it to the engine's rate, measures
// its integrated loudness once (for level matching), then hands it to the engine on the message thread.
void MainComponent::decodeReference(const juce::File& f, bool quiet) {
  auto* e = bridge_.engine(); if (!e) return;
  const double sr = e->sampleRate();
  ctx_.referenceStatus = juce::String(juce::CharPointer_UTF8("decoding\xe2\x80\xa6"));
  juce::Component::SafePointer<MainComponent> sp(this);
  const int gen = ++refGen_;
  std::thread([sp, f, sr, quiet, gen] {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(f));
    auto ref = std::make_shared<std::unique_ptr<airdaw::Reference>>();
    juce::String err;
    if (!rd) err = "can't read " + f.getFileName();
    else if (rd->lengthInSamples > (juce::int64)(rd->sampleRate * 60 * 30)) err = f.getFileName() + " is longer than 30 minutes";
    else {
      const int n = (int)rd->lengthInSamples;
      juce::AudioBuffer<float> in(2, n + 64); in.clear();
      rd->read(&in, 0, n, 0, true, true);
      if (rd->numChannels == 1) in.copyFrom(1, 0, in, 0, 0, n);
      const double ratio = rd->sampleRate / sr;  // input samples per output sample
      const int outN = (int)std::floor((double)n / ratio);
      auto r = std::make_unique<airdaw::Reference>();
      r->name = f.getFileNameWithoutExtension().toStdString();
      r->l.resize((size_t)outN); r->r.resize((size_t)outN);
      if (std::abs(ratio - 1.0) < 1e-9) { std::copy(in.getReadPointer(0), in.getReadPointer(0) + outN, r->l.begin()); std::copy(in.getReadPointer(1), in.getReadPointer(1) + outN, r->r.begin()); }
      else {
        juce::WindowedSincInterpolator il, ir;
        il.process(ratio, in.getReadPointer(0), r->l.data(), outN);
        ir.process(ratio, in.getReadPointer(1), r->r.data(), outN);
      }
      airdaw::LoudnessMeter m(sr);  // integrated loudness of the whole reference
      std::vector<float> chunk(2 * 8192);
      for (int i = 0; i < outN; i += 8192) {
        const int k = std::min(8192, outN - i);
        for (int j = 0; j < k; ++j) { chunk[(size_t)(2 * j)] = r->l[(size_t)(i + j)]; chunk[(size_t)(2 * j + 1)] = r->r[(size_t)(i + j)]; }
        m.process(chunk.data(), (size_t)k, true);
      }
      r->lufs = m.read().integrated;
      *ref = std::move(r);
    }
    juce::MessageManager::callAsync([sp, ref, err, sr, quiet, gen] {
      if (!sp || sp->refGen_ != gen) return;  // a newer reference (or none) was chosen meanwhile
      auto* en = sp->bridge_.engine();
      if (!err.isEmpty() || !*ref) { sp->ctx_.referenceStatus = err; sp->note("REFERENCE FAILED: " + err, true); return; }
      if (!en || en->sampleRate() != sr) return;  // the rate changed meanwhile: the new engine decodes it again
      const auto name = juce::String((*ref)->name); const float lufs = (*ref)->lufs;
      en->setReference(std::move(*ref));
      sp->ctx_.referenceStatus.clear();
      if (!quiet) sp->note("Reference loaded: " + name + (lufs > -70.f ? ", " + juce::String(lufs, 1) + " LUFS" : juce::String()));
    });
  }).detach();
}

juce::var MainComponent::projectData() const {
  auto* e = const_cast<AudioBridge&>(bridge_).engine();
  auto v = e ? juce::JSON::parse(juce::String::fromUTF8(e->appData.c_str())) : juce::var();
  return v.getDynamicObject() ? v : juce::var(new juce::DynamicObject());
}

void MainComponent::setProjectValue(const juce::String& key, const juce::var& value) {
  auto* e = bridge_.engine(); if (!e) return;
  auto d = projectData();
  d.getDynamicObject()->setProperty(key, value);
  e->appData = juce::JSON::toString(d, true).toStdString();
}

// Removing a track: asks first, then clears the slot back to a fresh track (takes leave the project, inserts are
// unloaded, settings reset) and hides its strip. Recorded files stay in the project folder. No engine change:
// the engine keeps its fixed track slots; "Add track" brings a removed slot back.
void MainComponent::removeTrack(int t) {
  auto* e = bridge_.engine(); if (!e || t < 0 || t >= airdaw::kMaxTracks) return;
  if (e->transport().recording.load() && e->track(t).armed.load()) { note("Stop recording before removing an armed track"); return; }
  const juce::String name(e->track(t).name);
  const int takes = e->numTakes(t);
  juce::String msg = "Remove " + name + "?";
  if (takes > 0) msg << "\n\nIts " << takes << (takes == 1 ? " take is" : " takes are") << " taken out of the project. The recorded files stay in the project folder.";
  msg << "\n\nInserts and settings on this track are cleared.";
  juce::AlertWindow::showAsync(juce::MessageBoxOptions().withTitle("Remove track").withMessage(msg).withButton("Remove", 1).withButton("Cancel", 0),
                               [t, sp = juce::Component::SafePointer<MainComponent>(this)](int r) {
    if (sp && r == 1) sp->clearAndHideTrack(t);
  });
}

void MainComponent::clearAndHideTrack(int t) {
  auto* e = bridge_.engine(); if (!e) return;
  if (e->transport().recording.load() && e->track(t).armed.load()) { note("Stop recording before removing an armed track"); return; }
  const juce::String name(e->track(t).name);
  if (inserts_.isVisible() && inserts_.channel() == t) inserts_.close();
  if (ctx_.viewTrack == t) ctx_.viewTrack = -1;
  auto& c = e->track(t);
  c.armed = false; c.audition = false; c.monitor = true; c.mute = false; c.solo = false; c.polarity = false;
  c.inputL = -1; c.inputR = -1; c.stereo = false; c.route = (int)airdaw::Route::Master;
  c.gainDb = 0.f; c.trimDb = 0.f; c.pan = 0.f;
  for (int s = 0; s < airdaw::kMaxInserts; ++s) e->setInsert(t, s, "");
  e->clearComp(t);
  e->clearTakeList(t);
  c.name = "TRACK " + std::to_string(t + 1);
  mixer_.removeTrack(t);
  note("Removed " + name);
  resized();
}

void MainComponent::restoreProjectReference(bool adoptGlobal) {
  auto* e = bridge_.engine(); if (!e) return;
  mixer_.setRemovedTracks(projectData()["removedTracks"]);  // each project remembers which tracks were removed
  const bool legacy = e->appData.empty();
  juce::String path = projectData()["reference"].toString();
  if (path.isEmpty() && legacy && adoptGlobal) {
    path = props_->getValue("reference.path");
    if (path.isNotEmpty()) setProjectValue("reference", path);
  }
  const juce::File f(path);
  if (path.isNotEmpty() && f.existsAsFile()) { decodeReference(f, true); return; }
  ++refGen_;  // drop any decode still running
  e->setReference(nullptr);
  auto& m = e->monitor(); m.listenB = false; m.refPlaying = false;
  ctx_.referenceStatus = path.isNotEmpty() ? "missing: " + f.getFileName() : juce::String();
}

// ---------------------------------------------------------------- snapshots (test hook)
void MainComponent::runScenario(const juce::String& nameIn) {
  auto* e = bridge_.engine(); if (!e) return;
  // test hook: "<scenario>@key=value,key=value" sets Display options first (e.g. "inserts@btn=toggle,font=industrial")
  const juce::String name = nameIn.upToFirstOccurrenceOf("@", false, false);
  if (nameIn.containsChar('@')) {
    for (auto& kv : juce::StringArray::fromTokens(nameIn.fromFirstOccurrenceOf("@", false, false), ",", ""))
      T().setOption(kv.upToFirstOccurrenceOf("=", false, false), kv.fromFirstOccurrenceOf("=", false, false));
    applyTheme();
  }
  if (name == "asis") return;  // test hook: picture the window exactly as it opened (e.g. a project from the command line)
  if (name == "open") { openSessionFile(juce::File(juce::SystemStats::getEnvironmentVariable("AIRDAW_OPEN", {}))); return; }  // test hook
  const char* names[] = {"Kick", "Snare", "OH L", "OH R", "Bass DI", "Gtr L", "Gtr R", "Keys"};
  for (int t = 0; t < 8; ++t) e->track(t).name = names[t];
  for (int t : {2, 3, 5, 6}) e->track(t).stereo = true;
  e->setInsert(0, 0, "Console7Channel"); e->setInsert(0, 1, "Pressure5");
  e->setInsert(4, 0, "Console7Channel"); e->setInsert(4, 1, "Density3"); e->setInsert(4, 2, "ToTape8");
  e->setInsert(100, 0, "Console7Buss"); e->setInsert(100, 1, "Galactic3");
  e->setInsert(ui::kMasterId, 0, "Console7Buss"); e->setInsert(ui::kMasterId, 1, "ADClip8");
  e->track(0).route = 1; e->track(1).route = 2; e->track(0).trimDb = 6.f; e->track(3).pan = -0.4f; e->track(2).gainDb = -4.5f;
  e->track(1).mute = true; e->track(2).solo = true; e->track(0).armed = true; e->track(0).inputL = 0;
  if (name == "inserts") { inserts_.show(0, 0); inserts_.show(0, 1); }
  if (name == "picker") {  // test hook: the plugin picker's list (opened like a click on it)
    inserts_.show(0, 0);
    juce::Component::SafePointer<MainComponent> sp(this);
    juce::Timer::callAfterDelay(400, [sp] {
      if (!sp) return;
      std::function<juce::ComboBox*(juce::Component*)> find = [&](juce::Component* c) -> juce::ComboBox* {
        if (auto* b = dynamic_cast<juce::ComboBox*>(c)) if (b->getTextWhenNothingSelected().startsWith("Choose Airwindows")) return b;
        for (auto* k : c->getChildren()) if (auto* f = find(k)) return f;
        return nullptr;
      };
      if (auto* box = find(&sp->inserts_)) {
        juce::String out;  // the list's structure, for checking (a menu window cannot be pictured)
        for (juce::PopupMenu::MenuItemIterator it(*box->getRootMenu()); it.next();) {
          auto& item = it.getItem();
          out << (item.isSectionHeader ? "# " : item.subMenu ? "> " : "  ") << item.text
              << (item.subMenu ? " [" + juce::String(item.subMenu->getNumItems()) + " items]" : juce::String()) << "\n";
        }
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("airdaw-picker.txt").replaceWithText(out);
        box->showPopup();
      }
    });
  }
  if (name == "inserts-tape") { inserts_.show(4, 2); }
  if (name == "takes") {  // three loop passes on Kick, the second one starred, the third one playing
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("airdaw_snap_takes");
    dir.deleteRecursively(); dir.createDirectory();
    const double sr = e->sampleRate(); const auto li = (juce::int64)(sr * 43), lo = (juce::int64)(sr * 51);
    for (int pass = 0; pass < 3; ++pass) {
      auto f = dir.getChildFile("Kick_take00" + juce::String(pass + 1) + ".wav").getFullPathName().toStdString();
      airdaw::WavWriter w; w.open(f, 1, (int)sr);
      std::vector<float> buf((size_t)(lo - li));
      for (size_t i = 0; i < buf.size(); ++i) {
        const double tt = (double)i / sr, env = std::exp(-std::fmod(tt, 0.5) * (6.0 + pass * 3.0));
        buf[i] = (float)(0.6 * env * std::sin(2.0 * 3.14159265 * (60.0 + pass * 15.0) * tt));
      }
      w.write(buf.data(), buf.size()); w.close();
      e->setActiveTake(0, e->addTake(0, f, li));
    }
    e->setTakeKeep(0, 1, true);
    e->transport().loopIn = li; e->transport().loopOut = lo; e->transport().loopOn = true; e->transport().seek(li + (juce::int64)(sr * 3));
  }
  if (name == "export") {  // end-to-end export of the "takes" scenario (no dialogs)
    runScenario("takes");
    e->track(2).solo = false;  // the base scenario solos OH L, which would (correctly) silence the Kick takes
    startExport(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("airdaw_snap_export.wav"), false, 24);
  }
  if (name == "favs") {  // ToTape8 as a favourite (in-memory only: snapshot mode never saves settings)
    props_->setValue("favoritePlugins", "Console7Channel;ToTape8");
    inserts_.show(4, 2);
  }
  if (name == "info") ctx_.showPluginInfo("ToTape8");
  if (name == "signal" || name == "listen-signal") {  // synthetic stereo mix fed straight into the meters (never to the speakers)
    if (name == "listen-signal") { ctx_.notes.items = {{2.0, "vocal too loud in chorus"}, {5.5, "check bass at the drop"}}; setListen(true); }
    e->track(1).polarity = true;  // a lit polarity button on track 2
    if (!hub_) hub_ = std::make_unique<ui::AnalysisHub>(e->sampleRate());
    const double sr = e->sampleRate(); std::vector<float> buf(2 * 4096); juce::Random rng(5); float lp = 0.f;
    for (int blk = 0; blk < (int)(8.0 * sr / 4096); ++blk) {
      for (int i = 0; i < 4096; ++i) {
        const double t = (blk * 4096 + i) / sr, kick = std::exp(-std::fmod(t, 0.5) * 18.0) * std::sin(2 * 3.14159265 * 55 * t);
        lp += 0.02f * ((rng.nextFloat() * 2.f - 1.f) - lp);
        const double mid = 0.45 * kick + 0.12 * std::sin(2 * 3.14159265 * 440 * t) + 0.05 * std::sin(2 * 3.14159265 * 2200 * t) + 0.3 * lp;
        const double side = 0.06 * (rng.nextFloat() * 2.f - 1.f) + 0.04 * std::sin(2 * 3.14159265 * 6500 * t);
        buf[(size_t)(2 * i)] = (float)(mid + side); buf[(size_t)(2 * i + 1)] = (float)(mid - side);
      }
      hub_->feed(buf.data(), 4096, true);
      if (blk % 4 == 0) { hub_->refresh(); tools_.updateAll(*hub_); }  // let scrolling tools (spectrogram) move
    }
    hub_->refresh(); mixer_.master().setLoudness(hub_->loud()); tools_.updateAll(*hub_);
    snapshotFrozen_ = true;  // keep these readings (the live tap would replace them with silence)
  }
  if (name == "reorder") { inserts_.show(4, 2); inserts_.moveInsert(4, 2, 0); }  // ToTape8 to the front, card stays open
  if (name == "add") { inserts_.show(0, 3); }
  if (name == "display") toggleDisplayPanel();
  if (name == "plugins") togglePluginsPanel();
  if (name == "plugins-check") { togglePluginsPanel(); pluginsPanel_.check(); }  // test hook: a real check against GitHub
  if (name == "settings") showAudioSettings();
  if (name == "plugins-install") {  // test hook: a local feed with a different build, check + install (no network)
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("airdaw-test-feed");
    const auto dll = juce::File(juce::SystemStats::getEnvironmentVariable("AIRDAW_TEST_FXLIB", {}));
    dir.deleteRecursively(); dir.createDirectory();
    const auto sha = juce::SHA256(dll).toHexString();
    juce::String names; for (auto& n : {"ToTape8", "DemoVerb1", "DemoClip2"}) names << (names.isEmpty() ? "" : ", ") << "\"" << n << "\"";
    const auto manifest = "{\"format\": \"airdaw-fxlib\", \"api\": 1, \"build\": \"local-test\", \"sha256\": \"" + sha + "\", \"effects\": [" + names + "]}";
    dir.getChildFile("manifest.json").replaceWithText(manifest);
    { juce::ZipFile::Builder zb; zb.addFile(dll, 9, "AirwindowsFX.dll"); zb.addFile(dir.getChildFile("manifest.json"), 9, "manifest.json");
      juce::FileOutputStream os(dir.getChildFile("AirwindowsFX-win64.zip")); zb.writeToStream(os, nullptr); }
    const auto url = [&](const char* f) { return juce::URL(dir.getChildFile(f)).toString(false); };
    dir.getChildFile("releases.json").replaceWithText("[{\"tag_name\": \"airwindows-local-test\", \"draft\": false, \"assets\": ["
        "{\"name\": \"manifest.json\", \"browser_download_url\": \"" + url("manifest.json") + "\"}, "
        "{\"name\": \"AirwindowsFX-win64.zip\", \"browser_download_url\": \"" + url("AirwindowsFX-win64.zip") + "\"}]}]");
    props_->setValue("plugins.feed", url("releases.json"));
    togglePluginsPanel();
    pluginsPanel_.check();
    juce::Component::SafePointer<MainComponent> sp(this);
    juce::Timer::callAfterDelay(2500, [sp] { if (sp) sp->pluginsPanel_.installForTest(); });
  }
  if (name == "display-all") { display_.setAllOpen(true); toggleDisplayPanel(); }
  if (name == "inserts-rack") { T().inserts = "rack"; inserts_.show(0, 0); inserts_.show(0, 1); }  // test hook: rack look
  if (name == "remove-track") clearAndHideTrack(1);  // test hook: Snare removed (what "Remove" in the dialog does)
  if (name.startsWith("look-")) {  // test hook: a preset (eras / classics), panel open on the Look section
    T().applyPreset(name.fromFirstOccurrenceOf("look-", false, false)); T().changed();
    toggleDisplayPanel();
  }
  if (name == "blue") { T().hue = 215.f; applyTheme(); }
  if (name == "glow-off") { T().glow = 0.f; applyTheme(); }
  if (name == "ambient-play") deck_.previewAmbient(1);  // test hook: the transport's light as if playing / recording (nothing plays)
  if (name == "ambient-rec") deck_.previewAmbient(2);
  if (name == "keys-lit") e->transport().loopOn = true;  // test hook: an engaged transport key with keys on both sides
  if (name == "nodock") { dockShown_ = false; }
  if (name == "arrange") {  // the same calls the drag-and-drop makes: history into the suite, RTA out, Target minimised
    setListen(true);
    tools_.addToSuite("history", 1);
    tools_.removeFromSuite("rta", 0);
    tools_.toggleMinimised("target");
  }
  if (name == "many-inserts") {  // test hook: a chain longer than the old 8 slots
    const char* fx[] = {"Console7Channel", "Pressure5", "Density3", "ToTape8", "Galactic3", "ADClip8", "Density2", "Pressure5",
                        "ToTape8", "Console7Buss", "Density3", "ADClip8"};
    for (int s = 0; s < 12; ++s) e->setInsert(0, s, fx[s]);
    T().inserts = "rack"; inserts_.show(0, -1);
  }
  if (name == "widetools") { for (auto& id : tools_.suite()) tools_.setWidth(id, 1500); dock_.rebuildSuite(); }  // every tool dragged too wide
  if (name == "widemeter") dock_.setMeterWidth(1300);  // meter dragged most of the way across
  if (name == "fullmeter") dock_.setMeterWidth(5000);  // all the way
  if (name == "foldmeter") dock_.setMeterWidth(1580);  // two tools folded into tabs, one still open
  if (name == "reference") {  // decode a 44.1 kHz reference into the 96 kHz engine; nothing is played aloud
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("airdaw_ref_test.wav");
    { airdaw::WavWriter w; w.open(f.getFullPathName().toStdString(), 2, 44100);
      std::vector<float> s(2 * 441000); const float a = std::pow(10.f, -18.f / 20.f);
      for (size_t i = 0; i < 441000; ++i) s[2 * i] = s[2 * i + 1] = a * (float)std::sin(2.0 * 3.14159265358979 * 1000.0 * (double)i / 44100.0);
      w.write(s.data(), 441000); w.close(); }
    decodeReference(f, false);
    e->monitor().mono = true; e->monitor().solo = airdaw::Monitor::Side;  // show lit monitor buttons
    e->monitor().refSync = true; e->track(0).polarity = true; e->bus(1).polarity = true;  // lit Sync and polarity buttons
    snapshotLevels_ = true;  // synthetic side-by-side levels: mix quieter than the reference
    ctx_.mixLevels = {-15.2f, -14.1f, -14.6f, -1.3f}; ctx_.refLevels = {-8.9f, -9.4f, -9.6f, 0.2f}; ctx_.refLive = true;
    tools_.addToSuite("reference", 0);
    setListen(true);
  }
  if (name == "comp") {  // test hook: a full take on Kick, 3 loop passes over a chorus, pass 2 comped into it
    const double sr = e->sampleRate();
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("airdaw-comp-test");
    dir.deleteRecursively(); dir.createDirectory();
    auto wav = [&](const juce::String& n, float amp, double sec, double hz) {
      const auto f = dir.getChildFile(n);
      airdaw::WavWriter w; w.open(f.getFullPathName().toStdString(), 1, (int)sr);
      std::vector<float> x((size_t)(sec * sr));
      for (size_t i = 0; i < x.size(); ++i) x[i] = amp * (float)std::sin(2.0 * 3.14159265 * hz * (double)i / sr) * (0.6f + 0.4f * (float)std::sin((double)i / sr * 3.0));
      w.write(x.data(), x.size()); w.close();
      return f.getFullPathName().toStdString();
    };
    const auto li = (juce::int64)(12.0 * sr), lo = (juce::int64)(20.0 * sr);
    const int base = e->addTake(0, wav("full.wav", 0.3f, 30.0, 110.0), 0);
    e->setActiveTake(0, base);
    int pass2 = -1;
    for (int k = 0; k < 3; ++k) { const int i = e->addTake(0, wav("pass" + juce::String(k + 1) + ".wav", 0.5f + 0.2f * k, 8.0, 110.0), li); if (k == 1) pass2 = i; }
    auto& tr = e->transport(); tr.loopIn = li; tr.loopOut = lo; tr.loopOn = true;
    e->setCompSection(0, li, lo, e->takeAt(0, pass2));
    e->compCrossfadeMs = 10.f;
    ctx_.viewTrack = 0;
  }
  if (name == "midilearn" || name == "midimap") {  // simulated controllers (no MIDI device is opened in snapshot runs)
    auto cc = [this](const char* dev, int n, int v) { midi_->inject(dev, juce::MidiMessage::controllerEvent(1, n, v)); };
    midi_->setLearning(true);
    midi_->arm("ch.0.fader"); cc("KeyLab mkII", 21, 40);                        // fixed knob, fader sits at 0 dB
    midi_->setLearning(false);
    for (int v : {60, 80, 90, 100, 104}) cc("KeyLab mkII", 21, v);           // picks up when it passes 0 dB, then follows
    midi_->setLearning(true);
    midi_->arm("ch.1.pan"); for (int i = 0; i < 12; ++i) cc("nanoKONTROL2", 16, 65);  // endless knob: detected as relative
    // Arturia-style endless knobs: accelerating steps without exact repeats (centre 64), and centre-16 steps.
    midi_->arm("ch.2.trim"); for (int v : {65, 66, 67, 66, 68, 67, 69, 66, 65, 66, 67, 68}) cc("Arturia MiniLab 3", 74, v);
    midi_->arm("ch.3.fader"); for (int v : {15, 14, 15, 15, 13, 14, 15, 14, 15, 15}) cc("Arturia MiniLab 3", 71, v);
    // Arturia MiniLab: a step then the centre for every click (65, 64, 65, 64 ...), as seen in a user's log.
    midi_->arm("ch.4.trim"); for (int i = 0; i < 9; ++i) { cc("Arturia MINILAB", 7, 65); cc("Arturia MINILAB", 7, 64); }
    // A mapping saved earlier as a fixed knob (learned from a sweep) corrects itself on that pattern.
    midi_->arm("ch.5.trim"); for (int v : {40, 41, 42, 43}) cc("Arturia MINILAB", 76, v);
    midi_->setLearning(false);
    for (int i = 0; i < 9; ++i) { cc("Arturia MINILAB", 76, 65); cc("Arturia MINILAB", 76, 64); }
    midi_->setLearning(true);
    midi_->arm("transport.play"); midi_->inject("Samson Graphite", juce::MidiMessage::noteOn(10, 36, (juce::uint8)100));
    midi_->arm("mon.mono"); cc("nanoKONTROL2", 41, 127);
    midi_->arm("fx.0.0.0"); cc("KeyLab mkII", 22, 64);
    midi_->arm("ch.2.mute");  // left armed: "Move a controller..."
    if (name == "midimap") showMidiMappings();
  }
  if (name == "listen") {
    ctx_.notes.items = {{12.0, "vocal too loud in chorus"}, {31.5, "check bass at the drop"}};  // in memory only
    setListen(true);
  }
  resized();
}

void MainComponent::takeSnapshot(const juce::File& png, const juce::String& scenario) {
  juce::Timer::callAfterDelay(1500, [this, png, scenario] {
    runScenario(scenario);
    juce::Timer::callAfterDelay(scenario == "export" || scenario == "plugins-install" || scenario == "plugins-check" ? 12000 : 1500, [this, png, scenario] {  // an export needs time to finish
      juce::Component* top = getTopLevelComponent();
      if (pluginInfo_ && pluginInfo_->isVisible()) top = pluginInfo_.get();  // "info" scenario: picture the About window
      if (midiWindow_ && midiWindow_->isVisible()) top = midiWindow_.get();
      if (settings_ && settings_->isVisible()) top = settings_.get();  // "settings" scenario: the Audio window
      if (scenario == "picker")  // the newest desktop window: the open menu
        if (auto* m = juce::Desktop::getInstance().getComponent(juce::Desktop::getInstance().getNumComponents() - 1); m && m != top) top = m;  // "midimap" scenario: the mappings window
      auto img = top->createComponentSnapshot(top->getLocalBounds(), true, 1.f);
      png.deleteFile();
      juce::FileOutputStream out(png);
      juce::PNGImageFormat().writeImageToStream(img, out);
      out.flush();
      juce::JUCEApplication::getInstance()->systemRequestedQuit();
    });
  });
}
