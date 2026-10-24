#pragma once
#include "LinkProfiles.h"
#include <PacketService.h>
#include <StatisticsManager.h>

class LinkManager : public protocol::LinkReceiver {
public:
 enum class ProbeReason : uint8_t { None, Startup, Periodic, Evidence };
 enum class Role : uint8_t { Balloon, Ground };
 enum class Phase : uint8_t { Stopped, Searching, Data, Beacon, Control, Fault };
 enum class Conversation : uint8_t { Idle, SendSyn, WaitSynAck, SendSynAck, WaitAck, SendAck, Complete, Provisional, Exhausted };
 struct Config {
  uint32_t dataWindowMs = 30000;
  uint8_t controlWindowSeconds = 10;
  uint16_t quietMs = 50, turnaroundMs = 50;
  uint16_t beaconWaitMs = 500, replyTimeoutMs = 4000;
  // Defaults are starting policies, not measured RF tuning.
  bool autoProbe = true;
  uint8_t minimumProbePackets = 4;
  uint8_t maximumProbeNegotiationWindows = 3;
  uint8_t silentDataWindowsBeforeFallback = 3;
  uint32_t upwardProbeIntervalMs = 16UL*60*1000;
  float maximumCorruptionRate = 0.25f, minimumGoodputImprovement = 0.05f;
  float maximumGoodputDrop = 0.25f;
  uint8_t degradedWindowsBeforeFallback = 3;
  float nearPeakFraction = 0.90f;
  uint8_t strongWindowsForUpgrade = 3;
  uint32_t evidenceRetryCooldownMs = 2UL*60*1000;
  float evidenceGoodputImprovement = 0.10f, evidenceSnrImprovementDb = 3.0f;
  uint16_t earlyRetryThresholdMs = 100, earlyRetryDelayMs = 100;
 };
 // Availability must remain stable until the conversation commits or aborts.
 // Null handler refuses nonempty retransmission requests; no fictional data cache.
 class RetransmissionHandler {
 public:
  virtual ~RetransmissionHandler() = default;
  virtual bool canRetransmit(const protocol::ControlPacket&) = 0;
  virtual void retransmit(const protocol::ControlPacket&) = 0;
  virtual void discard(const protocol::ControlPacket&) {} // Release proposal reservations, not messages.
 };
 LinkManager(PacketService& protocol, StatisticsManager& statistics)
  : protocol_(protocol), statistics_(statistics) {}
 void begin(); // Register callbacks, remain stopped until start().
 protocol::Result start(Role role, const Config&, uint32_t nowMs);
 void stop();
 void poll(uint32_t nowMs); // Call regularly alongside ProtocolManager::poll().
 uint32_t phaseStartedAtMs() const { return phaseStart_; }
 uint32_t phaseEndsAtMs() const { return phaseEnd_; }
 Phase phase() const { return phase_; }
 Conversation conversation() const { return conversation_; }
 bool established() const { return established_; }
 ProbeReason probeReason() const { return probing_ ? probeReason_ : ProbeReason::None; }
 float peakAirtimeGoodput(link_profiles::Id id) const {
  return statistics_.peakAirtimeGoodput(uint8_t(id));
 }
 bool probing() const { return probing_; }
 bool negotiatingProbe() const { return probing_ && probeNegotiating_; }
 uint8_t degradedDataWindows() const { return degradedWindows_; }
 uint8_t silentDataWindows() const { return silentDataWindows_; }
 uint8_t failedProbeWindows() const { return failedProbeWindows_; }
 bool acquiring() const { return acquisition_; }
 bool pendingAgreement() const { return pending_; }
 bool retransmissionAccepted() const { return lastRetransmitAccepted_; }
 bool retransmissionRefused() const {
  return lastRetransmitRefused_;
 }
 uint8_t conversationId() const { return conversationId_; }
 uint8_t missedControlWindows() const { return missed_; }
 link_profiles::Id dataProfile() const { return dataProfile_; }
 protocol::Result lastResult() const { return lastResult_; }
 // Ground-only configuration of the next proposal; frozen for an active attempt.
 protocol::Result requestDataProfile(link_profiles::Id);
 protocol::Result requestRetransmission(uint16_t message, uint16_t groups, const uint16_t* maps, uint8_t count);
 bool setRetransmissionHandler(RetransmissionHandler* handler) {
  if(pending_) return false;
  retransmission_=handler; return true;
 }
 StatisticsManager::Snapshot statistics() const { return statistics_.snapshot(); }
 StatisticsManager::ProfileSnapshot profileStatistics(link_profiles::Id id) const {
  return statistics_.profile(static_cast<uint8_t>(id));
 }
 bool recentProfileStatistics(link_profiles::Id id, StatisticsManager::RecentSnapshot& out) const {
  return statistics_.recentProfile(static_cast<uint8_t>(id),out);
 }
 // Explicit low-level local setting; automatic operation uses dataProfile separately.
 radio::Status setProfile(link_profiles::Id);
 protocol::Result sendControl(const protocol::ControlPacket& p, uint32_t nowMs);
 protocol::Result sendBeacon(const protocol::BeaconPacket& p, uint32_t nowMs);
 protocol::Result onControl(const protocol::ControlPacket&, const radio::PacketInfo&) override;
 protocol::Result onBeacon(const protocol::BeaconPacket&, const radio::PacketInfo&) override;
private:
 bool withinWindow(uint32_t now) const;
 bool p0(const radio::PacketInfo&) const;
 bool busyConversation() const;
 void enterData(uint32_t start);
 void enterWindow(uint32_t start);
 void finishWindow(uint32_t boundary);
 void discardPending();
 void abortAttempt(uint32_t now);
 void commit(bool provisional);
 void startAttempt(uint32_t now);
 void chooseProfile(uint32_t now);
 void recordFailedUpgrade(uint32_t now);
 void fail(protocol::Result result);
 void baseline();
 bool sameRequest(const protocol::ControlPacket&, const protocol::ControlPacket&) const;
 PacketService& protocol_;
 StatisticsManager& statistics_;
 Config config_;
 Role role_ = Role::Balloon;
 Phase phase_ = Phase::Stopped;
 Conversation conversation_ = Conversation::Idle;
 link_profiles::Id dataProfile_ = link_profiles::Id::P0, desiredProfile_ = link_profiles::Id::P0;
 bool acquisition_ = true, nextBeacon_ = true, established_ = false;
 bool pending_ = false, heard_ = false, sending_ = false, committed_ = false;
 bool automatic_ = true, initialProbe_ = true, probing_ = false;
 bool probeNegotiating_ = false, silenceRecovery_ = false;
 uint8_t silentDataWindows_ = 0, degradedWindows_ = 0;
 uint8_t degradationTarget_ = StatisticsManager::UnknownProfile;
 uint32_t degradationInstanceId_ = 0;
 uint8_t failedProbeWindows_ = 0;
 link_profiles::Id probeTarget_ = link_profiles::Id::P0, probeFrom_ = link_profiles::Id::P0;
 ProbeReason probeReason_ = ProbeReason::None;
 struct ProfileHistory {
  float attemptedGoodput=0, attemptedSnr=0;
  uint32_t retryAt=0;
  uint8_t strongWindows=0;
  bool failedUpgrade=false;
 } profileHistory_[5];
 bool retransmissionAccepted_ = false, lastRetransmitAccepted_ = false, lastRetransmitRefused_ = false;
 uint8_t conversationId_ = 0, missed_ = 0;
 uint32_t phaseStart_ = 0, phaseEnd_ = 0, actionAt_ = 0, replyAt_ = 0;
 int16_t nextSlideMs_ = 0;
 struct Baseline { uint32_t packets=0, unavailable=0; } baseline_;
 uint32_t nextProbeAt_ = 0, profileSinceMs_ = 0, lastPollMs_ = 0;
 uint32_t trialPackets_ = 0;
 float bestGoodput_ = 0;
 link_profiles::Id bestProfile_ = link_profiles::Id::P0;
 protocol::ControlPacket request_, reply_;
 protocol::Result lastResult_ = protocol::Result::Ok;
 RetransmissionHandler* retransmission_ = nullptr;
};
