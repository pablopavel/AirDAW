#include "MainComponent.h"
#include "Vst3Insert.h"

static constexpr int kMeterSlot = 7;  // last master insert slot: the meter listens to the final mix

static juce::String timeText(double sec) {
  int ms = (int)std::fmod(sec * 1000.0, 1000.0), s = (int)sec % 60, m = ((int)sec / 60) % 60, h = (int)sec / 3600;
  return juce::String::formatted("%02d:%02d:%02d.%03d", h, m, s, ms);
}

MainComponent::MainComponent() {
  juce::PropertiesFile::Options opt;
  opt.applicationName = "AirDAW"; opt.filenameSuffix = ".settings"; opt.osxLibrarySubFolder = "Application Support";
  opt.folderName = "AirDAW";
  props_ = std::make_unique<juce::PropertiesFile>(opt);

  bridge_.setInsertFactory([](const std::string& id, double sr) -> std::unique_ptr<airdaw::InsertProcessor> {
    if (id.rfind("vst3:", 0) != 0) return nullptr;
    juce::String err;
    return Vst3Insert::load(juce::File(juce::String(id.substr(5))), sr, err);
  });
  juce::String err = devices_.initialiseWithDefaultDevices(2, 2);  // Black Lion Revolution 2x2 is 2-in/2-out
  if (err.isNotEmpty()) juce::Logger::writeToLog("Audio init: " + err);
  devices_.addAudioCallback(&bridge_);

  selector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(devices_, 0, 2, 0, 2, false, false, true, false);
  for (juce::Component* c : std::initializer_list<juce::Component*>{selector_.get(), &rtz_, &play_, &stop_, &rec_, &arm1_, &arm2_, &tone_,
       &save_, &saveAs_, &open_, &new_, &meter_, &meterEd_, &autoSave_, &autoEvery_, &status_, &pos_, &notice_label_})
    addAndMakeVisible(*c);

  rtz_.onClick = [this] { if (auto* e = bridge_.engine()) e->transport().seek(0); };
  play_.onClick = [this] { if (auto* e = bridge_.engine()) e->transport().play(); };
  stop_.onClick = [this] {
    if (auto* e = bridge_.engine()) { e->transport().stop(); e->flushRecording(); collectTakes(); }
  };
  rec_.onClick = [this] {
    auto* e = bridge_.engine(); if (!e) return;
    auto dir = sessionFolder().getChildFile("audio"); dir.createDirectory();
    e->startRecording(dir.getFullPathName().toStdString());
  };
  rec_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6a1a1a));
  arm1_.onClick = [this] { if (auto* e = bridge_.engine()) { e->track(0).inputL = 0; e->track(0).stereo = false; e->track(0).armed = arm1_.getToggleState(); } };
  arm2_.onClick = [this] { if (auto* e = bridge_.engine()) { e->track(1).inputL = 1; e->track(1).stereo = false; e->track(1).armed = arm2_.getToggleState(); } };
  tone_.onClick = [this] { bridge_.testTone = tone_.getToggleState(); };

  meter_.onClick = [this] { chooseMeterPlugin(); };
  meterEd_.onClick = [this] { openMeterEditor(); };
  new_.onClick = [this] { newSession(); };
  open_.onClick = [this] { openSession(); };
  save_.onClick = [this] { saveSession(false); };
  saveAs_.onClick = [this] { saveSession(true); };

  // Auto-save options (off by default; remembered between runs).
  const std::pair<int, int> opts[] = {{1, 60}, {2, 120}, {3, 300}, {4, 600}, {5, 900}, {6, 1800}};
  const char* labels[] = {"1 min", "2 min", "5 min", "10 min", "15 min", "30 min"};
  for (int i = 0; i < 6; ++i) autoEvery_.addItem(labels[i], opts[i].first);
  int savedSec = props_->getIntValue("autosaveSec", 300);
  autoEvery_.setSelectedId(3, juce::dontSendNotification);
  for (auto& o : opts) if (o.second == savedSec) autoEvery_.setSelectedId(o.first, juce::dontSendNotification);
  autoSave_.setToggleState(props_->getBoolValue("autosaveOn", false), juce::dontSendNotification);
  autoSave_.onClick = [this] { applyAutoSaveSettings(); note(autoSave_.getToggleState() ? "Auto-save on" : "Auto-save off"); };
  autoEvery_.onChange = [this] { applyAutoSaveSettings(); };

  status_.setFont(juce::FontOptions(16.f, juce::Font::bold));
  pos_.setFont(juce::FontOptions(34.f, juce::Font::bold)); pos_.setJustificationType(juce::Justification::centred);
  pos_.setColour(juce::Label::textColourId, juce::Colour(0xfff0b030));
  notice_label_.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
  setSize(780, 760);
  startTimerHz(15);
}

MainComponent::~MainComponent() {
  meterWindow_.reset();
  devices_.removeAudioCallback(&bridge_);
  props_->saveIfNeeded();
}

juce::File MainComponent::sessionFolder() const {
  if (sessionFile_ != juce::File()) return sessionFile_.getParentDirectory();
  return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("AirDAW").getChildFile("Untitled");
}
juce::String MainComponent::sessionName() const {
  return sessionFile_ != juce::File() ? sessionFile_.getFileNameWithoutExtension() : juce::String("Untitled");
}

void MainComponent::setSessionFile(const juce::File& f) {
  sessionFile_ = f;
  if (autoSaver_) autoSaver_->setLocation(sessionFolder().getChildFile("autosave").getFullPathName().toStdString(),
                                          sessionName().toStdString());
  getTopLevelComponent()->setName("AirDAW - " + sessionName());
}

void MainComponent::applyAutoSaveSettings() {
  const int secs[] = {0, 60, 120, 300, 600, 900, 1800};
  airdaw::AutoSaveSettings s;
  s.enabled = autoSave_.getToggleState(); s.intervalSec = secs[juce::jlimit(1, 6, autoEvery_.getSelectedId())]; s.keep = 10;
  if (autoSaver_) autoSaver_->configure(s);
  props_->setValue("autosaveOn", s.enabled); props_->setValue("autosaveSec", s.intervalSec); props_->saveIfNeeded();
}

void MainComponent::newSession() {
  // A new session is a fresh engine state: reload an empty one by rebuilding via the bridge's device restart.
  juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon)
      .withTitle("New session").withMessage("Discard the current mixer state and start a new session?")
      .withButton("New session", 1).withButton("Cancel", 0),
      [this](int r) {
        if (r != 1) return;
        auto* e = bridge_.engine(); if (!e) return;
        e->transport().stop();
        airdaw::Engine blank(e->sampleRate());
        auto tmp = (juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("airdaw_blank.airdaw")).getFullPathName().toStdString();
        airdaw::saveSession(blank, tmp); airdaw::loadSession(*e, tmp);
        setSessionFile({}); note("New session");
      });
}

void MainComponent::saveSession(bool saveAs) {
  auto* e = bridge_.engine(); if (!e) return;
  auto doSave = [this, e](const juce::File& f) {
    auto r = airdaw::saveSession(*e, f.getFullPathName().toStdString());
    if (r.ok) { setSessionFile(f); note("Saved " + f.getFileName()); } else note("SAVE FAILED: " + r.error);
  };
  if (!saveAs && sessionFile_ != juce::File()) { doSave(sessionFile_); return; }
  auto start = sessionFolder(); start.createDirectory();
  chooser_ = std::make_shared<juce::FileChooser>("Save session", start.getChildFile(sessionName() + ".airdaw"), "*.airdaw");
  chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                        juce::FileBrowserComponent::warnAboutOverwriting,
      [doSave](const juce::FileChooser& c) { auto f = c.getResult(); if (f != juce::File()) doSave(f.withFileExtension("airdaw")); });
}

void MainComponent::openSession() {
  chooser_ = std::make_shared<juce::FileChooser>("Open session", sessionFolder(), "*.airdaw");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
      [this](const juce::FileChooser& c) {
        auto f = c.getResult(); auto* e = bridge_.engine(); if (f == juce::File() || !e) return;
        auto r = airdaw::loadSession(*e, f.getFullPathName().toStdString());
        if (!r.ok) { note("OPEN FAILED: " + r.error); return; }
        setSessionFile(f);
        arm1_.setToggleState(e->track(0).armed.load(), juce::dontSendNotification);
        arm2_.setToggleState(e->track(1).armed.load(), juce::dontSendNotification);
        note(r.warnings.empty() ? "Opened " + f.getFileName() : "Opened with " + juce::String((int)r.warnings.size()) + " warning(s): " + r.warnings[0]);
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

void MainComponent::chooseMeterPlugin() {
  // The Airwindows Hit Record Meter is a separate VST3 (not part of Consolidated); the user picks the file once.
  auto start = juce::File(props_->getValue("meterPath", juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName()));
  chooser_ = std::make_shared<juce::FileChooser>("Select the Hit Record Meter .vst3", start.getParentDirectory(), "*.vst3");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                        juce::FileBrowserComponent::canSelectDirectories,  // some platforms ship VST3 as a bundle folder
      [this](const juce::FileChooser& c) { auto f = c.getResult(); if (f != juce::File()) loadMeter(f); });
}

void MainComponent::loadMeter(const juce::File& vst3) {
  auto* e = bridge_.engine(); if (!e) return;
  juce::String err;
  auto ins = Vst3Insert::load(vst3, e->sampleRate(), err);
  if (!ins) { note("Meter load failed: " + err); return; }
  props_->setValue("meterPath", vst3.getFullPathName()); props_->saveIfNeeded();
  if (!e->setInsertProcessor(200, kMeterSlot, std::move(ins))) { note("Could not install the meter"); return; }
  note("Meter loaded on master insert " + juce::String(kMeterSlot + 1));
  openMeterEditor();
}

void MainComponent::openMeterEditor() {
  auto* e = bridge_.engine(); if (!e) return;
  auto* v = dynamic_cast<Vst3Insert*>(e->insertAt(200, kMeterSlot));
  if (!v) { note("No meter loaded yet"); return; }
  meterWindow_.reset();
  meterWindow_ = std::make_unique<PluginWindow>(v->plugin(), [this] {
    juce::MessageManager::callAsync([this] { meterWindow_.reset(); });  // never delete a window inside its own callback
  });
}

void MainComponent::collectTakes() {
  auto* e = bridge_.engine(); if (!e) return;
  for (auto& t : e->pollFinishedTakes()) {
    int idx = e->addTake(t.track, t.path, t.start);
    if (idx >= 0) e->setActiveTake(t.track, idx);
    if (t.droppedFrames) note("WARNING: disk fell behind, " + juce::String((int)t.droppedFrames) + " frames dropped on track " + juce::String(t.track + 1));
  }
}

void MainComponent::timerCallback() {
  auto* e = bridge_.engine();
  if (e && e != autoEngine_) {  // first run, or the engine was rebuilt for a new sample rate
    autoEngine_ = e;
    meterWindow_.reset();  // the engine (and its hosted plugins) was rebuilt
    autoSaver_ = std::make_unique<airdaw::AutoSaver>(*e);
    setSessionFile(sessionFile_);
    applyAutoSaveSettings();
    if (!recoveryChecked_) { recoveryChecked_ = true; checkRecovery(); }
  }
  if (e) {
    collectTakes();
    auto saved = autoSaver_->tick(juce::Time::getMillisecondCounterHiRes() / 1000.0);
    if (!saved.empty()) note("Auto-saved " + juce::Time::getCurrentTime().toString(false, true));
    pos_.setText(timeText((double)e->transport().pos.load() / e->sampleRate()), juce::dontSendNotification);
    meterL_ = e->master().meter.peakL.load(); meterR_ = e->master().meter.peakR.load();
    rec_.setToggleState(e->transport().recording.load(), juce::dontSendNotification);
    rec_.setColour(juce::TextButton::buttonColourId, e->transport().recording.load() ? juce::Colours::red : juce::Colour(0xff6a1a1a));
  }
  auto* dev = devices_.getCurrentAudioDevice();
  if (!dev) status_.setText("No audio device open", juce::dontSendNotification);
  else {
    const double sr = bridge_.sampleRate(); const int buf = bridge_.bufferSamples(), lat = bridge_.deviceLatencySamples();
    status_.setText(juce::String(dev->getTypeName()) + " | " + juce::String(sr / 1000.0, 1) + " kHz | buffer " + juce::String(buf) +
                    " smp | round-trip ~" + juce::String(1000.0 * (buf + lat) / sr, 2) + " ms | DSP " +
                    juce::String(int(devices_.getCpuUsage() * 100)) + "%", juce::dontSendNotification);
  }
  notice_label_.setText(juce::Time::getMillisecondCounter() < noticeUntil_ ? notice_ : juce::String(), juce::dontSendNotification);
  repaint();
}

void MainComponent::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xff15171a));
  auto r = getLocalBounds().removeFromBottom(40).reduced(12, 10);
  g.setColour(juce::Colour(0xff0a0b0d)); g.fillRect(r);
  auto bar = [&](juce::Rectangle<int> b, float v) {
    float db = juce::Decibels::gainToDecibels(v, -60.f);
    g.setColour(db > -3.f ? juce::Colours::red : juce::Colour(0xfff0b030));
    g.fillRect(b.withWidth((int)juce::jmap(db, -60.f, 0.f, 0.f, (float)b.getWidth())));
  };
  bar(r.removeFromTop(8), meterL_); r.removeFromTop(4); bar(r.removeFromTop(8), meterR_);
}

void MainComponent::resized() {
  auto r = getLocalBounds().reduced(12);
  status_.setBounds(r.removeFromTop(26));
  pos_.setBounds(r.removeFromTop(50));
  auto row = r.removeFromTop(34);
  for (auto* b : {&rtz_, &play_, &stop_, &rec_}) b->setBounds(row.removeFromLeft(90).reduced(2));
  row.removeFromLeft(10);
  arm1_.setBounds(row.removeFromLeft(130)); arm2_.setBounds(row.removeFromLeft(130));
  tone_.setBounds(r.removeFromTop(26));
  row = r.removeFromTop(34);
  for (auto* b : {&new_, &open_, &save_, &saveAs_}) b->setBounds(row.removeFromLeft(90).reduced(2));
  meter_.setBounds(row.removeFromLeft(150).reduced(2)); meterEd_.setBounds(row.removeFromLeft(110).reduced(2));
  row.removeFromLeft(10);
  autoSave_.setBounds(row.removeFromLeft(140)); autoEvery_.setBounds(row.removeFromLeft(90).reduced(0, 4));
  notice_label_.setBounds(r.removeFromTop(26));
  r.removeFromBottom(40);
  selector_->setBounds(r);
}
