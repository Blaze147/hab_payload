#pragma once
#include "ProtocolTypes.h"
// Commands complete locally before returning; TX completion is a separate state.
class PacketService {
public:
 virtual ~PacketService() = default;
 virtual void setDataReceiver(protocol::DataReceiver*) = 0;
 virtual void setLinkReceiver(protocol::LinkReceiver*) = 0;
 virtual radio::Status receive() = 0;
 virtual radio::Status idle() = 0;
 virtual radio::Status configureRadio(const radio::Config&) = 0;
 virtual const radio::Config& radioConfig() const = 0;
 virtual radio::State radioState() const = 0;
 virtual radio::TxStatus transmission() const = 0;
 virtual uint32_t airtimeMs(size_t) const = 0;
 virtual void allowDataTransmission(bool) = 0;
 virtual void setDataWindow(uint32_t) = 0;
 virtual protocol::Result sendData(const protocol::Header&, const uint8_t*, uint32_t) = 0;
 protocol::Result sendData(const protocol::DataPacket& p,uint32_t now) { return sendData(p.header,p.payload,now); }
 virtual protocol::Result sendControl(const protocol::ControlPacket&,uint32_t) = 0;
 virtual protocol::Result sendBeacon(const protocol::BeaconPacket&,uint32_t) = 0;
};
