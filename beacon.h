#ifndef BEACON_H
#define BEACON_H

// Pake kata 'extern' biar si GitHub tau kita cuma 'minjem' variabel dari sniffer.h
extern char emptySSID[32];
extern uint8_t channelIndex;
extern uint8_t macAddr[6];
extern uint8_t wifi_channel;
extern uint32_t currentTime;
extern uint32_t packetSize;
extern uint32_t packetCounter;
extern uint32_t attackTime;
extern uint32_t packetRateTime;
extern bool beacon_active;
extern uint8_t beaconPacket[109];

#endif
