#include "PluginsPanel.h"
#include <thread>
#include "airdaw/fx_host.h"
#include "airdaw/fx_library.h"

namespace ui {

// ---------------------------------------------------------------- library at startup
namespace pluginlib {
namespace {
juce::File& rootOverride() { static juce::File f; return f; }
juce::StringArray& newNames() { static juce::StringArray s; return s; }
juce::DynamicLibrary& dll() { static auto* d = new juce::DynamicLibrary(); return *d; }  // stays loaded until exit
}  // namespace

juce::File root() {
  if (rootOverride() != juce::File()) return rootOverride();
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("AirDAW").getChildFile("airwindows");
}
void setRoot(const juce::File& f) { rootOverride() = f; }

juce::String activate(juce::PropertiesFile& p) {
  juce::String err;
  const auto build = p.getValue("plugins.active");
  if (build.isNotEmpty()) {
    const auto dir = root().getChildFile(build), file = dir.getChildFile("AirwindowsFX.dll");
    const auto man = juce::JSON::parse(dir.getChildFile("manifest.json"));
    if (!file.existsAsFile()) err = "build " + build + " is missing";
    else if (man["sha256"].toString().compareIgnoreCase(juce::SHA256(file).toHexString()) != 0) err = "build " + build + " failed its checksum";
    else if (!dll().open(file.getFullPathName())) err = "build " + build + " could not be loaded";
    else {
      auto fn = reinterpret_cast<const AwlApi* (*)()>(dll().getFunction(airdaw::kFxLibEntryPoint));
      if (!fn || !airdaw::useFxLibrary(fn())) { err = "build " + build + " does not fit this AirDAW"; dll().close(); }
    }
  }
  // Effects this build added since the build used before ("new" tags in the picker until the next change).
  const juce::String cur = airdaw::activeFxLibrary().buildId;
  if (p.getValue("plugins.lastBuild") != cur) {
    juce::StringArray names;
    for (auto& f : airdaw::listEffects()) names.add(juce::String(f.name));
    const auto prev = juce::StringArray::fromTokens(p.getValue("plugins.lastNames"), "\n", "");
    newNames().clear();
    if (!prev.isEmpty()) for (auto& n : names) if (!prev.contains(n)) newNames().add(n);
    p.setValue("plugins.newNames", newNames().joinIntoString("\n"));
    p.setValue("plugins.lastNames", names.joinIntoString("\n"));
    p.setValue("plugins.lastBuild", cur);
  } else newNames() = juce::StringArray::fromTokens(p.getValue("plugins.newNames"), "\n", "");
  return err;
}

juce::String runningBuild() { return airdaw::activeFxLibrary().buildId; }
bool runningIsBuiltin() { return !airdaw::fxLibraryLoaded(); }
const juce::StringArray& newEffects() { return newNames(); }
}  // namespace pluginlib

// ---------------------------------------------------------------- network
static const char* kDefaultFeed = "https://api.github.com/repos/pablopavel/AirDAW/releases?per_page=30";

static std::unique_ptr<juce::InputStream> openUrl(const juce::String& url, int* status) {
  juce::URL u(url);
  if (u.isLocalFile()) return u.getLocalFile().createInputStream();  // test feeds
  return u.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                 .withExtraHeaders("Accept: application/vnd.github+json, application/octet-stream\r\nUser-Agent: AirDAW\r\n")
                                 .withConnectionTimeoutMs(20000).withNumRedirectsToFollow(8).withStatusCode(status));
}

static juce::String httpGet(const juce::String& url, juce::String& err) {
  int status = 0;
  auto in = openUrl(url, &status);
  if (!in) { err = "no connection (" + juce::URL(url).getDomain() + ")"; return {}; }
  auto s = in->readEntireStreamAsString();
  if (status >= 400) { err = "the server answered " + juce::String(status); return {}; }
  return s;
}

// ---------------------------------------------------------------- panel
PluginsPanel::PluginsPanel(AppCtx& ctx) : ctx_(ctx) {
  for (auto* b : {&checkBtn_, &installBtn_, &rollBtn_}) { addAndMakeVisible(*b); b->getProperties().set("fontH", 11.5); b->getProperties().set("noUpper", true); }
  checkBtn_.onClick = [this] { check(); };
  installBtn_.onClick = [this] { install(); };
  rollBtn_.onClick = [this] { rollBack(); };
  installBtn_.setTooltip("Download the new build, check its SHA-256 and install it beside the current one. It activates at the next launch.");
  rollBtn_.setTooltip("Go back to the build used before (it activates at the next launch)");
  const char* modes[3] = {"Manual", "On launch", "Weekly"};
  const char* ids[3] = {"manual", "launch", "weekly"};
  for (int i = 0; i < 3; ++i) {
    auto& b = mode_[(size_t)i]; b.setButtonText(modes[i]); b.getProperties().set("fontH", 11.5); b.getProperties().set("noUpper", true);
    const juce::String id = ids[i];
    b.onClick = [this, id] { ctx_.props.setValue("plugins.check", id); sync(); };
    addAndMakeVisible(b);
  }
  const char* olds[2] = {"Show", "Hide"};
  for (int i = 0; i < 2; ++i) {
    auto& b = old_[(size_t)i]; b.setButtonText(olds[i]); b.getProperties().set("fontH", 11.5); b.getProperties().set("noUpper", true);
    b.onClick = [this, i] { ctx_.props.setValue("plugins.showOld", i == 0); sync(); };
    addAndMakeVisible(b);
  }
  sync();
  setSize(kWidth, preferredHeight());
}

PluginsPanel::~PluginsPanel() { alive_->store(false); }

juce::String PluginsPanel::pendingBuild() const { return ctx_.props.getValue("plugins.active"); }

void PluginsPanel::setState(St s, const juce::String& msg) {
  st_ = s; msg_ = msg;
  if (s == St::Downloading) startTimerHz(10); else stopTimer();
  sync();
  setSize(kWidth, preferredHeight());
  if (layoutChanged) layoutChanged();
}

void PluginsPanel::sync() {
  const bool busy = st_ == St::Checking || st_ == St::Downloading;
  checkBtn_.setEnabled(!busy);
  installBtn_.setEnabled(st_ == St::Available);
  installBtn_.setToggleState(st_ == St::Available, juce::dontSendNotification);
  rollBtn_.setEnabled(!busy && pendingBuild().isNotEmpty());
  const auto mode = ctx_.props.getValue("plugins.check", "manual");
  const char* ids[3] = {"manual", "launch", "weekly"};
  for (int i = 0; i < 3; ++i) mode_[(size_t)i].setToggleState(mode == ids[i], juce::dontSendNotification);
  const bool showOld = ctx_.props.getBoolValue("plugins.showOld", false);
  old_[0].setToggleState(showOld, juce::dontSendNotification); old_[1].setToggleState(!showOld, juce::dontSendNotification);
  repaint();
}

void PluginsPanel::startupCheck() {
  const auto mode = ctx_.props.getValue("plugins.check", "manual");
  const auto last = juce::Time(ctx_.props.getValue("plugins.lastCheck", "0").getLargeIntValue());
  if (mode == "launch" || (mode == "weekly" && juce::Time::getCurrentTime() - last >= juce::RelativeTime::days(7))) check();
}

void PluginsPanel::check() {
  if (st_ == St::Checking || st_ == St::Downloading) return;
  setState(St::Checking, juce::String(juce::CharPointer_UTF8("Checking\xe2\x80\xa6")));
  const juce::String feed = ctx_.props.getValue("plugins.feed", kDefaultFeed);
  juce::Component::SafePointer<PluginsPanel> sp(this);
  auto alive = alive_;
  std::thread([sp, alive, feed] {
    juce::String err; Release r; bool found = false;
    const auto json = httpGet(feed, err);
    const juce::var parsed = juce::JSON::parse(json);  // keep the parsed tree alive while walking it
    if (err.isEmpty()) {
      if (auto* arr = parsed.getArray()) {
        for (auto& rel : *arr) {  // newest first
          if ((bool)rel["draft"] || !rel["tag_name"].toString().startsWith("airwindows-")) continue;
          if (auto* assets = rel["assets"].getArray())
            for (auto& a : *assets) {
              if (a["name"].toString() == "manifest.json") r.manifestUrl = a["browser_download_url"].toString();
              if (a["name"].toString() == "AirwindowsFX-win64.zip") r.zipUrl = a["browser_download_url"].toString();
            }
          if (r.manifestUrl.isNotEmpty() && r.zipUrl.isNotEmpty()) { r.tag = rel["tag_name"].toString(); found = true; break; }
          r = Release();
        }
      } else err = "unexpected answer from the releases feed";
    }
    if (found) {
      const auto m = juce::JSON::parse(httpGet(r.manifestUrl, err));
      r.build = m["build"].toString(); r.sha = m["sha256"].toString(); r.api = (int)m["api"];
      if (auto* e = m["effects"].getArray()) for (auto& n : *e) r.effects.add(n.toString());
      if (err.isEmpty() && (r.build.isEmpty() || r.sha.isEmpty())) err = "the newest build has no valid manifest";
    }
    juce::MessageManager::callAsync([sp, alive, r, found, err] {
      if (!alive->load() || !sp) return;
      auto& self = *sp;
      self.ctx_.props.setValue("plugins.lastCheck", juce::String(juce::Time::currentTimeMillis()));
      if (err.isNotEmpty()) { self.setState(St::Error, "Could not check: " + err); return; }
      const juce::String have = juce::jmax(pluginlib::runningBuild(), self.pendingBuild());
      if (!found || r.build.compare(have) <= 0) {
        self.setState(St::UpToDate, found ? "Checked just now: you are up to date." : "Checked just now: no plugin library builds are published yet.");
        if (self.onUpdateAvailable) self.onUpdateAvailable(false);
        return;
      }
      if (r.api != airdaw::kFxLibApi) { self.setState(St::Error, "Build " + r.build + " needs a newer AirDAW."); return; }
      self.rel_ = r;
      self.whatsNew_.clear();
      juce::StringArray cur; for (auto& f : airdaw::listEffects()) cur.add(juce::String(f.name));
      for (auto& n : r.effects) if (!cur.contains(n)) self.whatsNew_.add(n);
      self.setState(St::Available, "Update available.");
      if (self.onUpdateAvailable) self.onUpdateAvailable(true);
    });
  }).detach();
}

void PluginsPanel::install() {
  if (st_ != St::Available) return;
  const Release r = rel_;
  progress_ = 0.f;
  setState(St::Downloading);
  juce::Component::SafePointer<PluginsPanel> sp(this);
  auto alive = alive_;
  auto* prog = &progress_;
  std::thread([sp, alive, r, prog] {
    juce::String err;
    const auto zipFile = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("AirwindowsFX-" + r.build + ".zip");
    const auto dest = pluginlib::root().getChildFile(r.build), part = pluginlib::root().getChildFile(r.build + ".part");
    {  // download with progress
      int status = 0;
      auto in = openUrl(r.zipUrl, &status);
      if (!in || status >= 400) err = "download failed" + (status >= 400 ? " (" + juce::String(status) + ")" : juce::String());
      else {
        zipFile.deleteFile();
        juce::FileOutputStream out(zipFile);
        const auto total = in->getTotalLength();
        juce::HeapBlock<char> buf(1 << 16); juce::int64 done = 0;
        while (out.openedOk() && !in->isExhausted() && alive->load()) {
          const int n = in->read(buf, 1 << 16);
          if (n <= 0) break;
          out.write(buf, (size_t)n); done += n;
          if (total > 0) prog->store(0.9f * (float)done / (float)total);
        }
        if (!out.openedOk() || done == 0) err = "could not save the download";
      }
    }
    if (err.isEmpty()) {  // unpack beside the other builds, verify, then move into place
      part.deleteRecursively(); part.createDirectory();
      juce::ZipFile zip(zipFile);
      const auto dllFile = part.getChildFile("AirwindowsFX.dll");
      if (zip.getNumEntries() == 0 || zip.uncompressTo(part).failed() || !dllFile.existsAsFile()) err = "the download is not a valid plugin library";
      else {
        prog->store(0.95f);
        const auto man = juce::JSON::parse(part.getChildFile("manifest.json"));
        const auto sha = juce::SHA256(dllFile).toHexString();
        if (sha.compareIgnoreCase(r.sha) != 0 || man["sha256"].toString().compareIgnoreCase(r.sha) != 0) err = "verification failed (SHA-256 mismatch)";
        else if (man["build"].toString() != r.build) err = "verification failed (wrong build)";
      }
      if (err.isEmpty()) { dest.deleteRecursively(); if (!part.moveFileTo(dest)) err = "could not install into " + dest.getFullPathName(); }
      if (err.isNotEmpty()) part.deleteRecursively();
    }
    zipFile.deleteFile();
    prog->store(-1.f);
    juce::MessageManager::callAsync([sp, alive, r, err] {
      if (!alive->load() || !sp) return;
      auto& self = *sp;
      if (err.isNotEmpty()) { self.setState(St::Available, "Install failed: " + err); return; }
      auto prev = juce::StringArray::fromTokens(self.ctx_.props.getValue("plugins.previous"), "\n", "");
      prev.add(self.pendingBuild().isEmpty() ? juce::String("builtin") : self.pendingBuild());
      self.ctx_.props.setValue("plugins.previous", prev.joinIntoString("\n"));
      self.ctx_.props.setValue("plugins.active", r.build);
      self.ctx_.props.saveIfNeeded();
      self.setState(St::Installed, "Installed build " + r.build + ". Restart AirDAW to activate it.");
      if (self.onUpdateAvailable) self.onUpdateAvailable(false);
    });
  }).detach();
}

void PluginsPanel::rollBack() {
  auto prev = juce::StringArray::fromTokens(ctx_.props.getValue("plugins.previous"), "\n", "");
  juce::String target;  // "" = built in
  while (!prev.isEmpty()) {  // the most recent earlier build that is still on disk
    const auto t = prev[prev.size() - 1]; prev.remove(prev.size() - 1);
    if (t == "builtin" || pluginlib::root().getChildFile(t).getChildFile("AirwindowsFX.dll").existsAsFile()) { target = t == "builtin" ? juce::String() : t; break; }
  }
  ctx_.props.setValue("plugins.previous", prev.joinIntoString("\n"));
  ctx_.props.setValue("plugins.active", target);
  ctx_.props.saveIfNeeded();
  setState(St::Idle, "Rolled back to " + (target.isEmpty() ? juce::String("the built-in build ") + airdaw::builtinFxLibrary().buildId : "build " + target) +
                         ". Restart AirDAW to activate it.");
}

int PluginsPanel::preferredHeight() const {
  int h = 14 + 30 + 2 * 24 + 8;            // title, installed / available
  if (st_ == St::Downloading) h += 30;
  if (msg_.isNotEmpty()) h += 34;
  if (loadError.isNotEmpty()) h += 34;
  h += 38;                                   // buttons
  if (st_ == St::Available && !whatsNew_.isEmpty()) h += 26 + juce::jmin(10, whatsNew_.size()) * 22 + (whatsNew_.size() > 10 ? 22 : 0);
  h += 2 * 34 + 8 + 50;                      // options, footnote
  return h;
}

void PluginsPanel::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  t.drawBrush(g, r);
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), t.rad(6.f), 1.f);
  auto in = getLocalBounds().reduced(16, 14);
  g.setColour(Theme::cream()); g.setFont(Theme::spaced(Theme::uiFont(11.5f, true), 0.2f));
  g.drawText("AIRWINDOWS PLUGINS", in.removeFromTop(30), juce::Justification::centredLeft);
  auto row = [&](const juce::String& cap, const juce::String& val, juce::Colour c) {
    auto rr = in.removeFromTop(24);
    g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(10.5f), 0.12f));
    g.drawText(cap, rr, juce::Justification::centredLeft);
    g.setFont(Theme::monoFont(12.f, false)); g.setColour(c);
    g.drawText(val, rr.withTrimmedLeft(90), juce::Justification::centredRight, true);
    g.setColour(t.am().withAlpha(0.16f)); g.fillRect(rr.getX(), rr.getBottom() - 1, rr.getWidth(), 1);
  };
  const juce::String dot = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
  juce::String inst = "Consolidated" + dot + "build " + pluginlib::runningBuild() + (pluginlib::runningIsBuiltin() ? " (built in)" : "");
  row("INSTALLED", inst, t.readout());
  juce::String avail;
  switch (st_) {
    case St::Available: case St::Downloading: avail = "build " + rel_.build + dot + juce::String(whatsNew_.size()) + " new"; break;
    case St::Installed: avail = "after restart: build " + pendingBuild(); break;
    case St::UpToDate: avail = "up to date"; break;
    case St::Checking: avail = juce::String(juce::CharPointer_UTF8("checking\xe2\x80\xa6")); break;
    default: avail = pendingBuild() != (pluginlib::runningIsBuiltin() ? juce::String() : pluginlib::runningBuild())
                         ? "after restart: " + (pendingBuild().isEmpty() ? juce::String("built in") : "build " + pendingBuild())
                         : juce::String("not checked yet");
  }
  row("AVAILABLE", avail, st_ == St::Available ? t.readout() : Theme::dim());
  in.removeFromTop(8);
  if (st_ == St::Downloading) {
    auto pr = in.removeFromTop(30);
    const float f = juce::jmax(0.f, progress_.load());
    auto bar = pr.removeFromTop(10).toFloat();
    t.drawWell(g, bar, juce::Colour(0xff030303), 2.f);
    g.setColour(t.am()); g.fillRect(bar.withWidth(bar.getWidth() * f).reduced(1.f));
    g.setColour(Theme::dim()); g.setFont(Theme::uiFont(11.f));
    g.drawText(juce::String(juce::CharPointer_UTF8("Downloading and verifying\xe2\x80\xa6")), pr, juce::Justification::centredLeft);
  }
  if (msg_.isNotEmpty()) {
    g.setColour(st_ == St::Error || msg_.startsWith("Install failed") ? juce::Colour(0xffff6b6b) : t.am()); g.setFont(Theme::uiFont(12.f));
    g.drawFittedText(msg_, in.removeFromTop(34), juce::Justification::centredLeft, 2);
  }
  if (loadError.isNotEmpty()) {
    g.setColour(juce::Colour(0xffff6b6b)); g.setFont(Theme::uiFont(12.f));
    g.drawFittedText("Using the built-in build: " + loadError, in.removeFromTop(34), juce::Justification::centredLeft, 2);
  }
  in.removeFromTop(38);  // buttons
  if (st_ == St::Available && !whatsNew_.isEmpty()) {
    g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(10.5f), 0.12f));
    g.drawText("WHAT'S NEW", in.removeFromTop(26), juce::Justification::centredLeft);
    for (int i = 0; i < juce::jmin(10, whatsNew_.size()); ++i) {
      auto rr = in.removeFromTop(22);
      g.setColour(Theme::tx()); g.setFont(Theme::uiFont(12.5f));
      g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x98\x85 ")) + whatsNew_[i], rr, juce::Justification::centredLeft);
      g.setColour(Theme::dim()); g.drawText("new effect", rr, juce::Justification::centredRight);
    }
    if (whatsNew_.size() > 10) { g.setColour(Theme::dim()); g.drawText("+ " + juce::String(whatsNew_.size() - 10) + " more", in.removeFromTop(22), juce::Justification::centredLeft); }
  }
  g.setColour(Theme::dim()); g.setFont(Theme::uiFont(12.f));
  g.drawText("Check for updates", in.removeFromTop(34), juce::Justification::centredLeft);
  g.drawText("Older versions in picker", in.removeFromTop(34), juce::Justification::centredLeft);
  in.removeFromTop(8);
  g.setFont(Theme::uiFont(11.f));
  g.drawFittedText("Nothing is installed unless you press Install update. Old builds are kept so you can roll back. "
                   "New builds activate at the next launch.", in.removeFromTop(48), juce::Justification::topLeft, 3);
}

void PluginsPanel::resized() {
  auto in = getLocalBounds().reduced(16, 14);
  in.removeFromTop(30 + 2 * 24 + 8);
  if (st_ == St::Downloading) in.removeFromTop(30);
  if (msg_.isNotEmpty()) in.removeFromTop(34);
  if (loadError.isNotEmpty()) in.removeFromTop(34);
  auto br = in.removeFromTop(38).withTrimmedBottom(8);
  const int bw = (br.getWidth() - 8) / 3;
  checkBtn_.setBounds(br.removeFromLeft(bw)); br.removeFromLeft(4);
  installBtn_.setBounds(br.removeFromLeft(bw)); br.removeFromLeft(4);
  rollBtn_.setBounds(br);
  if (st_ == St::Available && !whatsNew_.isEmpty()) in.removeFromTop(26 + juce::jmin(10, whatsNew_.size()) * 22 + (whatsNew_.size() > 10 ? 22 : 0));
  auto m = in.removeFromTop(34).withTrimmedLeft(170).reduced(0, 5);
  const int mw = m.getWidth() / 3;
  for (auto& b : mode_) { b.setBounds(m.removeFromLeft(mw).reduced(2, 0)); }
  auto o = in.removeFromTop(34).withTrimmedLeft(170).reduced(0, 5);
  const int ow = o.getWidth() / 2;
  for (auto& b : old_) { b.setBounds(o.removeFromLeft(ow).reduced(2, 0)); }
}

}  // namespace ui
