#include <ostream>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"

#include "ns3/stats-module.h"

#include "realtime-apps.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("LltRealtimeApp");

TypeId
RealtimeSender::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("RealtimeSender")
          .SetParent<Application> ()
          .AddConstructor<RealtimeSender> ()
          .AddAttribute ("PacketSize", "The size of packets transmitted.", UintegerValue (64),
                         MakeUintegerAccessor (&RealtimeSender::m_pktSize),
                         MakeUintegerChecker<uint32_t> (1))
          .AddAttribute (
              "Destination", "Target host address.", Ipv4AddressValue ("255.255.255.255"),
              MakeIpv4AddressAccessor (&RealtimeSender::m_destAddr), MakeIpv4AddressChecker ())
          .AddAttribute ("Port", "Destination app port.", UintegerValue (1603),
                         MakeUintegerAccessor (&RealtimeSender::m_destPort),
                         MakeUintegerChecker<uint32_t> ())
          .AddAttribute ("NumPackets", "Total number of packets to send.", UintegerValue (30),
                         MakeUintegerAccessor (&RealtimeSender::m_numPkts),
                         MakeUintegerChecker<uint32_t> (1))
          .AddAttribute ("Interval", "The time to wait between packets", TimeValue (Seconds (1.0)),
                         MakeTimeAccessor (&RealtimeSender::m_interval), MakeTimeChecker ())
          .AddAttribute ("ToS", "ToS byte to be set on outbound packets", UintegerValue (0),
                         MakeUintegerAccessor (&RealtimeSender::m_ipTos),
                         MakeUintegerChecker<uint8_t> ())
          .AddTraceSource ("Tx", "A new packet is created and is sent",
                           MakeTraceSourceAccessor (&RealtimeSender::m_txTrace),
                           "ns3::Packet::TracedCallback");
  return tid;
}

RealtimeSender::RealtimeSender ()
{
  NS_LOG_FUNCTION_NOARGS ();
  m_socket = 0;
}

RealtimeSender::~RealtimeSender ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

void
RealtimeSender::DoDispose (void)
{
  NS_LOG_FUNCTION_NOARGS ();

  m_socket = 0;
  Application::DoDispose ();
}

void
RealtimeSender::StartApplication ()
{
  NS_LOG_FUNCTION_NOARGS ();

  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);

      if (Ipv4Address::IsMatchingType (m_destAddr) == true)
        {
          if (m_socket->Bind () == -1)
            {
              NS_FATAL_ERROR ("Failed to bind socket");
            }
          m_socket->Connect (InetSocketAddress (Ipv4Address::ConvertFrom (m_destAddr), m_destPort));
        }
      else
        {
          NS_ASSERT_MSG (false, "Incompatible address type: " << m_destAddr);
        }
    }

  if (m_ipTos)
    {
      m_socket->SetIpTos (m_ipTos);
    }

  m_count = 0;

  Simulator::Cancel (m_sendEvent);
  m_sendEvent = Simulator::ScheduleNow (&RealtimeSender::SendPacket, this);
}

void
RealtimeSender::StopApplication ()
{
  NS_LOG_FUNCTION_NOARGS ();
  Simulator::Cancel (m_sendEvent);
}

void
RealtimeSender::SendPacket ()
{
  NS_LOG_INFO ("Sending packet at " << Simulator::Now () << " to " << m_destAddr);

  Ptr<Packet> packet = Create<Packet> (m_pktSize);

  TimestampTag timestamp;
  timestamp.SetTimestamp (Simulator::Now ());
  packet->AddByteTag (timestamp);

  if ((m_socket->Send (packet)) >= 0)
    {
      m_count++;
    }
  else
    {
      NS_LOG_INFO ("Error while sending");
    }

  // Report the event to the trace.
  m_txTrace (packet);

  if (m_count < m_numPkts)
    {
      m_sendEvent = Simulator::Schedule (m_interval, &RealtimeSender::SendPacket, this);
    }
}

//------------------------------------------------------
//-- RealtimeReceiver
//------------------------------------------------------
TypeId
RealtimeReceiver::GetTypeId (void)
{
  static TypeId tid = TypeId ("RealtimeReceiver")
                          .SetParent<Application> ()
                          .AddConstructor<RealtimeReceiver> ()
                          .AddAttribute ("Port", "Listening port.", UintegerValue (1603),
                                         MakeUintegerAccessor (&RealtimeReceiver::m_port),
                                         MakeUintegerChecker<uint32_t> ());
  return tid;
}

RealtimeReceiver::RealtimeReceiver () : m_calc (nullptr), m_delay (nullptr)
{
  NS_LOG_FUNCTION_NOARGS ();

  m_socket = 0;
}

RealtimeReceiver::~RealtimeReceiver ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

void
RealtimeReceiver::DoDispose (void)
{
  NS_LOG_FUNCTION_NOARGS ();

  m_socket = 0;
  Application::DoDispose ();
}

void
RealtimeReceiver::StartApplication ()
{
  NS_LOG_FUNCTION_NOARGS ();

  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);

      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_port);
      if (m_socket->Bind (local) == -1)
        {
          NS_FATAL_ERROR ("Failed to bind socket");
        }
    }

  m_socket->SetRecvCallback (MakeCallback (&RealtimeReceiver::Receive, this));
}

void
RealtimeReceiver::StopApplication ()
{
  NS_LOG_FUNCTION_NOARGS ();

  if (m_socket != 0)
    {
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket>> ());
    }
}

void
RealtimeReceiver::SetCounter (Ptr<CounterCalculator<>> calc)
{
  m_calc = calc;
}

void
RealtimeReceiver::SetDelayTracker (Ptr<MinMaxAvgTotalCalculator<int64_t>> delay)
{
  m_delay = delay;
}

void
RealtimeReceiver::Receive (Ptr<Socket> socket)
{
  Ptr<Packet> packet;
  Address from;

  while ((packet = socket->RecvFrom (from)))
    {
      if (InetSocketAddress::IsMatchingType (from))
        {
          NS_LOG_INFO ("Received " << packet->GetSize () << " bytes from "
                                   << InetSocketAddress::ConvertFrom (from).GetIpv4 ());
        }

      TimestampTag timestamp;

      if (packet->FindFirstMatchingByteTag (timestamp))
        {
          Time tx = timestamp.GetTimestamp ();

          if (m_delay != nullptr)
            {
              auto delay = Simulator::Now () - tx;

              NS_LOG_INFO ("Computed delay " << delay);

              m_delay->Update (delay.GetMilliSeconds());
            }
        }

      if (m_calc != nullptr)
        {
          m_calc->Update ();
        }
    }
}

//------------------------------------------------------
//-- TimestampTag
//------------------------------------------------------
TypeId
TimestampTag::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("TimestampTag")
          .SetParent<Tag> ()
          .AddConstructor<TimestampTag> ()
          .AddAttribute ("Timestamp", "Some momentous point in time", EmptyAttributeValue (),
                         MakeTimeAccessor (&TimestampTag::GetTimestamp), MakeTimeChecker ());
  return tid;
}

TypeId
TimestampTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

uint32_t
TimestampTag::GetSerializedSize (void) const
{
  return 8;
}

void
TimestampTag::Serialize (TagBuffer i) const
{
  int64_t t = m_timestamp.GetNanoSeconds ();
  i.Write ((const uint8_t *) &t, 8);
}

void
TimestampTag::Deserialize (TagBuffer i)
{
  int64_t t;
  i.Read ((uint8_t *) &t, 8);
  m_timestamp = NanoSeconds (t);
}

void
TimestampTag::SetTimestamp (Time time)
{
  m_timestamp = time;
}

Time
TimestampTag::GetTimestamp (void) const
{
  return m_timestamp;
}

void
TimestampTag::Print (std::ostream &os) const
{
  os << "t=" << m_timestamp;
}
