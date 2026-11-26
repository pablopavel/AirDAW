#include "MainComponent.h"
#include "Vst3Insert.h"
#include "airdaw/render.h"
#include <map>

using ui::T;
static constexpr int kMeterSlot = 7;  // last master insert slot: the meter listens to the final mix
static constexpr int kTrimW = 30, kRailH = 14, kFooterH = 26, kOverviewH = 100, kMixerMin = 480;

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
  return std::make_unique<juce::PropertiesFile>(opt);
}

MainComponent::MainComponent(bool snapshotMode) : snapshotMode_(snapshotMode) {
  T().load(*props_);
  lnf_.refreshColours();
  juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
  juce::Desktop::getInstance().setGlobalScaleFactor(T().scalePct / 100.f);
  T().addChangeListener(this);
  tooltips_ = std::make_unique<juce::TooltipWindow>(this, 600);

  bridge_.setInsertFactory(pluginFactory);
  // Reopen the last audio device (e.g. ASIO) unless taking a test snapshot.
  std::unique_ptr<juce::XmlElement> saved = snapshotMode_ ? nullptr : props_->getXmlValue("audioDevice");
  juce::String err = devices_.initialise(2, 2, saved.get(), true);  // Black Lion Revolution 2x2 is 2-in/2-out
  if (err.isNotEmpty()) { juce::Logger::writeToLog("Audio init: " + err); initError_ = err; }
  devices_.addAudioCallback(&bridge_);
  devices_.addChangeListener(this);

  ctx_.note = [this](const juce::String& s) { note(s); };
  ctx_.requireAudio = [this](const char* w) { return requireAudio(w); };
  ctx_.showInserts = [this](int ch, int slot) {
    if (inserts_.isVisible() && inserts_.channel() == ch && slot < 0) { inserts_.close(); return; }
    inserts_.show(ch, slot);
  };
  ctx_.isCardOpen = [this](int ch, int slot) { return inserts_.isOpen(ch, slot); };
  ctx_.layoutChanged = [this] { resized(); };
  ctx_.openPluginWindow = [this](int ch, int slot) { openPluginWindow(ch, slot); };
  mixer_.master().onResetLoudness = [this] { if (loudness_) loudness_->resetIntegrated(); };
  ctx_.showPluginInfo = [this](const juce::String& name) {
    if (!pluginInfo_) pluginInfo_ = std::make_unique<ui::PluginInfoWindow>();
    pluginInfo_->show(name);
  };
  // Deferred: the move rebuilds the insert cards, one of which is still handling the drop.
  ctx_.moveInsert = [this](int ch, int from, int to) {
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
  splitter_.current = [this] { return dockHeight_; };
  splitter_.onDrag = [this](int h) { dockHeight_ = juce::jlimit(150, 900, h); props_->setValue("dockHeight", dockHeight_); resized(); };
  splitter_.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
  splitter_.setTooltip("Drag to resize the meter section");
  display_.setVisible(false);
  for (juce::Component* c : std::initializer_list<juce::Component*>{&deck_, &overview_, &inserts_, &mixer_, &splitter_, &dock_})
    addAndMakeVisible(c);
  addChildComponent(takes_);
  addChildComponent(display_);
  inserts_.setVisible(false);

  dockHeight_ = props_->getIntValue("dockHeight", 330);
  dockShown_ = props_->getBoolValue("dockShown", true);
  autoOn_ = props_->getBoolValue("autosaveOn", false);
  const int secs[] = {0, 60, 120, 300, 600, 900, 1800};
  const int savedSec = props_->getIntValue("autosaveSec", 300);
  for (int i = 1; i <= 6; ++i) if (secs[i] == savedSec) autoId_ = i;

  setWantsKeyboardFocus(true);
  addKeyListener(this);
  setSize(1600, 1000);
  startTimerHz(30);
}

MainComponent::~MainComponent() {
  stopTimer();
  T().removeChangeListener(this);
  devices_.removeChangeListener(this);
  pluginWindow_.reset();
  pluginInfo_.reset();
  settings_.reset();
  dock_.setPlugin(nullptr);
  devices_.removeAudioCallback(&bridge_);
  if (!snapshotMode_) props_->saveIfNeeded();
  juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

// ---------------------------------------------------------------- look
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* src) {
  if (src == &T()) { applyTheme(); return; }
  if (src == &devices_ && !snapshotMode_) {  // remember the device setup (e.g. ASIO, 96 kHz, 64 samples)
    if (auto xml = devices_.createStateXml()) { props_->setValue("audioDevice", xml.get()); props_->saveIfNeeded(); }
  }
}

void MainComponent::applyTheme() {
  lnf_.refreshColours();
  juce::Desktop::getInstance().setGlobalScaleFactor(T().scalePct / 100.f);
  if (!snapshotMode_) T().save(*props_);
  display_.syncFromTheme();
  sendLookAndFeelChange();
  if (auto* top = getTopLevelComponent()) top->repaint();
  if (settings_) settings_->repaint();
}

void MainComponent::toggleDisplayPanel() {
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
void MainComponent::Splitter::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat().withSizeKeepingCentre(90.f, 5.f);
  const bool over = isMouseOver();
  if (over) T().drawGlow(g, r, T().am(), 12.f, 0.8f);
  g.setGradientFill(juce::ColourGradient(over ? T().am() : juce::Colours::black, 0, r.getY(),
                                         over ? T().am() : juce::Colour(0xff3a3a3c), 0, r.getBottom(), false));
  g.fillRoundedRectangle(r, 3.f);
}

void MainComponent::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds();
  g.fillAll(juce::Colours::black);
  auto col = r.reduced(kTrimW, 0).withTrimmedBottom(kRailH);
  // warm wash from the top centre (mockup #col background)
  g.setGradientFill(juce::ColourGradient(t.am().withAlpha(t.glowAlpha(0.16f)), (float)col.getCentreX(), (float)col.getY() - col.getHeight() * 0.1f,
                                         juce::Colours::transparentBlack, (float)col.getCentreX(), (float)col.getY() + col.getHeight() * 0.55f, true));
  g.fillRect(col);
  // walnut side trim and bottom rail
  auto wood = [&](juce::Rectangle<int> w, bool vertical, bool leftSide) {
    const juce::Colour a(0xff2b170b), b(0xff4d2e19), c(0xff35200f);
    juce::ColourGradient wg(a, (float)w.getX(), (float)w.getY(), c, vertical ? (float)w.getRight() : (float)w.getX(),
                            vertical ? (float)w.getY() : (float)w.getBottom(), false);
    wg.addColour(0.45, b);
    g.setGradientFill(wg); g.fillRect(w);
    juce::Random rng(vertical ? (leftSide ? 7 : 11) : 13);
    for (int i = 0; i < (vertical ? w.getWidth() : w.getHeight()); i += 2 + rng.nextInt(4)) {
      g.setColour(juce::Colour(rng.nextBool() ? 0x1c000000 : 0x08ffffff));
      if (vertical) g.fillRect(w.getX() + i, w.getY(), 1, w.getHeight()); else g.fillRect(w.getX(), w.getY() + i, w.getWidth(), 1);
    }
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xaa000000), vertical ? (float)(leftSide ? w.getRight() : w.getX()) : 0.f,
                                           vertical ? 0.f : (float)w.getY(), juce::Colours::transparentBlack,
                                           vertical ? (float)(leftSide ? w.getRight() - 8 : w.getX() + 8) : 0.f,
                                           vertical ? 0.f : (float)w.getY() + 4.f, false));
    g.fillRect(w);
  };
  wood(r.withWidth(kTrimW), true, true);
  wood(r.withLeft(r.getRight() - kTrimW), true, false);
  wood(col.withY(col.getBottom()).withHeight(kRailH), false, false);
  // footer: device status (left, red when audio is not running) and the latest notice (right)
  auto foot = col.removeFromBottom(kFooterH);
  g.setColour(juce::Colours::black); g.fillRect(foot.removeFromTop(2));
  foot = foot.reduced(16, 0);
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
  auto r = getLocalBounds().reduced(kTrimW, 0).withTrimmedBottom(kRailH + kFooterH);
  deck_.setBounds(r.removeFromTop(deck_.preferredHeight(r.getWidth())));
  overview_.setBounds(r.removeFromTop(getHeight() >= 950 ? kOverviewH : 70));  // short windows: slimmer overview
  takes_.setVisible(takes_.wanted());
  if (takes_.isVisible()) takes_.setBounds(r.removeFromTop(ui::TakesBar::kHeight));
  if (display_.isVisible())
    display_.setBounds(r.getRight() - ui::DisplayPanel::kWidth - 16, deck_.getBottom() - 6, ui::DisplayPanel::kWidth, ui::DisplayPanel::kHeight);
  r = r.reduced(12, 0);
  r.removeFromTop(10);
  // Space is shared mixer-first: the meter dock gives way, then the insert panel scrolls.
  int avail = r.getHeight() - 10;
  int ins = 0;
  // Compact strips (slot list folded) still show a usable fader at ~400 px, so the panel may push the mixer down to that.
  if (inserts_.isVisible()) ins = juce::jmin(inserts_.preferredHeight(), juce::jmax(150, juce::jmin(avail * 2 / 5, avail - 400 - 10)));
  int dock = 0;
  if (dockShown_) dock = juce::jmin(dockHeight_, avail - ins - (ins ? 10 : 0) - kMixerMin - 14);
  if (dock < 150) dock = 0;
  if (inserts_.isVisible()) { inserts_.setBounds(r.removeFromTop(ins)); r.removeFromTop(10); }
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
  repaint(0, getHeight() - kRailH - kFooterH, getWidth(), kFooterH);
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
  juce::String text;
  if (dev == nullptr) {
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
  if (text != status_) { status_ = text; repaint(0, getHeight() - kRailH - kFooterH, getWidth(), kFooterH); }
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
        inserts_.close(); ctx_.viewTrack = -1; if (loudness_) loudness_->resetIntegrated();
        setSessionFile({}); note("New project");
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
  setSessionFile(file);
  juce::String msg = "Saved project " + name;
  if (moved) msg << ", moved " << moved << " take(s) into its audio folder";
  if (copied) msg << ", copied " << copied << " take(s) into its audio folder";
  if (failed) msg << ". WARNING: " << failed << " take file(s) missing or could not be copied";
  note(msg);
}

void MainComponent::openSession() {
  chooser_ = std::make_shared<juce::FileChooser>("Open project", sessionFile_ == juce::File() ? projectsRoot() : sessionFolder().getParentDirectory(), "*.airdaw");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
      [this](const juce::FileChooser& c) {
        auto f = c.getResult(); auto* e = bridge_.engine(); if (f == juce::File() || !e) return;
        auto r = airdaw::loadSession(*e, f.getFullPathName().toStdString());
        if (!r.ok) { note("OPEN FAILED: " + r.error); return; }
        inserts_.close(); ctx_.viewTrack = -1; if (loudness_) loudness_->resetIntegrated();
        setSessionFile(f);
        note(r.warnings.empty() ? "Opened " + f.getFileName() : "Opened with " + juce::String((int)r.warnings.size()) + " warning(s): " + r.warnings[0],
             !r.warnings.empty());
      });
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
        if (auto* e = bridge_.engine()) { auto res = airdaw::loadSession(*e, lf.getFullPathName().toStdString()); note(res.ok ? "Recovered auto-save" : "RECOVERY FAILED: " + res.error); }
      });
}

void MainComponent::collectTakes() {
  auto* e = bridge_.engine(); if (!e) return;
  for (auto& t : e->pollFinishedTakes()) {
    int idx = e->addTake(t.track, t.path, t.start);
    if (idx >= 0) e->setActiveTake(t.track, idx);
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
  auto* e = bridge_.engine();
  if (e && e != autoEngine_) {  // first run, or the engine was rebuilt for a new sample rate
    autoEngine_ = e;
    pluginWindow_.reset();
    dock_.setPlugin(nullptr);   // the hosted plugins belong to the old engine
    inserts_.close();
    autoSaver_ = std::make_unique<airdaw::AutoSaver>(*e);
    loudness_ = std::make_unique<airdaw::LoudnessMeter>(e->sampleRate());  // K-weighting depends on the rate
    tapBuf_.assign(2 * 16384, 0.f);
    e->setMasterTapEnabled(true);
    setSessionFile(sessionFile_);
    applyAutoSaveSettings();
    if (!snapshotMode_ && !recoveryChecked_) { recoveryChecked_ = true; checkRecovery(); }
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
  if (tick_ % 3 == 0) takes_.sync();
  if (e && tick_ % 3 == 0) {
    collectTakes();
    e->collectGarbage();
    auto* v = dynamic_cast<Vst3Insert*>(e->insertAt(ui::kMasterId, kMeterSlot));
    dock_.setPlugin(v ? v->plugin() : nullptr);
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
    notice_.clear(); repaint(0, getHeight() - kRailH - kFooterH, getWidth(), kFooterH);
  }
}

// Drains the engine's master tap into the loudness meter. Integrated loudness / LRA only accumulate while the
// transport plays, and start over when playback starts from the beginning of the song.
void MainComponent::pumpAnalysis(airdaw::Engine& e) {
  if (!loudness_) return;
  auto& tr = e.transport();
  const bool playing = tr.playing.load() && !tr.countingIn();
  if (playing && !wasPlaying_ && (double)tr.pos.load() < e.sampleRate() * 0.05) loudness_->resetIntegrated();
  wasPlaying_ = playing;
  for (size_t got; (got = e.readMasterTap(tapBuf_.data(), tapBuf_.size() / 2)) > 0;) loudness_->process(tapBuf_.data(), got, playing);
  if (tick_ % 2 == 0) mixer_.master().setLoudness(loudness_->read());
}

// ---------------------------------------------------------------- snapshots (test hook)
void MainComponent::runScenario(const juce::String& name) {
  auto* e = bridge_.engine(); if (!e) return;
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
  if (name == "reorder") { inserts_.show(4, 2); inserts_.moveInsert(4, 2, 0); }  // ToTape8 to the front, card stays open
  if (name == "add") { inserts_.show(0, 3); }
  if (name == "display") toggleDisplayPanel();
  if (name == "blue") { T().hue = 215.f; applyTheme(); }
  if (name == "glow-off") { T().glow = 0.f; applyTheme(); }
  if (name == "nodock") { dockShown_ = false; }
  resized();
}

void MainComponent::takeSnapshot(const juce::File& png, const juce::String& scenario) {
  juce::Timer::callAfterDelay(1500, [this, png, scenario] {
    runScenario(scenario);
    juce::Timer::callAfterDelay(scenario == "export" ? 12000 : 1500, [this, png] {  // an export needs time to finish
      juce::Component* top = getTopLevelComponent();
      if (pluginInfo_ && pluginInfo_->isVisible()) top = pluginInfo_.get();  // "info" scenario: picture the About window
      auto img = top->createComponentSnapshot(top->getLocalBounds(), true, 1.f);
      png.deleteFile();
      juce::FileOutputStream out(png);
      juce::PNGImageFormat().writeImageToStream(img, out);
      out.flush();
      juce::JUCEApplication::getInstance()->systemRequestedQuit();
    });
  });
}
