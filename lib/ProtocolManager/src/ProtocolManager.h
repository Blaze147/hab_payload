#pragma once
#include "PacketService.h"
class ProtocolManager : public PacketService {
public:
 ProtocolManager(radio::RadioManager& radio, packet_statistics::Sink& statistics)
  : radio_(radio), statistics_(statistics) {}
 void begin(); // Disconnect receivers; connect them after begin().
 // Local MCU construction arms this automatically. A host MCU can also arm it.
 void markRestart() { restartPending_=true; }
 bool lastTransmissionRestarted() const { return lastTxRestarted_; }
 void setDataReceiver(protocol::DataReceiver* receiver) { dataReceiver_=receiver; }
 void setLinkReceiver(protocol::LinkReceiver* receiver) { linkReceiver_=receiver; }
 radio::Status poll(uint32_t nowMs);
 radio::Status receive() { return radio_.receive(); }
 radio::Status idle() { return radio_.idle(); }
 radio::Status configureRadio(const radio::Config& config) { return radio_.configure(config); }
 const radio::Config& radioConfig() const { return radio_.config(); }
 radio::State radioState() const { return radio_.state(); }
 radio::TxStatus transmission() const { return radio_.transmission(); }
 uint32_t airtimeMs(size_t bytes) const { return radio::RadioManager::airtimeMs(radio_.config(),bytes); }
 void allowDataTransmission(bool allow) { dataAllowed_=allow; dataDeadline_=false; }
 void setDataWindow(uint32_t endMs) { dataAllowed_=true; dataDeadline_=true; dataEndMs_=endMs; }
 protocol::Result sendData(const protocol::DataPacket&, uint32_t nowMs);
 // Borrow payload for the duration of this call; avoids a full Packet on small stacks.
 protocol::Result sendData(const protocol::Header&, const uint8_t* payload, uint32_t nowMs);
 protocol::Result sendControl(const protocol::ControlPacket&, uint32_t nowMs);
 protocol::Result sendBeacon(const protocol::BeaconPacket&, uint32_t nowMs);
 // Raw ingress checks CRC, decodes fields, records statistics, then routes.
 protocol::Result receiveRaw(const uint8_t*, size_t length, const radio::PacketInfo&);
 // Trusted typed ingress retained for tests/integration: CRC is not checked.
 // These methods validate structure, record RX metadata, then route synchronously.
 protocol::Result acceptDecoded(const protocol::DataPacket&, const radio::PacketInfo&);
 protocol::Result acceptDecoded(const protocol::ControlPacket&, const radio::PacketInfo&);
 protocol::Result acceptDecoded(const protocol::BeaconPacket&, const radio::PacketInfo&);
private:
 radio::RadioManager& radio_;
 packet_statistics::Sink& statistics_;
 protocol::DataReceiver* dataReceiver_ = nullptr;
 protocol::LinkReceiver* linkReceiver_ = nullptr;
 bool callbackActive_ = false, dataAllowed_ = true, dataDeadline_ = false;
 uint32_t dataEndMs_ = 0;
 bool restartPending_=true,restartInFlight_=false,lastTxRestarted_=false;
 void settleRestart();
 protocol::Result transmit(uint8_t*,size_t,uint32_t);
 protocol::Fragment fragment_; // Single reusable RX workspace, no queued copies.
 packet_statistics::Observation observation(const radio::PacketInfo&) const;
 protocol::Result deliverData(const protocol::Header&, const radio::PacketInfo&,
                              packet_statistics::Integrity);

};
