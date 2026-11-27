// Hit Record Meter, dark view: a port of the Airwindows Meter display (github.com/airwindows/Meter, MIT License,
// Copyright (c) 2026 Chris Johnson). The measurements, scaling, layout, sweep, letter grades and score are the
// original's (the code below is the original's, adapted); only the colours change: a dark background, the dots
// with their lightness turned over (same hue), the tone colour (top bar, compact backdrop) kept dim in its hue, and
// the scale, rules, labels and sweep line in the accent. It measures what the meter plugin measures, from the same signal (what is heard on the
// master), on the UI side.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <deque>

namespace ui {

// The Meter plugin's processBlock measurements, per sample, in windows of 1881 samples at 44.1 kHz (the same time at
// any rate). Each finished window is queued for the view.
class HitMeterAnalyzer {
 public:
  explicit HitMeterAnalyzer(double sampleRate) : sr_(sampleRate) {}
  struct Window { float pl, pr, sl, sr, zl, zr, bsl, bsr, brl, brr, bzl, bzr, dsl, dsr, drl, drr, dzl, dzr; };
  bool running = true;  // the view's click-to-freeze (the plugin's audioPlaying)
  void process(const float* il, size_t frames);  // interleaved stereo
  bool pop(Window& w) { if (out_.empty()) return false; w = out_.front(); out_.pop_front(); return true; }
 private:
  double sr_;
  double previousLeft = 0.0, previousRight = 0.0, slewLeft = 0.0, slewRight = 0.0, peakLeft = 0.0, peakRight = 0.0;
  double holdPeakLeft = 0.0, holdPeakRight = 0.0, beamSlewL = 0.0, beamSlewR = 0.0, departureSlewL = 0.0, departureSlewR = 0.0;
  double beamRMSL = 0.0, beamRMSR = 0.0, departureRMSL = 0.0, departureRMSR = 0.0;
  double zeroLeft = 0.0, zeroRight = 0.0, holdZeroLeft = 0.0, holdZeroRight = 0.0, longestZeroLeft = 0.0, longestZeroRight = 0.0;
  double beamZeroL = 0.0, beamZeroR = 0.0, departureZeroL = 0.0, departureZeroR = 0.0;
  bool wasPositiveL = false, wasPositiveR = false;
  double windowCount = 0.0;
  std::deque<Window> out_;
};

class HitMeterView : public juce::Component, private juce::Timer {
 public:
  HitMeterView();
  std::function<HitMeterAnalyzer*()> source;  // the master analysis' meter (rebuilt with the engine)
  void setActive(bool on) { if (on) startTimer(1000 / 30); else stopTimer(); }
  void paint(juce::Graphics& g) override;
  void resized() override { displayWidth = juce::jmax(1, getWidth()); displayHeight = juce::jmax(1, getHeight()); }
  void mouseDown(const juce::MouseEvent& event) override;  // click: freeze / run; double-click: start over
  // Test hook: run a block of audio through a private analyzer straight into the view.
  void feedForTest(const float* il, size_t frames, double sampleRate);

 private:
  void timerCallback() override;  // the plugin editor's idle(): take the finished windows
  void take(const HitMeterAnalyzer::Window& w);
  void paintMeter(juce::Graphics& g);  // the original paint, recoloured
  // dark-view colours
  static juce::Colour dark(juce::Colour c);  // same hue and saturation, lightness turned over
  static juce::Colour darkTone(juce::Colour c);  // the tone-colour backdrop and bar: same hue, kept dim
  static juce::Colour darkRGBA(float r, float g, float b, float a) { return dark(juce::Colour::fromFloatRGBA(r, g, b, a)); }
  static juce::Colour gridColour();
  static juce::Colour ruleColour();
  static juce::Colour beamColour();
  static juce::Colour textColour();
  static juce::Colour sweepColour();
  static const juce::Colour kBack;
  std::unique_ptr<HitMeterAnalyzer> testAnalyzer_;

  // ---- the original AirwindowsMeter's state
    static constexpr int dataPoints = 5151;
    static constexpr int totalBins = 14;
    juce::Colour backdropColour = juce::Colours::white;
    juce::Colour cachedColour = juce::Colours::white;
    juce::Colour pluginColour = juce::Colours::white; //will be set to the plugin's background color: for blanking the tonecolor bars
    int displayWidth = 1280;
    int displayHeight = 720;
    unsigned long dataPosition = 0;
    int peakGrade = 0;
    int slewGrade = 0;
    int bassGrade = 0;
    double peakPrevGrade = 0.0;
    double slewPrevGrade = 0.0;
    double bassPrevGrade = 0.0;
    double cumulative = 0.0000001;
    double duration = 0.00001;
    double cumulativePlus = 0.0000001;
    double durationPlus = 0.000001;
    juce::String totalPackage = juce::String();
    juce::String bonusScore = juce::String();
    juce::String directions = juce::String();
    float directionsOpacity;
    juce::String power = juce::String();
    juce::String detail = juce::String();
    juce::String authority = juce::String();
    double storeR;
    double storeG;
    double storeB; //these are used ONLY to draw colors on screen
    double outputR;
    double outputG;
    double outputB; //these get values applied to them to produce scores
    double cyanFix; //need this to run inside a switch statement
    double outputMax;
    double outputMin;
    double gradientMin; //need the simpler form for GFX
    double outputVol;
    double outputShift;
    double sustainedClip;
    bool meterPlaying;
    
    std::array<float, totalBins+2> peakTrack; //arrays can be counted from zero and have X elements
    std::array<float, totalBins+2> slewTrack; //there's a place where Meter works with <= totalBins
    std::array<float, totalBins+2> bassTrack; //let's make sure the arrays don't exceed their boundaries
    
    std::array<float, dataPoints+2> dataPL;
    std::array<float, dataPoints+2> dataPR;
    std::array<float, dataPoints+2> dataSL;
    std::array<float, dataPoints+2> dataSR;
    std::array<float, dataPoints+2> dataZL;
    std::array<float, dataPoints+2> dataZR;
    std::array<float, dataPoints+2> dataBSL;
    std::array<float, dataPoints+2> dataBSR;
    std::array<float, dataPoints+2> dataBRL;
    std::array<float, dataPoints+2> dataBRR;
    std::array<float, dataPoints+2> dataBZL;
    std::array<float, dataPoints+2> dataBZR;
    std::array<float, dataPoints+2> dataDSL;
    std::array<float, dataPoints+2> dataDSR;
    std::array<float, dataPoints+2> dataDRL;
    std::array<float, dataPoints+2> dataDRR;
    std::array<float, dataPoints+2> dataDZL;
    std::array<float, dataPoints+2> dataDZR;
    std::array<float, dataPoints+2> backR;//backdrop red channel: loud
    std::array<float, dataPoints+2> backG;//backdrop green channel: bright
    std::array<float, dataPoints+2> backB;//backdrop blue channel: bass

    void pushPeakL(float X) {dataPL[dataPosition] = X; dataPL[dataPosition+1] = X;}
    void pushPeakR(float X) {dataPR[dataPosition] = X; dataPR[dataPosition+1] = X;}
    void pushSlewL(float X) {dataSL[dataPosition] = X; dataSL[dataPosition+1] = X;}
    void pushSlewR(float X) {dataSR[dataPosition] = X; dataSR[dataPosition+1] = X;}
    void pushZeroL(float X) {dataZL[dataPosition] = X; dataZL[dataPosition+1] = X;}
    void pushZeroR(float X) {dataZR[dataPosition] = X; dataZR[dataPosition+1] = X;}
    void pushBSLWL(float X) {dataBSL[dataPosition] = X; dataBSL[dataPosition+1] = X;}
    void pushBSLWR(float X) {dataBSR[dataPosition] = X; dataBSR[dataPosition+1] = X;}
    void pushBRMSL(float X) {dataBRL[dataPosition] = X; dataBRL[dataPosition+1] = X;}
    void pushBRMSR(float X) {dataBRR[dataPosition] = X; dataBRR[dataPosition+1] = X;}
    void pushBZERL(float X) {dataBZL[dataPosition] = X; dataBZL[dataPosition+1] = X;}
    void pushBZERR(float X) {dataBZR[dataPosition] = X; dataBZR[dataPosition+1] = X;}
    void pushDSLWL(float X) {dataDSL[dataPosition] = X; dataDSL[dataPosition+1] = X;}
    void pushDSLWR(float X) {dataDSR[dataPosition] = X; dataDSR[dataPosition+1] = X;}
    void pushDRMSL(float X) {dataDRL[dataPosition] = X; dataDRL[dataPosition+1] = X;}
    void pushDRMSR(float X) {dataDRR[dataPosition] = X; dataDRR[dataPosition+1] = X;}
    void pushDZERL(float X) {dataDZL[dataPosition] = X; dataDZL[dataPosition+1] = X;}
    void pushDZERR(float X) {dataDZR[dataPosition] = X; dataDZR[dataPosition+1] = X;}
    void pushIncrement() {
        dataPosition++;
        if (dataPosition >= (unsigned long)fmin(displayWidth,5150)) dataPosition = 0;
    }

    void resetArrays(){
        dataPosition = 0;
        peakGrade = 0;
        slewGrade = 0;
        bassGrade = 0;
        peakPrevGrade = 0.0;
        slewPrevGrade = 0.0;
        bassPrevGrade = 0.0; //to bump a grade you have to exceed it persistently
        totalPackage = juce::String();
        bonusScore = juce::String();
        directions = juce::String();
        directionsOpacity = 0.0f;
        cumulative = 0.0000001;
        duration = 0.00001;
        cumulativePlus = 0.0000001;
        durationPlus = 0.00001;
        power = juce::String();
        detail = juce::String();
        authority = juce::String();
        storeR = 1.0f;
        storeG = 1.0f;
        storeB = 1.0f;
        cyanFix = 1.0f;
        outputR = 10.0f;
        outputG = 10.0f;
        outputB = 10.0f;
        outputMin = 0.0f;
        gradientMin = 0.0f;
        outputVol = 0.0f;
        sustainedClip = 180.0f;
        meterPlaying = true;
        backdropColour = juce::Colours::white;
        cachedColour = juce::Colours::white;
        
        for (unsigned long count = 0; count < totalBins; ++count) //count through all the points in the array
        {
            peakTrack[count] = 0.0f;
            slewTrack[count] = 0.0f;
            bassTrack[count] = 0.0f;
        }
        for (unsigned long count = 0; count < dataPoints; ++count) //count through all the points in the array
        {
            dataPL[count] = 0.0f;
            dataPR[count] = 0.0f;
            dataSL[count] = 0.0f;
            dataSR[count] = 0.0f;
            dataZL[count] = 0.0f;
            dataZR[count] = 0.0f;
            dataBSL[count] = 0.0f;
            dataBSR[count] = 0.0f;
            dataBRL[count] = 0.0f;
            dataBRR[count] = 0.0f;
            dataBZL[count] = 1000.0f;
            dataBZR[count] = 1000.0f;
            dataDSL[count] = 0.0f;
            dataDSR[count] = 0.0f;
            dataDRL[count] = 0.0f;
            dataDRR[count] = 0.0f;
            dataDZL[count] = 0.0f;
            dataDZR[count] = 0.0f;
            backR[count] = pluginColour.getFloatRed();
            backG[count] = pluginColour.getFloatGreen();
            backB[count] = pluginColour.getFloatBlue();
        }
        repaint(); //and update the UI to immediately see the result
    }
};

}  // namespace ui
