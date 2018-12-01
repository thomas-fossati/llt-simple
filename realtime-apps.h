#ifndef REALTIME_APPS_H
#define REALTIME_APPS_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/application.h"

#include "ns3/stats-module.h"

using namespace ns3;

//------------------------------------------------------
// RealtimeSender
//------------------------------------------------------
class RealtimeSender : public Application
{
public:
  static TypeId GetTypeId (void);
  RealtimeSender ();
  virtual ~RealtimeSender ();

protected:
  virtual void DoDispose (void);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void SendPacket ();

  uint32_t m_pktSize;
  Ipv4Address m_destAddr;
  uint32_t m_destPort;
  Time m_interval;
  uint32_t m_numPkts;

  Ptr<Socket> m_socket;
  uint8_t m_ipTos;
  EventId m_sendEvent;

  TracedCallback<Ptr<const Packet>> m_txTrace;

  uint32_t m_count;
};

//------------------------------------------------------
// RealtimeReceiver
//------------------------------------------------------
class RealtimeReceiver : public Application
{
public:
  static TypeId GetTypeId (void);
  RealtimeReceiver ();
  virtual ~RealtimeReceiver ();

  void SetCounter (Ptr<CounterCalculator<>> calc);
  void SetDelayTracker (Ptr<MinMaxAvgTotalCalculator<int64_t>> delay);

protected:
  virtual void DoDispose (void);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void Receive (Ptr<Socket> socket);

  Ptr<Socket> m_socket;

  uint32_t m_port;

  Ptr<CounterCalculator<>> m_calc;
  Ptr<MinMaxAvgTotalCalculator<int64_t>> m_delay;
};

//------------------------------------------------------
class TimestampTag : public Tag
{
public:
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;

  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (TagBuffer i) const;
  virtual void Deserialize (TagBuffer i);

  // these are our accessors to our tag structure
  void SetTimestamp (Time time);
  Time GetTimestamp (void) const;

  void Print (std::ostream &os) const;

private:
  Time m_timestamp;
};

#endif  // REALTIME_APPS_H
