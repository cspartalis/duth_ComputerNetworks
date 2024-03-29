/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/*
  Network topology:

  A----R----B 

  A--R: 15 Mbps / 20 ms delay
  R--B: 1  Mbps / 40 ms delay
  queue at R: size 10
*/

#include <iostream>
#include <fstream>
#include <string>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-helper.h"

using namespace ns3;


std::string fileNameRoot = "1_3a";    // base name for trace files, etc

// Definition of trace sink callback
void CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << " " <<  newCwnd << std::endl;
}

static void
TraceCwnd ()    // Trace changes to the congestion window
{
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream (fileNameRoot + ".cwnd");
  // Connect the trace sink (CwndChange) to the appropriate trace source ("/.../CongestionWindow")
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeBoundCallback (&CwndChange,stream)); 
}


int main (int argc, char *argv[])
{
  // Set simulation parameters
  int tcpSegmentSize = 1000;
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (tcpSegmentSize));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpNewReno"));
//  Config::SetDefault ("ns3::RttEstimator::MinRTO", TimeValue (MilliSeconds (500)));

  unsigned int runtime = 20;   // seconds
  int delayAR = 20;            // ms
  int delayRB = 11;            // ms
  double bottleneckBW= 1;      // Mbps
  double fastBW = 15;          // Mbps
  uint32_t queuesize = 10; 
  uint32_t maxBytes = 0;       // 0 means "unlimited"

  CommandLine cmd;
  // Here, we define command line options overriding some of the above.
  cmd.AddValue ("runtime", "How long the applications should send data", runtime);
  cmd.AddValue ("delayRB", "Delay on the R--B link, in ms", delayRB);
  cmd.AddValue ("queuesize", "queue size at R", queuesize);
  cmd.AddValue ("tcpSegmentSize", "TCP segment size", tcpSegmentSize);
  
  cmd.Parse (argc, argv);

  std::cout << "queuesize=" << queuesize << ", delayRB=" << delayRB << std::endl;
  //NodeContainer allNodes; allNodes.Create(3); Ptr<Node> A = allNodes.Get(0), etc
  Ptr<Node> A = CreateObject<Node> ();
  Ptr<Node> R = CreateObject<Node> ();
  Ptr<Node> B = CreateObject<Node> ();

  // use PointToPointChannel and PointToPointNetDevice            
  NetDeviceContainer devAR, devRB;
  PointToPointHelper AR, RB;

  // create point-to-point link from A to R
  AR.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (fastBW * 1000 * 1000)));
  AR.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (delayAR)));
  devAR = AR.Install(A, R);

  // create point-to-point link from R to B
  RB.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (bottleneckBW * 1000 * 1000)));
  RB.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (delayRB)));
  RB.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(queuesize));
  devRB = RB.Install(R,B);

   // pointers to netdevices
    Ptr<NetDevice> Adev = devAR.Get(0) ;
    Ptr<NetDevice> Rdev1 = devAR.Get(1) ;
    Ptr<NetDevice> Rdev2 = devRB.Get(0) ;
    Ptr<NetDevice> Bdev = devRB.Get(1) ;

  InternetStackHelper internet;
  internet.Install (A);
  internet.Install (R);
  internet.Install (B);

  // Assign IP addresses

  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer ipv4Interfaces;
  ipv4Interfaces.Add(ipv4.Assign (devAR));
  ipv4.SetBase ("10.0.1.0", "255.255.255.0");
  ipv4Interfaces.Add(ipv4.Assign(devRB));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  Ptr<Ipv4> A4 = A->GetObject<Ipv4>();  // gets node A's IPv4 subsystem
  Ptr<Ipv4> B4 = B->GetObject<Ipv4>();
  Ptr<Ipv4> R4 = R->GetObject<Ipv4>();
  Ipv4Address Aaddr = A4->GetAddress(1,0).GetLocal();
  Ipv4Address Baddr = B4->GetAddress(1,0).GetLocal();
  Ipv4Address Raddr = R4->GetAddress(1,0).GetLocal();

  std::cout << "A's address: " << Aaddr << std::endl;
  std::cout << "B's address: " << Baddr << std::endl;
  std::cout << "R's #1 address: " << Raddr << std::endl;
  std::cout << "R's #2 address: " << R4->GetAddress(2,0).GetLocal() << std::endl;

// Remove traffic controller, so that no queuing discipline is used and cwnd fluctuations can  be clearly observed

   TrafficControlHelper tch;
   tch.Uninstall(Rdev1);
   tch.Uninstall(Rdev2);
   tch.Uninstall(Adev);
   tch.Uninstall(Bdev);

  // create a sink on B
  uint16_t Bport = 80;
  Address sinkAaddr(InetSocketAddress (Ipv4Address::GetAny (), Bport));
  PacketSinkHelper sinkA ("ns3::TcpSocketFactory", sinkAaddr);
  ApplicationContainer sinkAppA = sinkA.Install (B);
  sinkAppA.Start (Seconds (0.01));
  // the following means the receiver will run 1 min longer than the sender app.
  sinkAppA.Stop (Seconds (runtime + 60.0));

  Address sinkAddr(InetSocketAddress(Baddr, Bport));

  BulkSendHelper sourceAhelper ("ns3::TcpSocketFactory",  sinkAddr);
  sourceAhelper.SetAttribute ("MaxBytes", UintegerValue (maxBytes));
  sourceAhelper.SetAttribute ("SendSize", UintegerValue (tcpSegmentSize));
  ApplicationContainer sourceAppsA = sourceAhelper.Install (A);
  sourceAppsA.Start (Seconds (0.0));
  sourceAppsA.Stop (Seconds (runtime));

  // Set up tracing
  AsciiTraceHelper ascii;
  std::string tfname = fileNameRoot + ".tr";
  AR.EnableAsciiAll (ascii.CreateFileStream (tfname));
  // Setup tracing for cwnd
  Simulator::Schedule(Seconds(0.01),&TraceCwnd);       // this Time cannot be 0.0
  
  // This tells ns-3 to generate pcap traces, including "-node#-dev#-" in filename
  // AR.EnablePcapAll (fileNameRoot);    // ".pcap" suffix is added automatically

  Simulator::Stop (Seconds (runtime+60));
  Simulator::Run ();

  Ptr<PacketSink> sink1 = DynamicCast<PacketSink> (sinkAppA.Get (0));
  std::cout << "Total Bytes Received from A: " << sink1->GetTotalRx () << std::endl;
  return 0;
}
