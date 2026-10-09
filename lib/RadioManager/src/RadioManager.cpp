#include "RadioManager.h"
#include <math.h>
namespace radio {
bool RadioManager::valid(const Config& c) {
 const uint32_t widths[] = {7800,10400,15600,20800,31250,41700,62500,125000,250000,500000};
 bool bw = false; for (auto w : widths) if(c.bandwidth == w) bw = true;
 return c.frequency >= 902000000UL && c.frequency <= 928000000UL && bw && c.spreadingFactor >= 7 && c.spreadingFactor <= 12 && c.codingRate >= 5 && c.codingRate <= 8 && c.power >= 2 && c.power <= 20 && c.preamble >= 6;
}
uint32_t RadioManager::airtimeMs(const Config& c, size_t n) {
 const double symbol = double(1UL << c.spreadingFactor) / c.bandwidth;
 const int de = symbol > 0.016 ? 1 : 0;
 const double numerator = 8.0*n - 4*c.spreadingFactor + 28 + (c.crc ? 16 : 0);
 const double blocks = ceil(numerator / (4*(c.spreadingFactor-2*de)));
 const double payload = 8 + (blocks > 0 ? blocks*c.codingRate : 0);
 return uint32_t(ceil((c.preamble + 4.25 + payload)*symbol*1000));
}
Status RadioManager::ready() const {
 if(state_ == State::Uninitialized) return Status::NotInitialized;
 if(state_ == State::Fault) return Status::HardwareError;
 if(state_ == State::Transmitting) return Status::Busy;
 return Status::Ok;
}
Status RadioManager::fault(Status s) { adapter_.idle(); state_=State::Fault; tx_=TxStatus::Failed; return s; }
Status RadioManager::initialize(const Config& c) {
 if(state_ == State::Transmitting) return Status::Busy;
 if(!valid(c)) return Status::InvalidConfig;
 if(!adapter_.begin(c)) return fault(Status::HardwareError);
 config_=c; state_=State::Idle; tx_=TxStatus::None; return Status::Ok;
}
Status RadioManager::configure(const Config& c) {
 auto s=ready(); if(s!=Status::Ok) return s;
 if(!valid(c)) return Status::InvalidConfig;
 bool rx=state_==State::Receiving;
 if(!adapter_.idle() || !adapter_.configure(c) || (rx && !adapter_.receive())) return fault(Status::HardwareError);
 config_=c; return Status::Ok;
}
Status RadioManager::idle() { auto s=ready(); if(s!=Status::Ok) return s; if(!adapter_.idle()) return fault(Status::HardwareError); state_=State::Idle; return Status::Ok; }
Status RadioManager::receive() { auto s=ready(); if(s!=Status::Ok) return s; if(!adapter_.receive()) return fault(Status::HardwareError); state_=State::Receiving; return Status::Ok; }
Status RadioManager::transmit(const uint8_t* p,size_t n,uint32_t now) {
 auto s=ready(); if(s!=Status::Ok) return s;
 if(!p || !n || n>MaxPayload) return Status::InvalidPayload;
 if(!adapter_.transmit(p,n)) return fault(Status::HardwareError);
 started_=now; timeout_=airtimeMs(config_,n)*2+1000; state_=State::Transmitting; tx_=TxStatus::InProgress; return Status::Ok;
}
Status RadioManager::poll(uint32_t now) {
 if(state_!=State::Transmitting) return state_==State::Fault ? Status::HardwareError : Status::Ok;
 auto t=adapter_.pollTx();
 if(t==TxStatus::Failed) return fault(Status::HardwareError);
 if(t==TxStatus::Complete) { if(!adapter_.idle()) return fault(Status::HardwareError); state_=State::Idle; tx_=t; return Status::Ok; }
 if(uint32_t(now-started_)>=timeout_) return fault(Status::Timeout);
 return Status::Ok;
}
Status RadioManager::read(uint8_t* p,size_t n,PacketInfo& info) {
 auto s=ready(); if(s!=Status::Ok) return s;
 if(state_!=State::Receiving) return Status::NoPacket;
 if(!p) return Status::InvalidPayload;
 s=adapter_.read(p,n,info); if(s==Status::HardwareError) return fault(s); return s;
}
const char* statusName(Status s) {
 switch(s) {
#define NAME(x) case Status::x: return #x;
 NAME(Ok) NAME(NotInitialized) NAME(InvalidConfig) NAME(InvalidPayload) NAME(Busy) NAME(NoPacket) NAME(BufferTooSmall) NAME(HardwareError) NAME(Timeout)
#undef NAME
 } return "Unknown";
}
}
