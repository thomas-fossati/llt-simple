// See README.md

#include "ns3/applications-module.h"
#include "ns3/config-store.h"
#include "ns3/core-module.h"
#include "ns3/epc-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/lte-helper.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-helper.h"
#include "realtime-apps.h"

using namespace ns3;

enum {
  LLT_LOW_LATENCY = 20, // 000101 (00)
};

NS_LOG_COMPONENT_DEFINE ("LLTSimple");

int
main (int argc, char *argv[])
{
  bool markingEnabled = true;
  bool videoExperiment = false;

  std::string runId = "run-" + std::to_string (time (NULL));
  std::string experiment ("loss latency tradeoff");
  std::string strategy ("single-ue");
  std::string input;

  // Set up command line parameters used to control the experiment.
  CommandLine cmd;
  cmd.AddValue ("experiment", "study of which this trial is a member", experiment);
  cmd.AddValue ("strategy", "code or parameters being examined in this trial", strategy);
  cmd.AddValue ("run", "unique identifier for this trial for identification in later analysis",
                runId);
  cmd.AddValue ("marking-enabled", "whether the LLT marking is enabled on the real-time flow(s).",
                markingEnabled);
  cmd.AddValue ("video", "Whether we do an audio(def) or a video experiment.",
                videoExperiment);
  cmd.Parse (argc, argv);

  // Configure FDD SISO (transmission mode 0) with 6 RBs, i.e. a peak downlink
  // of 4.4Mbps (see table in NOTES.md for dynamic peak BW lookup)
  // (empirically measures show 4.26Mbps is a slightly more faithful figure)
  Config::SetDefault ("ns3::LteEnbRrc::DefaultTransmissionMode", UintegerValue (0));
  Config::SetDefault ("ns3::LteEnbNetDevice::DlBandwidth", UintegerValue (6));
  Config::SetDefault ("ns3::LteEnbNetDevice::UlBandwidth", UintegerValue (6));

  // This will instantiate some common objects (e.g., the Channel object) and
  // provide the methods to add eNBs and UEs and configure them.
  auto lteHelper = CreateObject<LteHelper> ();

  // Create EPC entities (PGW & friends) and a point-to-point network topology
  // Also tell the LTE helper that the EPC will be used
  auto epcHelper = CreateObject<PointToPointEpcHelper> ();
  lteHelper->SetEpcHelper (epcHelper);

  // Create Node object for the eNodeB
  // (NOTE that the Node instances at this point still don’t have an LTE
  // protocol stack installed; they’re just empty nodes.)
  NodeContainer eNB;
  eNB.Create (1);

  // Create Node object for the UEs
  NodeContainer UE;
  UE.Create (2);

  // Configure the Mobility model for all the nodes.  The above will place all
  // nodes at the coordinates (0,0,0).  Refer to the documentation of the ns-3
  // mobility model for how to set a different position or configure node
  // movement.
  MobilityHelper mobility;

  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (eNB);

  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (UE);

  // Install an LTE protocol stack on the eNB
  auto eNBDevice = lteHelper->InstallEnbDevice (eNB);

  // Install an LTE protocol stack on the UEs
  auto UEDevice = lteHelper->InstallUeDevice (UE);

  // Install the IP protocol stack on the UE
  InternetStackHelper IPStack;
  IPStack.Install (UE);

  auto UEIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (UEDevice.Get (0)));
  // Set the default gateway for the UE
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  auto ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (UE.Get (0)->GetObject<Ipv4> ());
  ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);

  // Attach the UE to an eNB. This will configure the UE according to the eNB
  // settings, and create an RRC connection between them.
  // A side-effect of this call is to activate the default bearer.
  lteHelper->Attach (UEDevice, eNBDevice.Get (0));

  // Add a dedicated low-latency bearer for applications marking
  // their traffic with the LLT PHB codepoint for low-latency traffic
  // https://tools.ietf.org/html/draft-you-tsvwg-latency-loss-tradeoff-00#section-4.4
  auto tft = Create<EpcTft> ();
  EpcTft::PacketFilter pf;
  pf.typeOfService = LLT_LOW_LATENCY;
  pf.typeOfServiceMask = LLT_LOW_LATENCY;
  tft->Add (pf);
  lteHelper->ActivateDedicatedEpsBearer (UEDevice, EpsBearer (EpsBearer::NGBR_VOICE_VIDEO_GAMING),
                                         tft);

  // Create an application server in the SGi-LAN
  NodeContainer appServer;
  appServer.Create (1);
  IPStack.Install (appServer);

  // Create the SGiLAN as a point-to-point topology between the PGW and the
  // application server
  // - capacity: 10Gb/s
  // - MTU: 1500 bytes
  // - propagation delay: 1ms
  PointToPointHelper SGiLAN;
  SGiLAN.SetDeviceAttribute ("Mtu", UintegerValue (1500));
  SGiLAN.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("10Gbps")));
  SGiLAN.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (1)));

  auto SGiLANDevices = SGiLAN.Install (epcHelper->GetPgwNode (), appServer.Get (0));

  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase ("1.0.0.0", "255.0.0.0");
  auto SGiLANIpIfaces = ipv4h.Assign (SGiLANDevices);
  // interface 0 is localhost, 1 is the point-to-point device
  // Ipv4Address appServerAddr = SGiLANIpIfaces.GetAddress(1);

  auto appServerStaticRouting =
      ipv4RoutingHelper.GetStaticRouting (appServer.Get (0)->GetObject<Ipv4> ());
  appServerStaticRouting->AddNetworkRouteTo (epcHelper->GetUeDefaultGatewayAddress (),
                                             Ipv4Mask ("255.255.0.0"), 1);

  //
  // Realtime sender (server in the SGi)
  //
  // Application simulating realtime downlink communication using a Realtime
  // sender/receiver pair.  If instructed to do so, sender can mark all
  // outgoing packets with the LLT PHB codepoint for low-latency which is
  // mapped to a dedicated EPS bearer with low-latency QCI on LTE segment.
  auto appSource = appServer.Get (0);
  auto sender = CreateObject<RealtimeSender> ();
  appSource->AddApplication (sender);
  sender->SetStartTime (Seconds (2));

  // simulate downstream real-time audio, specifically:
  // 64kbps PCM audio packetized in 20ms increments
  // IP/UDP/RTP/PCM 20+8+12+160=200, required BW is
  // 200 / 0.02 bytes/second = 80kbps
  Config::Set ("/NodeList/*/ApplicationList/*/$RealtimeSender/Destination",
               Ipv4AddressValue (UEIpIface.GetAddress (0)));
  uint16_t dlRtPort = 1234;
  Config::Set ("/NodeList/*/ApplicationList/*/$RealtimeSender/Port",
			   UintegerValue (dlRtPort));


  if (videoExperiment)
  {
	  Config::Set ("/NodeList/*/ApplicationList/*/$RealtimeSender/PacketSize",
				   UintegerValue (930));
	  // - packet rate: 10ms (100 pps)
	  Config::Set ("/NodeList/*/ApplicationList/*/$RealtimeSender/Interval",
				   TimeValue (MilliSeconds (10)));
	  // Send for 10s at most
	  Config::Set ("/NodeList/*/ApplicationList/*/$RealtimeSender/NumPackets",
				   UintegerValue (10 * 100));
  } else {
	  // audio
	  // - packet size: 160 PCM + 12 RTP
	  Config::Set ("/NodeList/*/ApplicationList/*/$RealtimeSender/PacketSize",
				   UintegerValue (172));
	  // - packet rate: 20ms (50 pps)
	  Config::Set ("/NodeList/*/ApplicationList/*/$RealtimeSender/Interval",
				   TimeValue (MilliSeconds (20)));
	  // Send for 10s at most
	  Config::Set ("/NodeList/*/ApplicationList/*/$RealtimeSender/NumPackets",
				   UintegerValue (10 * 50));
  }

  if (markingEnabled)
  {
      Config::Set ("/NodeList/*/ApplicationList/*/$RealtimeSender/ToS",
                   UintegerValue (LLT_LOW_LATENCY));
  }

  //
  // Realtime receiver (UE)
  //
  auto appSink = UE.Get (0);
  auto receiver = CreateObject<RealtimeReceiver> ();
  appSink->AddApplication (receiver);
  receiver->SetStartTime (Seconds (0));
  Config::Set ("/NodeList/*/ApplicationList/*/$RealtimeReceiver/Port", UintegerValue (dlRtPort));

  //
  // Stats collection on the realtime app
  //
  // Create a DataCollector object to hold information about this run.
  DataCollector data;
  data.DescribeRun (experiment, strategy, input, runId);
  // Add any information we wish to record about this run.
  data.AddMetadata ("LLT", uint32_t (markingEnabled));
  // use millisec granularity (See RealtimeReceiver classe)
  auto rtAppDelayStat = CreateObject<MinMaxAvgTotalCalculator<int64_t>> ();
  rtAppDelayStat->SetKey ("real time app delay (ms)");
  receiver->SetDelayTracker (rtAppDelayStat);
  data.AddDataCalculator (rtAppDelayStat);

  // Jitter (paag)
  auto rtAppJitterStat = CreateObject<MinMaxAvgTotalCalculator<int64_t>> ();
  rtAppJitterStat->SetKey ("real time app jitter (ms)");
  receiver->SetJitterTracker (rtAppJitterStat);
  data.AddDataCalculator (rtAppJitterStat);

  // Downlink pipe filler, no marking whatsoever
  uint16_t dlGreedyPort = 5687;
  BulkSendHelper greedySender ("ns3::TcpSocketFactory",
                               InetSocketAddress (UEIpIface.GetAddress (0), dlGreedyPort));
  // MaxBytes==0 means send as much as possible until stopped
  greedySender.SetAttribute ("MaxBytes", UintegerValue (0));
  greedySender.SetAttribute ("SendSize", UintegerValue (1400));
  auto greedySenderApp = greedySender.Install (appServer.Get (0));
  greedySenderApp.Start (Seconds (0.02));

  PacketSinkHelper greedyReceiver ("ns3::TcpSocketFactory",
                                   InetSocketAddress (Ipv4Address::GetAny (), dlGreedyPort));
  auto greedyReceiverApp = greedyReceiver.Install (UE.Get (0));
  greedyReceiverApp.Start (Seconds (0.01));

  // Dump PHY, MAC, RLC and PDCP level KPIs
  lteHelper->EnableTraces ();
  // Get pcaps from the EPC and the SGi
  SGiLAN.EnablePcapAll ("llt:" + std::to_string (int(markingEnabled)));

  // Set the stop time.
  // This is needed otherwise the simulation will last forever, because (among
  // others) the start-of-subframe event is scheduled repeatedly, and the ns-3
  // simulator scheduler will hence never run out of events.
  Simulator::Stop (Seconds (5));

  // Run the simulation
  Simulator::Run ();

  //--------------------------------------------
  //-- Generate statistics output.
  //--------------------------------------------
  CreateObject<OmnetDataOutput> ()->Output (data);

  // Cleanup and exit:
  Simulator::Destroy ();

  return 0;
}
