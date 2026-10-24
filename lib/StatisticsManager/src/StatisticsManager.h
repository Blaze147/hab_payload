#pragma once
#include <stdint.h>
#include <stddef.h>
#include "StatisticsEvents.h"

class StatisticsManager : public packet_statistics::Sink {
public:
 enum class Mode : uint8_t { Minimal, Heavy };
 using Integrity = packet_statistics::Integrity;
 using Observation = packet_statistics::Observation;
 static constexpr uint8_t ProfileCount = 5;
 static constexpr uint8_t UnknownProfile = ProfileCount;
 static constexpr uint8_t BucketCount = ProfileCount + 1;
 static constexpr uint16_t FragmentCapacity = 256;
 static constexpr uint8_t TrackingCapacity = 2;
 struct ProfileSnapshot {
  uint32_t packets = 0, corruptPackets = 0, malformedPackets = 0, crcCheckedPackets = 0;
  uint32_t wireBytes = 0, uniquePayloadBytes = 0;
  uint32_t dataPackets = 0;
  uint32_t uniqueFragments = 0, duplicateFragments = 0, retryPackets = 0;
  uint32_t pendingMissing = 0, trackingUnavailable = 0;
  uint32_t firstAtMs = 0, lastAtMs = 0;
  int16_t lastRssi = 0, minRssi = 0;
  float lastSnr = 0, meanRssi = 0, meanSnr = 0, minSnr = 0;
  bool observed = false;
  // Fractions, 0..1. No observations yields zero, not evidence of a good link.
  float corruptionRate() const;
  float provisionalMissingRate() const;
  // Receive-observation span, not PHY bitrate. Explicit now allows idle decay.
  float throughputBytesPerSecond(uint32_t nowMs) const;
  float goodputBytesPerSecond(uint32_t nowMs) const;
 };
 static constexpr uint8_t RecentPacketCount = 4;
 struct RecentSample {
  Observation observation;
  uint16_t uniquePayloadBytes = 0;
  bool duplicate = false, trackingUnavailable = false;
 };
 // Optional external storage: four samples per profile, including unknown.
 static constexpr uint8_t AirtimeSampleCount = 16;
 struct AirtimeSample {
  uint32_t receivedAtMs=0, airtimeMs=0;
  uint16_t uniquePayloadBytes=0;
  bool corrupt=false, trackingUnavailable=false;
  float snr=0;
 };
 struct AirtimeSnapshot {
  uint8_t packets=0, corruptPackets=0, trackingUnavailable=0;
  uint32_t airtimeMs=0, uniquePayloadBytes=0, firstAtMs=0;
  float meanSnr=0;
  // Both rates use the same retained data/error samples. Wall time begins
  // at the estimated start of the oldest attempt and includes subsequent idle.
  float wallGoodputBytesPerSecond(uint32_t nowMs) const {
   const uint32_t elapsed=nowMs-firstAtMs;
   return packets && elapsed ? float(uniquePayloadBytes)*1000/elapsed : 0;
  }
  // Observed-attempt estimate: completely unheard transmissions are unavailable.
  float airtimeGoodputBytesPerSecond() const {
   return airtimeMs ? float(uniquePayloadBytes)*1000/airtimeMs : 0;
  }
  float goodputBytesPerSecond() const { return airtimeGoodputBytesPerSecond(); }
 };
 struct Summary {
  ProfileSnapshot packets;
  uint64_t airtimeMs=0;
  uint32_t wallMs=0, dataWindows=0;
  float wallGoodputBytesPerSecond() const {
   return wallMs ? float(packets.uniquePayloadBytes)*1000/wallMs : 0;
  }
  float airtimeGoodputBytesPerSecond() const {
   return airtimeMs ? float(packets.uniquePayloadBytes)*1000/airtimeMs : 0;
  }
 };
 struct PacketRecord {
  uint32_t startedAtMs=0, endedAtMs=0;
  Summary summary;
 };
 struct ProfileRecord {
  uint32_t instanceId=0, startedAtMs=0, endedAtMs=0;
  uint8_t profile=UnknownProfile, sampleCount=0, sampleNext=0;
  bool active=false;
  float peakAirtimeGoodput=0;
  Summary total;
  PacketRecord samples[RecentPacketCount];
  Summary whole(uint32_t nowMs) const {
   auto result=total;
   result.wallMs=(active ? nowMs : endedAtMs)-startedAtMs;
   return result;
  }
  Summary recent(uint32_t nowMs) const;
 };
 struct DenseStorage {
  // Newest/current record first, preceding record second; no older visits.
  ProfileRecord records[ProfileCount][2];
  uint8_t activeProfile=UnknownProfile;
  uint32_t nextInstanceId=0, windowDeadline=0;
  bool windowOpen=false;
  uint32_t windowStartedAt=0;
  AirtimeSample airtime[BucketCount][AirtimeSampleCount];
  uint8_t airtimeCount[BucketCount]={}, airtimeNext[BucketCount]={};
  RecentSample samples[BucketCount][RecentPacketCount];
  uint8_t count[BucketCount] = {}, next[BucketCount] = {};
 };
 struct RecentSnapshot {
  uint8_t packets = 0, corruptPackets = 0, crcCheckedPackets = 0, malformedPackets = 0;
  uint8_t duplicatePackets = 0, retryPackets = 0, trackingUnavailable = 0;
  uint32_t wireBytes = 0, uniquePayloadBytes = 0;
  uint32_t firstAtMs = 0, lastAtMs = 0;
  float meanRssi = 0, meanSnr = 0, minSnr = 0;
  int16_t minRssi = 0;
  float corruptionRate() const;
  float throughputBytesPerSecond(uint32_t nowMs) const;
  float goodputBytesPerSecond(uint32_t nowMs) const;
 };
 struct Snapshot {
  uint32_t receivedPackets = 0, rejectedPackets = 0;
  uint32_t completedMessages = 0, rejectedMessages = 0, radioErrors = 0;
  bool hasReceiveMeasurement = false;
  int16_t lastRssi = 0;
  float lastSnr = 0;
  size_t lastPacketLength = 0;
 };
 void begin(); // Reset counters and trackers; preserve selected history storage/mode.
 // Caller owns supplied storage and must keep it alive. Heavy requires DenseStorage;
 // a global history ring is optional. Minimal stores neither. No heap allocation.
 bool setLogging(Mode mode, Observation* history = nullptr, size_t capacity = 0,
                 DenseStorage* dense = nullptr);
 bool setDenseLogging(DenseStorage& storage) { return setLogging(Mode::Heavy,nullptr,0,&storage); }
 // False if dense storage is not enabled; empty profiles return an empty summary.
 // Record pointers remain valid until that profile is visited again or reset.
 const ProfileRecord* profileRecord(uint8_t id, uint8_t age=0) const;
 void recordReliableGoodput(uint8_t id, float score);
 float peakAirtimeGoodput(uint8_t id) const;
 void beginDataWindow(uint8_t profile, uint32_t startMs, uint32_t endMs);
 void endDataWindow(uint32_t endMs);
 void endProfileInstance(uint32_t endMs);
 bool recentProfile(uint8_t id, RecentSnapshot& out) const;
 bool recentAirtimeProfile(uint8_t id, uint32_t sinceMs, AirtimeSnapshot& out) const;
 bool recentPacket(uint8_t id, size_t oldestFirstIndex, RecentSample& out) const;
 Mode mode() const { return mode_; }
 size_t historySize() const { return historySize_; }
 bool history(size_t oldestFirstIndex, Observation& out) const;
 void observe(const Observation&);
 ProfileSnapshot profile(uint8_t id) const;
 // Low-level decoder/radio diagnostics, including control traffic outside records.
 ProfileSnapshot packetDiagnostics(uint8_t id) const {
  return profiles_[id<ProfileCount ? id : UnknownProfile];
 }
 // Explicitly release a tracked message. Clears provisional gaps without claiming
 // loss. Late packets after release cannot be deduplicated against that message.
 bool releaseMessage(uint16_t sessionId, uint16_t messageId);
 bool pendingFragment(uint16_t sessionId, uint16_t messageId, uint16_t index) const;
 void recordPacketRejected();
 void recordMessageCompleted();
 void recordMessageRejected();
 void recordRadioError();
 Snapshot snapshot() const { return data_; }
private:
 struct Tracker {
  bool used = false;
  uint16_t session = 0, message = 0, count = 0, high = 0, received = 0;
  uint8_t seen[32] = {};
  // Two 4-bit owners per byte: 0=no gap, 1..6=profile bucket+1.
  uint8_t gaps[128] = {};
 };
 bool seen(const Tracker&, uint16_t index) const;
 uint8_t gap(const Tracker&, uint16_t index) const;
 void setGap(Tracker&, uint16_t index, uint8_t owner);
 void clearTracker(Tracker&);
 void track(const Observation&, uint8_t bucket);
 void recordObservation(const Observation&, uint32_t usefulBytes, bool duplicate,
                        bool unavailable, bool uniqueFragment);
 Snapshot data_;
 ProfileSnapshot profiles_[BucketCount];
 Tracker trackers_[TrackingCapacity];
 Mode mode_ = Mode::Minimal;
 DenseStorage* dense_ = nullptr;
 Observation* history_ = nullptr;
 size_t historyCapacity_ = 0, historySize_ = 0, historyNext_ = 0;
};
