/*
 * V7 64-node self-organizing MAC experiment for ns-3.47.
 *
 * Research boundary:
 *   - IEEE 802.11n PHY/low-MAC are used as the transmission carrier.
 *   - The experiment studies the 100 ms high-MAC timeline, Beacon overhead,
 *     one-hop/two-hop neighbor learning, topology convergence and CSMA/CA data.
 *   - There is deliberately no IP, UDP, ARP or routing protocol. Data is sent
 *     directly through PacketSocket at layer 2.
 *
 * This file expects the small AdhocWifiMac extension in
 * ns3-adhoc-topology-beacon.patch when --controlFrame=beacon is selected.
 * The already existing SendVendorSpecificAction extension is used by
 * --controlFrame=action.
 */

#include "ns3/adhoc-wifi-mac.h"
#include "ns3/channel-access-manager.h"
#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/packet-socket-helper.h"
#include "ns3/qos-frame-exchange-manager.h"
#include "ns3/qos-txop.h"
#include "ns3/txop.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/wifi-module.h"

#include "../model/control-window-scheduler.h"
#include "../model/logical-clock.h"
#include "../model/mac-airtime-monitor.h"
#include "../model/data-trace-tag.h"
#include "../model/time-sync-manager.h"
#include "../model/topology-control-header.h"
#include "../model/topology-scenario.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SelfOrganizing64NodeMac");

namespace
{

constexpr uint32_t NODE_COUNT = 64;
constexpr uint16_t DATA_PROTOCOL = 0x88b5;

const Time SUPERFRAME = MilliSeconds(100);
const Time CONTROL_END = MilliSeconds(10);
const Time CONTROL_QUEUE_CLOSE = MilliSeconds(9); // 1 ms guard for CSMA/CA backoff
const Time FIRST_BEACON = MicroSeconds(200);
const Time TOPOLOGY_SAMPLE_PERIOD = MilliSeconds(10);
const Time DATA_ACTIVE_START = MilliSeconds(10.2);
const Time DATA_INJECTION_END = MilliSeconds(95.0); // leave 5 ms to drain the BE queue
const Time DATA_RELEASE_END = MilliSeconds(80.0);   // V4: conservative 20 ms drain/guard
const Time DATA_QUEUE_SERVICE_PERIOD = MilliSeconds(1.0);
Time g_beaconRotation = MilliSeconds(800);
Time g_stableConvergence = MilliSeconds(800);

enum class NeighborState : uint8_t
{
    UNSEEN,
    VALID,
    AGING,
    SUSPECT,
    EXPIRED
};

struct OneHopEntry
{
    NeighborState state{NeighborState::UNSEEN};
    Time lastHeard{Seconds(0)};
    uint16_t lastSuperframe{0};
    uint8_t missedRotations{0};
    std::set<uint8_t> advertisedNeighbors;
};

struct RadioState
{
    bool alive{true};
    std::map<uint8_t, OneHopEntry> oneHop;
    std::map<uint8_t, std::set<uint8_t>> twoHopVia;
    double packetCredit{0.0};
    uint32_t dataSequence{0};
};

struct PacketSizeStats
{
    uint64_t offeredPackets{0};
    uint64_t sourceDeadDrops{0};
    uint64_t blockedMissingLearned{0};
    uint64_t correctlyBlockedNoLink{0};
    uint64_t staleNeighborAttempts{0};
    uint64_t outsideWindowDrops{0};
    uint64_t socketRejected{0};
    uint64_t txPackets{0};
    uint64_t rxPackets{0};
    uint64_t txBytes{0};
    uint64_t rxBytes{0};
    std::vector<double> delaysMs;
};

struct GlobalStats
{
    uint64_t controlTx{0};
    uint64_t controlRx{0};
    uint64_t topologyTxBytes{0};
    uint64_t topologyRxBytes{0};
    uint64_t macControlTxBytes{0};
    uint64_t macControlRxBytes{0};
    uint64_t listFrames{0};
    uint64_t bitmapFrames{0};
    uint64_t superframeMismatch{0};
    uint64_t controlOutsideWindow{0};
    uint64_t dataOutsideWindow{0};
    uint64_t dataHardGateDeferrals{0};
    uint64_t controlWindowFlushDrops{0};
    uint64_t dataWindowFlushDrops{0};
    uint64_t dataWindowSafetyFlushDrops{0};
    uint64_t dataMacDequeued{0};
    uint64_t highMacEnqueued{0};
    uint64_t highMacReleased{0};
    uint64_t boundaryDeferredObservations{0};
    uint64_t maxHighMacBacklog{0};
    uint64_t controlRxBeforeInjectedLoss{0};
    uint64_t injectedControlDrops{0};
    std::map<uint32_t, PacketSizeStats> packetSizes;
};

struct EncodingDecision
{
    double timeSeconds{0.0};
    uint8_t node{0};
    uint32_t neighborCount{0};
    std::string requested;
    std::string selected;
    uint32_t listBytes{0};
    uint32_t bitmapBytes{0};
    uint32_t selectedBytes{0};
    int32_t bytesSavedVsList{0};
    int32_t bytesSavedVsBitmap{0};
};

struct GroundTruthEvent
{
    uint32_t id{0};
    double timeSeconds{0.0};
    std::string type;
    uint8_t node{0};
    Vector oldPosition;
    Vector newPosition;
    uint32_t changedLinks{0};
    std::map<uint8_t, uint32_t> affectedOneHop;
    std::map<uint8_t, uint32_t> affectedTwoHop;
};

struct TopologyEvent
{
    double timeSeconds{0.0};
    uint8_t observer{0};
    std::string event;
    uint8_t peer{0};
    uint16_t superframe{0};
};

struct TopologySample
{
    double timeSeconds{0.0};
    double oneHopPrecision{0.0};
    double oneHopRecall{0.0};
    double twoHopPrecision{0.0};
    double twoHopRecall{0.0};
    double oneHopF1{0.0};
    double twoHopF1{0.0};
    double macroOneHopF1{0.0};
    double macroTwoHopF1{0.0};
    uint64_t learnedOneHop{0};
    uint64_t actualOneHop{0};
    uint64_t learnedTwoHop{0};
    uint64_t actualTwoHop{0};
    uint64_t oneHopFalsePositive{0};
    uint64_t oneHopFalseNegative{0};
    uint64_t twoHopFalsePositive{0};
    uint64_t twoHopFalseNegative{0};
    uint32_t aliveNodes{0};
    uint32_t exactOneHopNodes{0};
    uint32_t exactTwoHopNodes{0};
    uint32_t exactBothNodes{0};
};

struct NodeTopologySample
{
    double timeSeconds{0.0};
    uint8_t observer{0};
    uint32_t learnedOneHop{0};
    uint32_t actualOneHop{0};
    uint32_t oneHopTruePositive{0};
    uint32_t oneHopFalsePositive{0};
    uint32_t oneHopFalseNegative{0};
    double oneHopPrecision{0.0};
    double oneHopRecall{0.0};
    double oneHopF1{0.0};
    bool exactOneHop{false};
    uint32_t learnedTwoHop{0};
    uint32_t actualTwoHop{0};
    uint32_t twoHopTruePositive{0};
    uint32_t twoHopFalsePositive{0};
    uint32_t twoHopFalseNegative{0};
    double twoHopPrecision{0.0};
    double twoHopRecall{0.0};
    double twoHopF1{0.0};
    bool exactTwoHop{false};
    std::string learnedOneHopIds;
    std::string actualOneHopIds;
    std::string learnedTwoHopIds;
    std::string actualTwoHopIds;
};

struct DataOutcome
{
    uint64_t demandId{0};
    double offeredTimeSeconds{0.0};
    uint8_t source{0};
    uint8_t destination{0};
    uint32_t bytes{0};
    std::string kind;
    bool actualNeighbor{false};
    bool learnedNeighbor{false};
    std::string decision;
    bool macAccepted{false};
    bool received{false};
    double firstMacSubmitTimeSeconds{-1.0};
    double queueDelayMilliseconds{-1.0};
    double receiveTimeSeconds{-1.0};
    double delayMilliseconds{-1.0};
};

struct PendingData
{
    uint64_t demandId{0};
    uint8_t source{0};
    uint8_t destination{0};
    uint32_t bytes{0};
    uint32_t sequence{0};
    uint16_t offeredSuperframe{0};
    Time offeredAt{Seconds(0)};
};

struct QueueBoundarySample
{
    double timeSeconds{0.0};
    uint64_t superframe{0};
    uint8_t node{0};
    uint32_t highMacPending{0};
    uint32_t wifiMacPending{0};
    std::string policy;
};

struct ControlWindowSample
{
    uint64_t superframe{0};
    uint32_t contenderLimit{0};
    uint32_t requested{0};
    uint32_t dequeued{0};
    uint32_t deferred{0};
    uint32_t expectedReceiverCopies{0};
    uint32_t receivedCopies{0};
};

struct LifetimeInterval
{
    double startSeconds{0.0};
    double endSeconds{0.0};
    uint8_t observer{0};
    bool alive{false};
    bool exactOneHop{false};
    bool exactTwoHop{false};
    std::string reason;
};

struct LifetimeState
{
    bool initialized{false};
    double startSeconds{0.0};
    bool alive{false};
    bool exactOneHop{false};
    bool exactTwoHop{false};
    uint32_t oneHopFalsePositive{0};
    uint32_t oneHopFalseNegative{0};
    uint32_t twoHopFalsePositive{0};
    uint32_t twoHopFalseNegative{0};
    std::string reason;
};

struct LifetimeTransition
{
    double timeSeconds{0.0};
    uint8_t observer{0};
    bool oldInitialized{false};
    bool oldAlive{false};
    bool oldExactOneHop{false};
    bool oldExactTwoHop{false};
    bool newAlive{false};
    bool newExactOneHop{false};
    bool newExactTwoHop{false};
    uint32_t oneHopFalsePositive{0};
    uint32_t oneHopFalseNegative{0};
    uint32_t twoHopFalsePositive{0};
    uint32_t twoHopFalseNegative{0};
    std::string reason;
};

struct ProbePair
{
    uint8_t first{0};
    uint8_t second{0};
    Time expires{Seconds(0)};
};

NodeContainer g_nodes;
NetDeviceContainer g_devices;
std::vector<Ptr<AdhocWifiMac>> g_macs;
std::vector<Ptr<WifiPhy>> g_phys;
std::vector<Ptr<Socket>> g_sockets;
std::vector<RadioState> g_radios(NODE_COUNT);
std::vector<TopologyEvent> g_topologyEvents;
std::vector<TopologySample> g_topologySamples;
std::vector<NodeTopologySample> g_nodeTopologySamples;
std::vector<EncodingDecision> g_encodingDecisions;
std::vector<GroundTruthEvent> g_groundTruthEvents;
std::vector<DataOutcome> g_dataOutcomes;
std::vector<std::deque<PendingData>> g_pendingData(NODE_COUNT);
std::vector<QueueBoundarySample> g_queueBoundarySamples;
std::vector<ControlWindowSample> g_controlWindowSamples;
std::vector<LifetimeInterval> g_lifetimeIntervals;
std::vector<LifetimeTransition> g_lifetimeTransitions;
std::array<LifetimeState, NODE_COUNT> g_lifetimeStates;
std::array<bool, NODE_COUNT> g_beaconPending{};
std::array<bool, NODE_COUNT> g_requestedThisWindow{};
std::array<bool, NODE_COUNT> g_dequeuedThisWindow{};
uint64_t g_activeControlSuperframe{0};
uint32_t g_expectedRxThisWindow{0};
std::map<uint16_t, uint32_t> g_controlRxBySuperframe;
std::map<uint64_t, std::size_t> g_dataOutcomeById;
std::map<std::pair<uint8_t, uint8_t>, ProbePair> g_probePairs;
GlobalStats g_stats;
MacAirtimeMonitor g_airtimeMonitor;

ControlTransport g_transport = ControlTransport::BEACON;
NeighborEncoding g_encoding = NeighborEncoding::ADAPTIVE;
Time g_start = Seconds(1.0);
Time g_duration = Seconds(35.0);
LogicalClock g_clock(g_start, SUPERFRAME);
ControlWindowScheduler g_controlScheduler(NODE_COUNT,
                                          CONTROL_END,
                                          FIRST_BEACON);
TimeSyncManager g_timeSync(g_clock);
double g_spacing = 30.0;
double g_maxRange = 36.0;
double g_perNodeMbps = 0.25;
uint32_t g_run = 1;
std::string g_density = "sparse";
std::string g_trafficMode = "learned-gated";
std::string g_trafficPattern = "burst";
std::string g_topologyScenario = "scripted";
std::string g_outputDirectory = "output/self-organizing-mac-v7";
std::string g_beaconAddress = "standard";
std::string g_dataAddress = "standard";
uint32_t g_controlContenders = 8;
std::string g_controlReleaseMode = "simultaneous";
uint32_t g_controlReleaseSpreadUs = 0;
uint16_t g_membershipEpoch = 0;
uint32_t g_neighborRemoveMisses = 2;
uint32_t g_neighborExpireMisses = 3;
double g_controlLossRate = 0.0;
uint64_t g_nextDemandId = 1;
std::string g_dataQueuePolicy = "defer";
uint32_t g_amsduBytes = 2304;
uint32_t g_macQueueAdmissionLimit = 4;
uint32_t g_fixedPacketBytes = 0;

Ptr<UniformRandomVariable> g_packetKindRng;
Ptr<UniformRandomVariable> g_jitterRng;
Ptr<UniformRandomVariable> g_destinationRng;
bool g_flushingWindowQueues = false;

std::set<uint8_t> ActualOneHop(uint8_t observer);
void ObserveNodeLifetime(uint8_t observer, const std::string& reason);
void ObserveAllLifetimes(const std::string& reason);
void SampleTopology();

bool
IsActive(const OneHopEntry& entry)
{
    return entry.state == NeighborState::VALID || entry.state == NeighborState::AGING;
}

std::string
ToString(NeighborState state)
{
    switch (state)
    {
    case NeighborState::UNSEEN:
        return "UNSEEN";
    case NeighborState::VALID:
        return "VALID";
    case NeighborState::AGING:
        return "AGING";
    case NeighborState::SUSPECT:
        return "SUSPECT";
    case NeighborState::EXPIRED:
        return "EXPIRED";
    }
    return "UNKNOWN";
}

uint16_t
CurrentSuperframe(Time now = Simulator::Now())
{
    return g_clock.GetWireSuperframe(now);
}

Time
OffsetInSuperframe(Time now = Simulator::Now())
{
    return g_clock.GetOffsetInSuperframe(now);
}

double
ExperimentTimeSeconds()
{
    return (Simulator::Now() - g_start).GetSeconds();
}

bool
InDataWindow(Time now = Simulator::Now())
{
    const Time offset = OffsetInSuperframe(now);
    return now >= g_start && offset >= DATA_ACTIVE_START && offset < DATA_INJECTION_END;
}

bool
AllowMacTransmission(const WifiMacHeader& header)
{
    if (!header.IsData())
    {
        return true;
    }
    const Time offset = OffsetInSuperframe();
    // Close the low-MAC gate at 95 ms.  The remaining 5 ms is a drain guard:
    // an MPDU that has already started may finish, but no new data MPDU or
    // retry may start close enough to cross the next 10 ms control window.
    const bool allowed = Simulator::Now() >= g_start &&
                         offset >= DATA_ACTIVE_START && offset < DATA_INJECTION_END;
    if (!allowed)
    {
        ++g_stats.dataHardGateDeferrals;
    }
    return allowed;
}

int16_t
SuperframeDelta(uint16_t remote, uint16_t local)
{
    return static_cast<int16_t>(static_cast<uint16_t>(remote - local));
}

uint32_t
MacControlFrameBytes(ControlTransport transport, uint32_t topologyBytes)
{
    // Action: 24-byte MAC header + action/topology body + 4-byte FCS.
    if (transport == ControlTransport::ACTION)
    {
        return 24 + topologyBytes + 4;
    }

    // V5 Beacon: standard mode uses the 24-byte IEEE 802.11 management header;
    // short mode uses the experimental 12-byte three-address header.  The body
    // contains 12 fixed bytes and 68 standard IE bytes (including the explicit
    // four-byte IBSS Parameter Set), followed by the private topology IE.
    const uint32_t macHeader = g_beaconAddress == "short" ? 12 : 24;
    return macHeader + 12 + 68 + topologyBytes + 4;
}

int32_t
FindNodeId(Mac48Address address)
{
    for (uint32_t id = 0; id < g_macs.size(); ++id)
    {
        if (g_macs[id]->GetAddress() == address)
        {
            return static_cast<int32_t>(id);
        }
    }
    return -1;
}

bool
ShouldInjectControlLoss(uint8_t receiver, uint8_t sender, uint16_t superframe)
{
    if (g_controlLossRate <= 0.0)
    {
        return false;
    }
    // Deterministic per receiver/sender/superframe, so all six schemes see the
    // same requested loss pattern even if their earlier reception counts differ.
    uint64_t value = (static_cast<uint64_t>(g_run) << 32) |
                     (static_cast<uint64_t>(superframe) << 16) |
                     (static_cast<uint64_t>(sender) << 8) | receiver;
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    const double unit = static_cast<double>(value >> 11) / 9007199254740992.0;
    return unit < g_controlLossRate;
}

std::set<uint8_t>
ValidOneHop(uint8_t node)
{
    std::set<uint8_t> result;
    for (const auto& [peer, entry] : g_radios.at(node).oneHop)
    {
        if (IsActive(entry))
        {
            result.insert(peer);
        }
    }
    return result;
}

void
RebuildTwoHop(uint8_t observer)
{
    auto& radio = g_radios.at(observer);
    radio.twoHopVia.clear();
    const auto direct = ValidOneHop(observer);
    for (const auto& [via, entry] : radio.oneHop)
    {
        if (!IsActive(entry))
        {
            continue;
        }
        for (const auto target : entry.advertisedNeighbors)
        {
            if (target == observer || target == via || direct.count(target) != 0)
            {
                continue;
            }
            radio.twoHopVia[target].insert(via);
        }
    }
}

void
LogTopologyEvent(uint8_t observer, const std::string& event, uint8_t peer, uint16_t sf)
{
    g_topologyEvents.push_back(
        {ExperimentTimeSeconds(), observer, event, peer, sf});
}

void
ProcessTopologyFrame(uint8_t receiver,
                     Ptr<const Packet> received,
                     Mac48Address from,
                     ControlTransport transport)
{
    if (!g_radios.at(receiver).alive)
    {
        return;
    }
    const int32_t senderId = FindNodeId(from);
    if (senderId < 0 || senderId == receiver)
    {
        return;
    }

    Ptr<Packet> packet = received->Copy();
    TopologyControlHeader header(transport);
    if (packet->GetSize() < 14)
    {
        return;
    }
    packet->RemoveHeader(header);
    if (!header.IsValid())
    {
        return;
    }

    const uint8_t sender = static_cast<uint8_t>(senderId);
    ++g_stats.controlRxBeforeInjectedLoss;
    if (ShouldInjectControlLoss(receiver, sender, header.GetSuperframe()))
    {
        ++g_stats.injectedControlDrops;
        return;
    }
    auto& entry = g_radios.at(receiver).oneHop[sender];
    const bool wasValid = IsActive(entry);
    entry.state = NeighborState::VALID;
    entry.lastHeard = Simulator::Now();
    entry.lastSuperframe = header.GetSuperframe();
    entry.missedRotations = 0;
    entry.advertisedNeighbors = header.GetNeighbors();
    entry.advertisedNeighbors.erase(sender);
    ++g_stats.controlRx;
    ++g_controlRxBySuperframe[header.GetSuperframe()];
    g_stats.topologyRxBytes += received->GetSize();
    g_stats.macControlRxBytes += MacControlFrameBytes(transport, received->GetSize());

    const int16_t difference = SuperframeDelta(header.GetSuperframe(), CurrentSuperframe());
    if (std::abs(static_cast<int>(difference)) > 1)
    {
        ++g_stats.superframeMismatch;
    }
    if (!wasValid)
    {
        LogTopologyEvent(receiver, "ONE_HOP_UP", sender, header.GetSuperframe());
    }
    RebuildTwoHop(receiver);
    ObserveNodeLifetime(receiver, "BEACON_RX");
}

void
ReceiveAction(uint32_t receiver,
              Ptr<const Packet> packet,
              Mac48Address from,
              Mac48Address)
{
    if (g_transport == ControlTransport::ACTION)
    {
        ProcessTopologyFrame(static_cast<uint8_t>(receiver),
                             packet,
                             from,
                             ControlTransport::ACTION);
    }
}

void
ReceiveBeacon(uint32_t receiver,
              Ptr<const Packet> topologyIe,
              Mac48Address from,
              Mac48Address)
{
    if (g_transport == ControlTransport::BEACON)
    {
        ProcessTopologyFrame(static_cast<uint8_t>(receiver),
                             topologyIe,
                             from,
                             ControlTransport::BEACON);
    }
}

void
SendTopology(uint8_t sender, uint64_t absoluteSf)
{
    if (sender >= NODE_COUNT || !g_radios.at(sender).alive)
    {
        return;
    }
    const auto neighbors = ValidOneHop(sender);
    TopologyControlHeader header;
    header.Configure(g_transport,
                     g_encoding,
                     static_cast<uint16_t>(absoluteSf & 0xffffU),
                     neighbors);
    const uint32_t bytes = header.GetSerializedSize();
    const bool bitmap = (g_encoding == NeighborEncoding::BITMAP) ||
                        (g_encoding == NeighborEncoding::ADAPTIVE && neighbors.size() >= 4);
    const uint32_t listBytes = 14 + 2 * neighbors.size();
    constexpr uint32_t bitmapBytes = 22;
    g_encodingDecisions.push_back({ExperimentTimeSeconds(),
                                   sender,
                                   static_cast<uint32_t>(neighbors.size()),
                                   ToString(g_encoding),
                                   bitmap ? "bitmap" : "list",
                                   listBytes,
                                   bitmapBytes,
                                   bytes,
                                   static_cast<int32_t>(listBytes) - static_cast<int32_t>(bytes),
                                   static_cast<int32_t>(bitmapBytes) - static_cast<int32_t>(bytes)});
    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(header);

    g_beaconPending.at(sender) = true;
    g_requestedThisWindow.at(sender) = true;

    if (g_transport == ControlTransport::ACTION)
    {
        g_macs.at(sender)->SendVendorSpecificAction(packet, Mac48Address::GetBroadcast());
    }
    else
    {
        // Added by ns3-adhoc-topology-beacon.patch. This queues a genuine
        // WIFI_MAC_MGT_BEACON and prepends MgtBeaconHeader before this IE.
        g_macs.at(sender)->SendTopologyBeacon(packet, SUPERFRAME);
    }

    ++g_stats.controlTx;
    g_stats.topologyTxBytes += bytes;
    g_stats.macControlTxBytes += MacControlFrameBytes(g_transport, bytes);
    bitmap ? ++g_stats.bitmapFrames : ++g_stats.listFrames;
}

Time
ControlReleaseJitter(uint8_t node, uint64_t absoluteSf)
{
    if (g_controlReleaseMode == "simultaneous")
    {
        return MicroSeconds(0);
    }
    if (g_controlReleaseMode == "staggered")
    {
        return MicroSeconds(900 * g_controlScheduler.CandidatePosition(
                                      node,
                                      static_cast<uint16_t>(g_controlContenders),
                                      g_membershipEpoch));
    }
    // Reproducible, locally computable jitter; all address modes see the same
    // eligibility and release times under the same run number.
    uint64_t value = (static_cast<uint64_t>(g_run) << 40) ^
                     (absoluteSf << 8) ^ static_cast<uint64_t>(node);
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return MicroSeconds(value % (static_cast<uint64_t>(g_controlReleaseSpreadUs) + 1));
}

void
EndControlWindow()
{
    uint32_t requested = 0;
    uint32_t dequeued = 0;
    uint32_t deferred = 0;
    for (uint8_t node = 0; node < NODE_COUNT; ++node)
    {
        requested += g_requestedThisWindow.at(node) ? 1 : 0;
        dequeued += g_dequeuedThisWindow.at(node) ? 1 : 0;
        deferred += (g_requestedThisWindow.at(node) && !g_dequeuedThisWindow.at(node)) ? 1 : 0;
    }
    g_controlWindowSamples.push_back({g_activeControlSuperframe,
                                      g_controlContenders,
                                      requested,
                                      dequeued,
                                      deferred,
                                      g_expectedRxThisWindow,
                                      g_controlRxBySuperframe[static_cast<uint16_t>(
                                          g_activeControlSuperframe & 0xffffU)]});

    g_flushingWindowQueues = true;
    for (const auto& mac : g_macs)
    {
        Ptr<WifiMacQueue> queue = mac->GetQosTxop(AC_VO)->GetWifiMacQueue();
        g_stats.controlWindowFlushDrops += queue->GetNPackets();
        queue->Flush();
    }
    g_flushingWindowQueues = false;
}

void
FreezeDataEdca()
{
    if (g_dataQueuePolicy != "defer")
    {
        return;
    }
    // Freeze only AC_BE from 80 ms until the next data window at 10.2 ms.
    // AC_VO remains active, so topology Action/Beacon traffic can still use
    // the control window.  Backoff and queued data are preserved by ns-3.
    const Time freezeDuration = SUPERFRAME - DATA_RELEASE_END + DATA_ACTIVE_START;
    for (const auto& mac : g_macs)
    {
        mac->GetChannelAccessManager()->DisableEdcaFor(mac->GetQosTxop(AC_BE),
                                                       freezeDuration);
    }
}

void
ScheduleControlWindow(uint64_t absoluteSf)
{
    // Enforce the 10 ms/90 ms high-MAC boundary.  The V3 baseline flushes the
    // residual Wi-Fi queue.  The V4 defer policy keeps not-yet-admitted packets
    // in a high-MAC queue and freezes AC_BE 20 ms before this boundary.  Both
    // the high-MAC and Wi-Fi queues are preserved until the next data window.
    g_flushingWindowQueues = true;
    for (uint8_t node = 0; node < NODE_COUNT; ++node)
    {
        const auto& mac = g_macs.at(node);
        Ptr<WifiMacQueue> queue = mac->GetQosTxop(AC_BE)->GetWifiMacQueue();
        const uint32_t wifiPending = queue->GetNPackets();
        const uint32_t highMacPending = g_pendingData.at(node).size();
        g_queueBoundarySamples.push_back({ExperimentTimeSeconds(),
                                          absoluteSf,
                                          node,
                                          highMacPending,
                                          wifiPending,
                                          g_dataQueuePolicy});
        if (g_dataQueuePolicy == "defer")
        {
            g_stats.boundaryDeferredObservations += highMacPending + wifiPending;
            g_stats.maxHighMacBacklog =
                std::max(g_stats.maxHighMacBacklog, static_cast<uint64_t>(highMacPending));
            // AC_BE has been frozen since 80 ms.  Preserve both the high-MAC
            // queue and the Wi-Fi queue through the 10 ms control window.
        }
        else
        {
            g_stats.dataWindowFlushDrops += wifiPending;
            queue->Flush();
        }
    }
    g_flushingWindowQueues = false;

    // Re-assert the AC_BE freeze exactly at the control-window boundary.
    // An MPDU already in a failure/retry path can request access after the
    // earlier 80 ms freeze; this also freezes such retries without dropping
    // the Wi-Fi queue.
    if (g_dataQueuePolicy == "defer")
    {
        for (const auto& mac : g_macs)
        {
            mac->GetChannelAccessManager()->DisableEdcaFor(mac->GetQosTxop(AC_BE),
                                                           DATA_ACTIVE_START);
        }
    }

    g_activeControlSuperframe = absoluteSf;
    g_expectedRxThisWindow = 0;
    g_requestedThisWindow.fill(false);
    g_dequeuedThisWindow.fill(false);

    // Every radio evaluates the same rule locally.  The simulator iterates
    // nodes only to schedule their independent decisions; it does not build or
    // distribute a central list of selected node IDs.
    for (uint8_t node = 0; node < NODE_COUNT; ++node)
    {
        if (!g_radios.at(node).alive ||
            !g_controlScheduler.ShouldAttempt(absoluteSf,
                                              node,
                                              static_cast<uint8_t>(g_controlContenders),
                                              g_beaconPending.at(node),
                                              g_membershipEpoch))
        {
            continue;
        }
        g_expectedRxThisWindow += ActualOneHop(node).size();
        Simulator::Schedule(FIRST_BEACON + ControlReleaseJitter(node, absoluteSf),
                            &SendTopology,
                            node,
                            absoluteSf);
        // TIME_SYNC will later use a separate local eligibility rule.
    }
    Simulator::Schedule(CONTROL_QUEUE_CLOSE, &EndControlWindow);
}

void
AgeNeighbors()
{
    const Time now = Simulator::Now();
    for (uint8_t observer = 0; observer < NODE_COUNT; ++observer)
    {
        bool changed = false;
        for (auto& [peer, entry] : g_radios.at(observer).oneHop)
        {
            if (entry.state == NeighborState::UNSEEN || entry.state == NeighborState::EXPIRED)
            {
                continue;
            }
            const Time age = now - entry.lastHeard;
            const uint8_t missed = static_cast<uint8_t>(std::min<int64_t>(
                255,
                age.GetNanoSeconds() / std::max<int64_t>(1, g_beaconRotation.GetNanoSeconds())));
            entry.missedRotations = missed;
            if (missed >= g_neighborExpireMisses)
            {
                entry.state = NeighborState::EXPIRED;
                entry.advertisedNeighbors.clear();
                changed = true;
                LogTopologyEvent(observer, "ONE_HOP_EXPIRED", peer, CurrentSuperframe());
            }
            else if (missed >= g_neighborRemoveMisses &&
                     entry.state != NeighborState::SUSPECT)
            {
                entry.state = NeighborState::SUSPECT;
                entry.advertisedNeighbors.clear();
                changed = true;
                LogTopologyEvent(observer, "ONE_HOP_SUSPECT", peer, CurrentSuperframe());
            }
            else if (missed == 1 && entry.state == NeighborState::VALID)
            {
                entry.state = NeighborState::AGING;
                LogTopologyEvent(observer, "ONE_HOP_AGING", peer, CurrentSuperframe());
            }
        }
        if (changed)
        {
            RebuildTwoHop(observer);
            ObserveNodeLifetime(observer, "NEIGHBOR_AGING");
        }
    }
}

uint32_t
ChoosePacketSize()
{
    if (g_fixedPacketBytes != 0)
    {
        return g_fixedPacketBytes;
    }
    const double sample = g_packetKindRng->GetValue();
    if (sample < 0.4)
    {
        return 256;
    }
    if (sample < 0.8)
    {
        return 1024;
    }
    return 1472;
}

void
SubmitPendingData(const PendingData& pending)
{
    const auto outcomeIt = g_dataOutcomeById.find(pending.demandId);
    if (outcomeIt == g_dataOutcomeById.end())
    {
        return;
    }
    auto& outcome = g_dataOutcomes.at(outcomeIt->second);
    auto& stats = g_stats.packetSizes[pending.bytes];

    Ptr<Packet> packet = Create<Packet>(pending.bytes);
    DataTraceTag tag;
    // The tag remains simulator-only.  Using the original offer time makes the
    // measured delay include V4 high-MAC queueing without adding on-air bytes.
    tag.Set(pending.demandId,
            pending.source,
            pending.destination,
            pending.sequence,
            pending.offeredSuperframe,
            pending.offeredAt);
    packet->AddPacketTag(tag);

    PacketSocketAddress remote;
    remote.SetSingleDevice(g_devices.Get(pending.source)->GetIfIndex());
    remote.SetPhysicalAddress(g_devices.Get(pending.destination)->GetAddress());
    remote.SetProtocol(DATA_PROTOCOL);
    const int sent = g_sockets.at(pending.source)->SendTo(packet, 0, remote);
    outcome.firstMacSubmitTimeSeconds = ExperimentTimeSeconds();
    outcome.queueDelayMilliseconds =
        (Simulator::Now() - pending.offeredAt).GetSeconds() * 1000.0;
    if (sent > 0)
    {
        ++stats.txPackets;
        stats.txBytes += pending.bytes;
        outcome.macAccepted = true;
        outcome.decision = outcome.actualNeighbor ? "SENT_TRUE_NEIGHBOR"
                                                  : "SENT_FALSE_POSITIVE";
    }
    else
    {
        ++stats.socketRejected;
        outcome.decision = "SOCKET_REJECTED";
    }
}

void
ServicePendingQueues()
{
    if (g_dataQueuePolicy != "defer" || OffsetInSuperframe() >= DATA_RELEASE_END)
    {
        return;
    }

    for (uint8_t source = 0; source < NODE_COUNT; ++source)
    {
        Ptr<WifiMacQueue> queue =
            g_macs.at(source)->GetQosTxop(AC_BE)->GetWifiMacQueue();
        auto& pending = g_pendingData.at(source);
        while (!pending.empty() && queue->GetNPackets() < g_macQueueAdmissionLimit)
        {
            const PendingData demand = pending.front();
            pending.pop_front();
            SubmitPendingData(demand);
            ++g_stats.highMacReleased;
        }
        g_stats.maxHighMacBacklog =
            std::max(g_stats.maxHighMacBacklog, static_cast<uint64_t>(pending.size()));
    }

    if (OffsetInSuperframe() + DATA_QUEUE_SERVICE_PERIOD < DATA_RELEASE_END)
    {
        Simulator::Schedule(DATA_QUEUE_SERVICE_PERIOD, &ServicePendingQueues);
    }
}

void
SendDataTo(uint8_t source, uint8_t destination, uint32_t totalBytes, const std::string& kind)
{
    auto& stats = g_stats.packetSizes[totalBytes];
    ++stats.offeredPackets;

    DataOutcome outcome;
    outcome.demandId = g_nextDemandId++;
    outcome.offeredTimeSeconds = ExperimentTimeSeconds();
    outcome.source = source;
    outcome.destination = destination;
    outcome.bytes = totalBytes;
    outcome.kind = kind;

    if (!g_radios.at(source).alive)
    {
        ++stats.sourceDeadDrops;
        outcome.decision = "SOURCE_DEAD";
        g_dataOutcomes.push_back(outcome);
        return;
    }
    if (!InDataWindow())
    {
        ++g_stats.dataOutsideWindow;
        ++stats.outsideWindowDrops;
        outcome.decision = "OUTSIDE_DATA_WINDOW";
        g_dataOutcomes.push_back(outcome);
        return;
    }

    outcome.actualNeighbor = ActualOneHop(source).count(destination) != 0;
    outcome.learnedNeighbor = ValidOneHop(source).count(destination) != 0;
    if (!outcome.learnedNeighbor)
    {
        if (outcome.actualNeighbor)
        {
            ++stats.blockedMissingLearned;
            outcome.decision = "BLOCKED_FALSE_NEGATIVE";
        }
        else
        {
            ++stats.correctlyBlockedNoLink;
            outcome.decision = "BLOCKED_NO_TRUE_LINK";
        }
        g_dataOutcomes.push_back(outcome);
        return;
    }
    if (!outcome.actualNeighbor)
    {
        ++stats.staleNeighborAttempts;
        outcome.decision = "SENT_FALSE_POSITIVE";
    }
    else
    {
        outcome.decision = "SENT_TRUE_NEIGHBOR";
    }

    const PendingData pending{outcome.demandId,
                              source,
                              destination,
                              totalBytes,
                              g_radios.at(source).dataSequence++,
                              CurrentSuperframe(),
                              Simulator::Now()};
    const std::size_t outcomeIndex = g_dataOutcomes.size();
    g_dataOutcomeById[outcome.demandId] = outcomeIndex;
    g_dataOutcomes.push_back(outcome);

    if (g_dataQueuePolicy == "defer")
    {
        auto& stored = g_dataOutcomes.at(outcomeIndex);
        stored.decision = stored.actualNeighbor ? "QUEUED_TRUE_NEIGHBOR"
                                                : "QUEUED_FALSE_POSITIVE";
        g_pendingData.at(source).push_back(pending);
        ++g_stats.highMacEnqueued;
        g_stats.maxHighMacBacklog = std::max(
            g_stats.maxHighMacBacklog,
            static_cast<uint64_t>(g_pendingData.at(source).size()));
    }
    else
    {
        SubmitPendingData(pending);
    }
}

void
SendBackgroundData(uint8_t source, uint32_t totalBytes)
{
    if (!g_radios.at(source).alive)
    {
        auto& stats = g_stats.packetSizes[totalBytes];
        ++stats.offeredPackets;
        ++stats.sourceDeadDrops;
        DataOutcome outcome;
        outcome.demandId = g_nextDemandId++;
        outcome.offeredTimeSeconds = ExperimentTimeSeconds();
        outcome.source = source;
        outcome.destination = source;
        outcome.bytes = totalBytes;
        outcome.kind = "BACKGROUND";
        outcome.decision = "SOURCE_DEAD";
        g_dataOutcomes.push_back(outcome);
        return;
    }
    const auto actual = ActualOneHop(source);
    if (actual.empty())
    {
        return;
    }
    const uint32_t index = g_destinationRng->GetInteger(0, actual.size() - 1);
    auto destinationIt = actual.begin();
    std::advance(destinationIt, index);
    SendDataTo(source, *destinationIt, totalBytes, "BACKGROUND");
}

void
ScheduleDataWindow()
{
    if (g_dataQueuePolicy == "defer")
    {
        Simulator::ScheduleNow(&ServicePendingQueues);
    }
    constexpr double expectedPacketBytes = 0.4 * 256 + 0.4 * 1024 + 0.2 * 1472;
    const double offeredPacketBytes =
        g_fixedPacketBytes == 0 ? expectedPacketBytes : static_cast<double>(g_fixedPacketBytes);
    const double packetsPerSuperframe = g_perNodeMbps * 1e6 * SUPERFRAME.GetSeconds() /
                                        (offeredPacketBytes * 8.0);
    const Time active = DATA_INJECTION_END - DATA_ACTIVE_START;
    for (uint8_t source = 0; source < NODE_COUNT; ++source)
    {
        auto& credit = g_radios.at(source).packetCredit;
        credit += packetsPerSuperframe;
        const uint32_t count = static_cast<uint32_t>(std::floor(credit));
        credit -= count;
        if (g_trafficPattern == "burst" && count > 0 && g_radios.at(source).alive)
        {
            // Model a short upper-layer flow burst.  All packets in the burst
            // use one physical neighbor so A-MSDU has a fair opportunity to
            // aggregate, while the offered byte rate and packet-size mix stay
            // unchanged.  Bursts end well before the 80 ms EDCA freeze point.
            const auto actual = ActualOneHop(source);
            if (!actual.empty())
            {
                const uint32_t destinationIndex =
                    g_destinationRng->GetInteger(0, actual.size() - 1);
                auto destination = actual.begin();
                std::advance(destination, destinationIndex);
                const Time burstStart =
                    MilliSeconds(1) + MicroSeconds(g_jitterRng->GetInteger(0, 45000));
                for (uint32_t n = 0; n < count; ++n)
                {
                    Simulator::Schedule(burstStart + MicroSeconds(50 * n),
                                        &SendDataTo,
                                        source,
                                        *destination,
                                        ChoosePacketSize(),
                                        std::string("BACKGROUND"));
                }
            }
        }
        else
        {
            for (uint32_t n = 0; n < count; ++n)
            {
                const double fraction = (n + 0.5) / std::max(1U, count);
                const Time base = Seconds(active.GetSeconds() * fraction);
                const Time jitter = MicroSeconds(g_jitterRng->GetInteger(0, 150));
                if (base + jitter < active)
                {
                    Simulator::Schedule(base + jitter,
                                        &SendBackgroundData,
                                        source,
                                        ChoosePacketSize());
                }
            }
        }
    }

    for (auto it = g_probePairs.begin(); it != g_probePairs.end();)
    {
        if (it->second.expires <= Simulator::Now())
        {
            it = g_probePairs.erase(it);
        }
        else
        {
            ++it;
        }
    }
    std::vector<std::pair<uint8_t, uint8_t>> probes;
    for (const auto& [key, probe] : g_probePairs)
    {
        (void)key;
        probes.emplace_back(probe.first, probe.second);
        probes.emplace_back(probe.second, probe.first);
    }
    for (std::size_t index = 0; index < probes.size(); ++index)
    {
        const double fraction = (index + 0.5) / std::max<std::size_t>(1, probes.size());
        const Time offset = Seconds(active.GetSeconds() * fraction);
        Simulator::Schedule(offset,
                            &SendDataTo,
                            probes.at(index).first,
                            probes.at(index).second,
                            256,
                            std::string("TOPOLOGY_PROBE"));
    }
}

void
ReceiveData(uint32_t receiver, Ptr<Socket> socket)
{
    Address from;
    while (Ptr<Packet> packet = socket->RecvFrom(from))
    {
        const uint32_t totalBytes = packet->GetSize();
        DataTraceTag tag;
        if (!packet->PeekPacketTag(tag) || tag.GetDestination() != receiver)
        {
            continue;
        }
        auto& stats = g_stats.packetSizes[totalBytes];
        ++stats.rxPackets;
        stats.rxBytes += totalBytes;
        const double delayMs = (Simulator::Now() - tag.GetTransmitTime()).GetSeconds() * 1000.0;
        stats.delaysMs.push_back(delayMs);
        const auto outcomeIt = g_dataOutcomeById.find(tag.GetDemandId());
        if (outcomeIt != g_dataOutcomeById.end())
        {
            auto& outcome = g_dataOutcomes.at(outcomeIt->second);
            outcome.received = true;
            outcome.receiveTimeSeconds = ExperimentTimeSeconds();
            outcome.delayMilliseconds = delayMs;
        }
    }
}

void
QueueDequeue(uint32_t node, Ptr<const WifiMpdu> mpdu)
{
    if (Simulator::Now() < g_start || g_flushingWindowQueues)
    {
        return;
    }
    if (mpdu->GetHeader().IsData())
    {
        ++g_stats.dataMacDequeued;
    }
    // Only topology Beacons count as control-window violations here. Other
    // management exchanges can legitimately belong to data-plane QoS work.
    if (mpdu->GetHeader().IsBeacon() && OffsetInSuperframe() >= CONTROL_END)
    {
        ++g_stats.controlOutsideWindow;
    }
    if (mpdu->GetHeader().IsBeacon())
    {
        g_beaconPending.at(node) = false;
        if (OffsetInSuperframe() < CONTROL_END)
        {
            g_dequeuedThisWindow.at(node) = true;
        }
    }
    else if (mpdu->GetHeader().IsData() && OffsetInSuperframe() < CONTROL_END)
    {
        ++g_stats.dataOutsideWindow;
    }
}

void
PhyTxPsduBegin(uint32_t node,
               WifiConstPsduMap psduMap,
               WifiTxVector txVector,
               double)
{
    g_airtimeMonitor.Observe(node,
                             ExperimentTimeSeconds(),
                             OffsetInSuperframe().GetMicroSeconds(),
                             std::move(psduMap),
                             txVector,
                             g_phys.at(node)->GetPhyBand());
}

double
SafeRatio(uint64_t numerator, uint64_t denominator)
{
    return denominator == 0 ? 1.0 : static_cast<double>(numerator) / denominator;
}

std::set<uint8_t>
ActualOneHop(uint8_t observer)
{
    std::set<uint8_t> result;
    if (!g_radios.at(observer).alive)
    {
        return result;
    }
    const Vector a = g_nodes.Get(observer)->GetObject<MobilityModel>()->GetPosition();
    for (uint8_t peer = 0; peer < NODE_COUNT; ++peer)
    {
        if (peer == observer || !g_radios.at(peer).alive)
        {
            continue;
        }
        const Vector b = g_nodes.Get(peer)->GetObject<MobilityModel>()->GetPosition();
        if (CalculateDistance(a, b) <= g_maxRange)
        {
            result.insert(peer);
        }
    }
    return result;
}

std::set<uint8_t>
ActualTwoHop(uint8_t observer)
{
    const auto direct = ActualOneHop(observer);
    std::set<uint8_t> result;
    for (const auto via : direct)
    {
        for (const auto target : ActualOneHop(via))
        {
            if (target != observer && direct.count(target) == 0)
            {
                result.insert(target);
            }
        }
    }
    return result;
}

uint64_t
IntersectionSize(const std::set<uint8_t>& a, const std::set<uint8_t>& b)
{
    std::vector<uint8_t> intersection;
    std::set_intersection(a.begin(),
                          a.end(),
                          b.begin(),
                          b.end(),
                          std::back_inserter(intersection));
    return intersection.size();
}

uint64_t
DifferenceSize(const std::set<uint8_t>& a, const std::set<uint8_t>& b)
{
    std::vector<uint8_t> difference;
    std::set_difference(a.begin(),
                        a.end(),
                        b.begin(),
                        b.end(),
                        std::back_inserter(difference));
    return difference.size();
}

uint64_t
SymmetricDifferenceSize(const std::set<uint8_t>& a, const std::set<uint8_t>& b)
{
    return DifferenceSize(a, b) + DifferenceSize(b, a);
}

double
F1Score(double precision, double recall)
{
    return precision + recall == 0.0 ? 0.0
                                     : 2.0 * precision * recall / (precision + recall);
}

std::set<uint8_t>
LearnedTwoHop(uint8_t observer)
{
    std::set<uint8_t> result;
    for (const auto& [target, via] : g_radios.at(observer).twoHopVia)
    {
        if (!via.empty())
        {
            result.insert(target);
        }
    }
    return result;
}

void
ObserveNodeLifetime(uint8_t observer, const std::string& reason)
{
    const bool alive = g_radios.at(observer).alive;
    bool exactOne = false;
    bool exactTwo = false;
    uint32_t oneFalsePositive = 0;
    uint32_t oneFalseNegative = 0;
    uint32_t twoFalsePositive = 0;
    uint32_t twoFalseNegative = 0;
    if (alive)
    {
        const auto learnedOne = ValidOneHop(observer);
        const auto actualOne = ActualOneHop(observer);
        const auto learnedTwo = LearnedTwoHop(observer);
        const auto actualTwo = ActualTwoHop(observer);
        exactOne = learnedOne == actualOne;
        exactTwo = learnedTwo == actualTwo;
        oneFalsePositive = DifferenceSize(learnedOne, actualOne);
        oneFalseNegative = DifferenceSize(actualOne, learnedOne);
        twoFalsePositive = DifferenceSize(learnedTwo, actualTwo);
        twoFalseNegative = DifferenceSize(actualTwo, learnedTwo);
    }

    const double now = ExperimentTimeSeconds();
    auto& current = g_lifetimeStates.at(observer);
    if (!current.initialized)
    {
        g_lifetimeTransitions.push_back({now,
                                         observer,
                                         false,
                                         false,
                                         false,
                                         false,
                                         alive,
                                         exactOne,
                                         exactTwo,
                                         oneFalsePositive,
                                         oneFalseNegative,
                                         twoFalsePositive,
                                         twoFalseNegative,
                                         reason});
        current = {true,
                   now,
                   alive,
                   exactOne,
                   exactTwo,
                   oneFalsePositive,
                   oneFalseNegative,
                   twoFalsePositive,
                   twoFalseNegative,
                   reason};
        return;
    }
    if (current.alive == alive && current.exactOneHop == exactOne &&
        current.exactTwoHop == exactTwo &&
        current.oneHopFalsePositive == oneFalsePositive &&
        current.oneHopFalseNegative == oneFalseNegative &&
        current.twoHopFalsePositive == twoFalsePositive &&
        current.twoHopFalseNegative == twoFalseNegative)
    {
        return;
    }
    if (now > current.startSeconds)
    {
        g_lifetimeIntervals.push_back({current.startSeconds,
                                       now,
                                       observer,
                                       current.alive,
                                       current.exactOneHop,
                                       current.exactTwoHop,
                                       current.reason});
    }
    g_lifetimeTransitions.push_back({now,
                                     observer,
                                     true,
                                     current.alive,
                                     current.exactOneHop,
                                     current.exactTwoHop,
                                     alive,
                                     exactOne,
                                     exactTwo,
                                     oneFalsePositive,
                                     oneFalseNegative,
                                     twoFalsePositive,
                                     twoFalseNegative,
                                     reason});
    current = {true,
               now,
               alive,
               exactOne,
               exactTwo,
               oneFalsePositive,
               oneFalseNegative,
               twoFalsePositive,
               twoFalseNegative,
               reason};
}

void
ObserveAllLifetimes(const std::string& reason)
{
    for (uint8_t observer = 0; observer < NODE_COUNT; ++observer)
    {
        ObserveNodeLifetime(observer, reason);
    }
}

void
SampleTopologyAndLifetime()
{
    SampleTopology();
    ObserveAllLifetimes("PERIODIC_10MS");
}

void
FinalizeLifetimeIntervals()
{
    const double end = g_duration.GetSeconds();
    for (uint8_t observer = 0; observer < NODE_COUNT; ++observer)
    {
        const auto& current = g_lifetimeStates.at(observer);
        if (current.initialized && end > current.startSeconds)
        {
            g_lifetimeIntervals.push_back({current.startSeconds,
                                           end,
                                           observer,
                                           current.alive,
                                           current.exactOneHop,
                                           current.exactTwoHop,
                                           current.reason});
        }
    }
}

std::string
JoinNodeIds(const std::set<uint8_t>& nodes)
{
    std::ostringstream stream;
    bool first = true;
    for (const auto node : nodes)
    {
        if (!first)
        {
            stream << ';';
        }
        stream << +node;
        first = false;
    }
    return stream.str();
}

void
SampleTopology()
{
    uint64_t learnedOne = 0;
    uint64_t actualOne = 0;
    uint64_t trueOne = 0;
    uint64_t learnedTwo = 0;
    uint64_t actualTwo = 0;
    uint64_t trueTwo = 0;
    uint64_t falsePositiveOne = 0;
    uint64_t falseNegativeOne = 0;
    uint64_t falsePositiveTwo = 0;
    uint64_t falseNegativeTwo = 0;
    uint32_t aliveNodes = 0;
    uint32_t exactOneNodes = 0;
    uint32_t exactTwoNodes = 0;
    uint32_t exactBothNodes = 0;
    double sumOneF1 = 0.0;
    double sumTwoF1 = 0.0;
    for (uint8_t observer = 0; observer < NODE_COUNT; ++observer)
    {
        if (!g_radios.at(observer).alive)
        {
            continue;
        }
        ++aliveNodes;
        const auto learnedOneSet = ValidOneHop(observer);
        const auto actualOneSet = ActualOneHop(observer);
        const auto learnedTwoSet = LearnedTwoHop(observer);
        const auto actualTwoSet = ActualTwoHop(observer);
        const uint64_t nodeTrueOne = IntersectionSize(learnedOneSet, actualOneSet);
        const uint64_t nodeFalsePositiveOne = DifferenceSize(learnedOneSet, actualOneSet);
        const uint64_t nodeFalseNegativeOne = DifferenceSize(actualOneSet, learnedOneSet);
        const uint64_t nodeTrueTwo = IntersectionSize(learnedTwoSet, actualTwoSet);
        const uint64_t nodeFalsePositiveTwo = DifferenceSize(learnedTwoSet, actualTwoSet);
        const uint64_t nodeFalseNegativeTwo = DifferenceSize(actualTwoSet, learnedTwoSet);
        const double nodeOnePrecision = SafeRatio(nodeTrueOne,
                                                  nodeTrueOne + nodeFalsePositiveOne);
        const double nodeOneRecall = SafeRatio(nodeTrueOne,
                                               nodeTrueOne + nodeFalseNegativeOne);
        const double nodeTwoPrecision = SafeRatio(nodeTrueTwo,
                                                  nodeTrueTwo + nodeFalsePositiveTwo);
        const double nodeTwoRecall = SafeRatio(nodeTrueTwo,
                                               nodeTrueTwo + nodeFalseNegativeTwo);
        const double nodeOneF1 = F1Score(nodeOnePrecision, nodeOneRecall);
        const double nodeTwoF1 = F1Score(nodeTwoPrecision, nodeTwoRecall);
        const bool exactOne = learnedOneSet == actualOneSet;
        const bool exactTwo = learnedTwoSet == actualTwoSet;

        g_nodeTopologySamples.push_back({ExperimentTimeSeconds(),
                                         observer,
                                         static_cast<uint32_t>(learnedOneSet.size()),
                                         static_cast<uint32_t>(actualOneSet.size()),
                                         static_cast<uint32_t>(nodeTrueOne),
                                         static_cast<uint32_t>(nodeFalsePositiveOne),
                                         static_cast<uint32_t>(nodeFalseNegativeOne),
                                         nodeOnePrecision,
                                         nodeOneRecall,
                                         nodeOneF1,
                                         exactOne,
                                         static_cast<uint32_t>(learnedTwoSet.size()),
                                         static_cast<uint32_t>(actualTwoSet.size()),
                                         static_cast<uint32_t>(nodeTrueTwo),
                                         static_cast<uint32_t>(nodeFalsePositiveTwo),
                                         static_cast<uint32_t>(nodeFalseNegativeTwo),
                                         nodeTwoPrecision,
                                         nodeTwoRecall,
                                         nodeTwoF1,
                                         exactTwo,
                                         JoinNodeIds(learnedOneSet),
                                         JoinNodeIds(actualOneSet),
                                         JoinNodeIds(learnedTwoSet),
                                         JoinNodeIds(actualTwoSet)});

        learnedOne += learnedOneSet.size();
        actualOne += actualOneSet.size();
        trueOne += nodeTrueOne;
        learnedTwo += learnedTwoSet.size();
        actualTwo += actualTwoSet.size();
        trueTwo += nodeTrueTwo;
        falsePositiveOne += nodeFalsePositiveOne;
        falseNegativeOne += nodeFalseNegativeOne;
        falsePositiveTwo += nodeFalsePositiveTwo;
        falseNegativeTwo += nodeFalseNegativeTwo;
        exactOneNodes += exactOne ? 1 : 0;
        exactTwoNodes += exactTwo ? 1 : 0;
        exactBothNodes += exactOne && exactTwo ? 1 : 0;
        sumOneF1 += nodeOneF1;
        sumTwoF1 += nodeTwoF1;
    }
    const double onePrecision = SafeRatio(trueOne, trueOne + falsePositiveOne);
    const double oneRecall = SafeRatio(trueOne, trueOne + falseNegativeOne);
    const double twoPrecision = SafeRatio(trueTwo, trueTwo + falsePositiveTwo);
    const double twoRecall = SafeRatio(trueTwo, trueTwo + falseNegativeTwo);
    g_topologySamples.push_back({ExperimentTimeSeconds(),
                                 onePrecision,
                                 oneRecall,
                                 twoPrecision,
                                 twoRecall,
                                 F1Score(onePrecision, oneRecall),
                                 F1Score(twoPrecision, twoRecall),
                                 aliveNodes == 0 ? 1.0 : sumOneF1 / aliveNodes,
                                 aliveNodes == 0 ? 1.0 : sumTwoF1 / aliveNodes,
                                 learnedOne,
                                 actualOne,
                                 learnedTwo,
                                 actualTwo,
                                 falsePositiveOne,
                                 falseNegativeOne,
                                 falsePositiveTwo,
                                 falseNegativeTwo,
                                 aliveNodes,
                                 exactOneNodes,
                                 exactTwoNodes,
                                 exactBothNodes});
}

using TruthSnapshot = std::array<std::set<uint8_t>, NODE_COUNT>;

TruthSnapshot
CaptureOneHopTruth()
{
    TruthSnapshot snapshot;
    for (uint8_t observer = 0; observer < NODE_COUNT; ++observer)
    {
        snapshot.at(observer) = ActualOneHop(observer);
    }
    return snapshot;
}

TruthSnapshot
CaptureTwoHopTruth()
{
    TruthSnapshot snapshot;
    for (uint8_t observer = 0; observer < NODE_COUNT; ++observer)
    {
        snapshot.at(observer) = ActualTwoHop(observer);
    }
    return snapshot;
}

void
RecordGroundTruthChange(const std::string& type,
                        uint8_t node,
                        Vector oldPosition,
                        Vector newPosition,
                        const TruthSnapshot& oldOne,
                        const TruthSnapshot& oldTwo)
{
    const auto newOne = CaptureOneHopTruth();
    const auto newTwo = CaptureTwoHopTruth();
    GroundTruthEvent event;
    event.id = g_groundTruthEvents.size();
    event.timeSeconds = ExperimentTimeSeconds();
    event.type = type;
    event.node = node;
    event.oldPosition = oldPosition;
    event.newPosition = newPosition;

    for (uint8_t observer = 0; observer < NODE_COUNT; ++observer)
    {
        if (!g_radios.at(observer).alive)
        {
            continue;
        }
        const uint32_t oneChanges = SymmetricDifferenceSize(oldOne.at(observer),
                                                             newOne.at(observer));
        const uint32_t twoChanges = SymmetricDifferenceSize(oldTwo.at(observer),
                                                             newTwo.at(observer));
        if (oneChanges > 0)
        {
            event.affectedOneHop.emplace(observer, oneChanges);
        }
        if (twoChanges > 0)
        {
            event.affectedTwoHop.emplace(observer, twoChanges);
        }
    }
    for (uint8_t first = 0; first < NODE_COUNT; ++first)
    {
        for (uint8_t second = first + 1; second < NODE_COUNT; ++second)
        {
            const bool oldLink = oldOne.at(first).count(second) != 0;
            const bool newLink = newOne.at(first).count(second) != 0;
            if (oldLink != newLink)
            {
                ++event.changedLinks;
                g_probePairs[std::make_pair(first, second)] =
                    {first, second, Simulator::Now() + Seconds(4.5)};
            }
        }
    }
    g_groundTruthEvents.push_back(std::move(event));
    ObserveAllLifetimes("GROUND_TRUTH_CHANGE");
}

void
FailNode(uint8_t node)
{
    const auto oldOne = CaptureOneHopTruth();
    const auto oldTwo = CaptureTwoHopTruth();
    const Vector position = g_nodes.Get(node)->GetObject<MobilityModel>()->GetPosition();
    g_radios.at(node).alive = false;
    g_radios.at(node).oneHop.clear();
    g_radios.at(node).twoHopVia.clear();
    g_phys.at(node)->SetOffMode();
    LogTopologyEvent(node, "NODE_FAILED", node, CurrentSuperframe());
    RecordGroundTruthChange("NODE_FAIL", node, position, position, oldOne, oldTwo);
}

void
RecoverNode(uint8_t node)
{
    const auto oldOne = CaptureOneHopTruth();
    const auto oldTwo = CaptureTwoHopTruth();
    const Vector position = g_nodes.Get(node)->GetObject<MobilityModel>()->GetPosition();
    g_radios.at(node).alive = true;
    g_phys.at(node)->ResumeFromOff();
    LogTopologyEvent(node, "NODE_RECOVERED", node, CurrentSuperframe());
    RecordGroundTruthChange("NODE_RECOVER", node, position, position, oldOne, oldTwo);
}

void
MoveNode(uint8_t node, Vector delta)
{
    const auto oldOneTruth = CaptureOneHopTruth();
    const auto oldTwoTruth = CaptureTwoHopTruth();
    Ptr<MobilityModel> mobility = g_nodes.Get(node)->GetObject<MobilityModel>();
    const Vector oldPosition = mobility->GetPosition();
    const Vector newPosition(oldPosition.x + delta.x,
                             oldPosition.y + delta.y,
                             oldPosition.z + delta.z);
    mobility->SetPosition(newPosition);
    LogTopologyEvent(node, "NODE_MOVED", node, CurrentSuperframe());
    RecordGroundTruthChange("NODE_MOVE",
                            node,
                            oldPosition,
                            newPosition,
                            oldOneTruth,
                            oldTwoTruth);
}

double
Mean(const std::vector<double>& values)
{
    return values.empty() ? 0.0
                          : std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double
Percentile(const std::vector<double>& values, double fraction)
{
    if (values.empty())
    {
        return 0.0;
    }
    std::vector<double> ordered(values);
    std::sort(ordered.begin(), ordered.end());
    const double rank = fraction * (ordered.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(rank));
    const auto upper = static_cast<std::size_t>(std::ceil(rank));
    const double weight = rank - lower;
    return ordered.at(lower) * (1.0 - weight) + ordered.at(upper) * weight;
}

bool
TopologyConverged(const TopologySample& sample, bool oneHop)
{
    return oneHop ? sample.exactOneHopNodes == sample.aliveNodes
                  : sample.exactTwoHopNodes == sample.aliveNodes;
}

struct DetectionResult
{
    double firstCorrectDelaySeconds{-1.0};
    double stableCorrectDelaySeconds{-1.0};
};

DetectionResult
FindNodeDetection(std::size_t eventIndex, uint8_t observer, bool oneHop)
{
    const double start = g_groundTruthEvents.at(eventIndex).timeSeconds;
    const double stop = eventIndex + 1 < g_groundTruthEvents.size()
                            ? g_groundTruthEvents.at(eventIndex + 1).timeSeconds
                            : std::numeric_limits<double>::infinity();
    double candidateStart = -1.0;
    DetectionResult result;
    for (const auto& sample : g_nodeTopologySamples)
    {
        if (sample.observer != observer || sample.timeSeconds <= start ||
            sample.timeSeconds >= stop)
        {
            continue;
        }
        const bool exact = oneHop ? sample.exactOneHop : sample.exactTwoHop;
        if (!exact)
        {
            candidateStart = -1.0;
            continue;
        }
        if (result.firstCorrectDelaySeconds < 0.0)
        {
            result.firstCorrectDelaySeconds = sample.timeSeconds - start;
        }
        if (candidateStart < 0.0)
        {
            candidateStart = sample.timeSeconds;
        }
        if (sample.timeSeconds - candidateStart + 1e-9 >=
            g_stableConvergence.GetSeconds())
        {
            result.stableCorrectDelaySeconds = sample.timeSeconds - start;
            return result;
        }
    }
    return result;
}

double
FindFirstDetection(std::size_t eventIndex)
{
    double first = std::numeric_limits<double>::infinity();
    const auto& event = g_groundTruthEvents.at(eventIndex);
    for (const auto& [observer, changed] : event.affectedOneHop)
    {
        (void)changed;
        const auto result = FindNodeDetection(eventIndex, observer, true);
        if (result.firstCorrectDelaySeconds >= 0.0)
        {
            first = std::min(first, result.firstCorrectDelaySeconds);
        }
    }
    for (const auto& [observer, changed] : event.affectedTwoHop)
    {
        (void)changed;
        const auto result = FindNodeDetection(eventIndex, observer, false);
        if (result.firstCorrectDelaySeconds >= 0.0)
        {
            first = std::min(first, result.firstCorrectDelaySeconds);
        }
    }
    return std::isfinite(first) ? first : -1.0;
}

double
FindConvergence(std::size_t eventIndex, bool oneHop)
{
    const auto& affected = oneHop ? g_groundTruthEvents.at(eventIndex).affectedOneHop
                                  : g_groundTruthEvents.at(eventIndex).affectedTwoHop;
    double convergence = 0.0;
    for (const auto& [observer, changed] : affected)
    {
        (void)changed;
        const auto result = FindNodeDetection(eventIndex, observer, oneHop);
        if (result.stableCorrectDelaySeconds < 0.0)
        {
            return -1.0;
        }
        convergence = std::max(convergence, result.stableCorrectDelaySeconds);
    }
    return convergence;
}

double
FindInitialConvergence(bool oneHop)
{
    double candidateStart = -1.0;
    for (const auto& sample : g_topologySamples)
    {
        if (TopologyConverged(sample, oneHop))
        {
            if (candidateStart < 0.0)
            {
                candidateStart = sample.timeSeconds;
            }
            if (sample.timeSeconds - candidateStart + 1e-9 >=
                g_stableConvergence.GetSeconds())
            {
                return sample.timeSeconds;
            }
        }
        else
        {
            candidateStart = -1.0;
        }
    }
    return -1.0;
}

void
WriteResults()
{
    const std::filesystem::path out(g_outputDirectory);
    std::filesystem::create_directories(out);

    uint64_t offered = 0;
    uint64_t accepted = 0;
    uint64_t received = 0;
    uint64_t sourceDead = 0;
    uint64_t blockedMissingLearned = 0;
    uint64_t correctlyBlockedNoLink = 0;
    uint64_t staleNeighborAttempts = 0;
    uint64_t outsideWindow = 0;
    uint64_t socketRejected = 0;
    std::vector<double> queueDelaysMs;
    for (const auto& [bytes, stats] : g_stats.packetSizes)
    {
        (void)bytes;
        offered += stats.offeredPackets;
        accepted += stats.txPackets;
        received += stats.rxPackets;
        sourceDead += stats.sourceDeadDrops;
        blockedMissingLearned += stats.blockedMissingLearned;
        correctlyBlockedNoLink += stats.correctlyBlockedNoLink;
        staleNeighborAttempts += stats.staleNeighborAttempts;
        outsideWindow += stats.outsideWindowDrops;
        socketRejected += stats.socketRejected;
    }
    for (const auto& outcome : g_dataOutcomes)
    {
        if (outcome.queueDelayMilliseconds >= 0.0)
        {
            queueDelaysMs.push_back(outcome.queueDelayMilliseconds);
        }
    }
    uint64_t finalHighMacPending = 0;
    for (const auto& pending : g_pendingData)
    {
        finalHighMacPending += pending.size();
    }
    const std::string topologyClass = g_transport == ControlTransport::ACTION
                                          ? "TOPOLOGY_ACTION"
                                          : "TOPOLOGY_BEACON";
    uint64_t topologyAirtimeNanoseconds = 0;
    uint64_t dataAirtimeNanoseconds = 0;
    uint64_t dataAmsduMpdus = 0;
    uint64_t dataAmsduSubframes = 0;
    uint64_t topologyTxStartedOutsideWindow = 0;
    uint64_t topologyTxCrossedWindowBoundary = 0;
    uint64_t dataPhyAttemptsInControlWindow = 0;
    uint64_t dataPhyAirtimeInControlWindow = 0;
    uint64_t dataPhyAttemptsInEndGuard = 0;
    uint64_t dataTxCrossedSuperframeBoundary = 0;
    const auto topologyAirtimeIt = g_airtimeMonitor.GetStats().find(topologyClass);
    if (topologyAirtimeIt != g_airtimeMonitor.GetStats().end())
    {
        topologyAirtimeNanoseconds = topologyAirtimeIt->second.airtimeNanoseconds;
    }
    const auto dataAirtimeIt = g_airtimeMonitor.GetStats().find("DATA");
    if (dataAirtimeIt != g_airtimeMonitor.GetStats().end())
    {
        dataAirtimeNanoseconds = dataAirtimeIt->second.airtimeNanoseconds;
        dataAmsduMpdus = dataAirtimeIt->second.amsduMpdus;
        dataAmsduSubframes = dataAirtimeIt->second.amsduSubframes;
    }
    for (const auto& event : g_airtimeMonitor.GetEvents())
    {
        if (event.frameClass == "DATA" &&
            event.offsetInSuperframeMicroseconds >= 0 &&
            event.offsetInSuperframeMicroseconds < CONTROL_END.GetMicroSeconds())
        {
            dataPhyAttemptsInControlWindow += event.mpduAttempts;
            dataPhyAirtimeInControlWindow += event.airtimeNanoseconds;
        }
        if (event.frameClass == "DATA")
        {
            const int64_t dataEndMicroseconds =
                event.offsetInSuperframeMicroseconds +
                static_cast<int64_t>(event.airtimeNanoseconds / 1000);
            if (event.offsetInSuperframeMicroseconds >=
                    DATA_INJECTION_END.GetMicroSeconds() &&
                event.offsetInSuperframeMicroseconds < SUPERFRAME.GetMicroSeconds())
            {
                dataPhyAttemptsInEndGuard += event.mpduAttempts;
            }
            if (event.offsetInSuperframeMicroseconds >=
                    DATA_ACTIVE_START.GetMicroSeconds() &&
                event.offsetInSuperframeMicroseconds < SUPERFRAME.GetMicroSeconds() &&
                dataEndMicroseconds > SUPERFRAME.GetMicroSeconds())
            {
                ++dataTxCrossedSuperframeBoundary;
            }
        }
        if (event.frameClass != topologyClass)
        {
            continue;
        }
        const int64_t endMicroseconds = event.offsetInSuperframeMicroseconds +
                                        static_cast<int64_t>(event.airtimeNanoseconds / 1000);
        topologyTxStartedOutsideWindow +=
            event.offsetInSuperframeMicroseconds >= CONTROL_END.GetMicroSeconds() ? 1 : 0;
        topologyTxCrossedWindowBoundary +=
            event.offsetInSuperframeMicroseconds < CONTROL_END.GetMicroSeconds() &&
                    endMicroseconds > CONTROL_END.GetMicroSeconds()
                ? 1
                : 0;
    }
    const uint64_t controlWindows = static_cast<uint64_t>(
        std::ceil(g_duration.GetSeconds() / SUPERFRAME.GetSeconds()));
    const double controlWindowAttemptOccupancy =
        SafeRatio(topologyAirtimeNanoseconds,
                  controlWindows * static_cast<uint64_t>(CONTROL_END.GetNanoSeconds()));

    std::ofstream summary(out / "summary.csv");
    summary << "control_frame,beacon_address,data_address,control_contenders,candidate_rule,membership_epoch,"
               "beacon_rotation_ms,neighbor_remove_misses,neighbor_expire_misses,"
               "neighbor_encoding,density,run,traffic_mode,traffic_pattern,"
               "topology_scenario,"
               "data_queue_policy,amsdu_bytes,mac_queue_admission_limit,"
               "range_m,per_node_mbps,control_tx,control_rx,"
               "topology_tx_bytes,topology_rx_bytes,mac_control_tx_bytes,"
               "mac_control_rx_bytes,list_frames,bitmap_frames,sf_mismatch,"
               "control_loss_rate,control_rx_before_injected_loss,injected_control_drops,"
               "topology_beacon_dequeues_outside_window,topology_tx_started_outside_window,"
               "topology_tx_crossed_window_boundary,data_mac_dequeues_in_control_window,"
               "data_phy_attempts_in_control_window,data_phy_airtime_in_control_us,"
               "data_phy_attempts_in_end_guard,data_tx_crossed_superframe_boundary,"
               "data_hard_gate_deferrals,"
               "control_window_flush_drops,"
               "data_window_flush_drops,data_window_safety_flush_drops,high_mac_enqueued,"
               "high_mac_released,boundary_deferred_observations,max_high_mac_backlog,"
               "final_high_mac_pending,offered_packets,mac_accepted_packets,rx_packets,"
               "blocked_missing_learned,correctly_blocked_no_link,stale_neighbor_attempts,"
               "offered_completion_ratio,mac_pdr,data_mac_dequeued,final_one_hop_f1,"
               "final_two_hop_f1,final_exact_both_ratio,topology_airtime_us,"
               "control_window_attempt_occupancy,mean_queue_delay_ms,p95_queue_delay_ms,"
               "data_airtime_us,data_amsdu_mpdus,data_amsdu_subframes,"
               "airtime_us_per_delivered_kib\n";
    summary << ToString(g_transport) << ',' << g_beaconAddress << ',' << g_dataAddress << ','
            << g_controlContenders << ','
            << "local-affine-permutation" << ',' << g_membershipEpoch << ','
            << g_beaconRotation.GetMilliSeconds() << ',' << g_neighborRemoveMisses << ','
            << g_neighborExpireMisses << ',' << ToString(g_encoding) << ','
            << g_density << ',' << g_run << ',' << g_trafficMode << ',' << g_trafficPattern << ','
            << g_topologyScenario << ','
            << g_dataQueuePolicy << ',' << g_amsduBytes << ',' << g_macQueueAdmissionLimit << ','
            << g_maxRange << ','
            << g_perNodeMbps << ',' << g_stats.controlTx << ',' << g_stats.controlRx << ','
            << g_stats.topologyTxBytes << ',' << g_stats.topologyRxBytes << ','
            << g_stats.macControlTxBytes << ',' << g_stats.macControlRxBytes << ','
            << g_stats.listFrames << ',' << g_stats.bitmapFrames << ','
            << g_stats.superframeMismatch << ',' << g_controlLossRate << ','
            << g_stats.controlRxBeforeInjectedLoss << ',' << g_stats.injectedControlDrops << ','
            << g_stats.controlOutsideWindow << ',' << topologyTxStartedOutsideWindow << ','
            << topologyTxCrossedWindowBoundary << ','
            << g_stats.dataOutsideWindow << ',' << dataPhyAttemptsInControlWindow << ','
            << dataPhyAirtimeInControlWindow / 1000.0 << ','
            << dataPhyAttemptsInEndGuard << ',' << dataTxCrossedSuperframeBoundary << ','
            << g_stats.dataHardGateDeferrals << ','
            << g_stats.controlWindowFlushDrops << ','
            << g_stats.dataWindowFlushDrops << ',' << g_stats.dataWindowSafetyFlushDrops << ','
            << g_stats.highMacEnqueued << ',' << g_stats.highMacReleased << ','
            << g_stats.boundaryDeferredObservations << ',' << g_stats.maxHighMacBacklog << ','
            << finalHighMacPending << ',' << offered << ',' << accepted << ',' << received
            << ',' << blockedMissingLearned << ',' << correctlyBlockedNoLink << ','
            << staleNeighborAttempts << ',' << SafeRatio(received, offered) << ','
            << SafeRatio(received, accepted) << ',' << g_stats.dataMacDequeued << ','
            << (g_topologySamples.empty() ? 0.0 : g_topologySamples.back().oneHopF1) << ','
            << (g_topologySamples.empty() ? 0.0 : g_topologySamples.back().twoHopF1) << ','
            << (g_topologySamples.empty()
                    ? 0.0
                    : SafeRatio(g_topologySamples.back().exactBothNodes,
                                g_topologySamples.back().aliveNodes))
            << ',' << topologyAirtimeNanoseconds / 1000.0 << ','
            << controlWindowAttemptOccupancy << ',' << Mean(queueDelaysMs) << ','
            << Percentile(queueDelaysMs, 0.95) << ',' << dataAirtimeNanoseconds / 1000.0 << ','
            << dataAmsduMpdus << ',' << dataAmsduSubframes << ','
            << (received == 0
                    ? 0.0
                    : (dataAirtimeNanoseconds / 1000.0) /
                          (static_cast<double>(std::accumulate(
                               g_stats.packetSizes.begin(),
                               g_stats.packetSizes.end(),
                               uint64_t{0},
                               [](uint64_t total, const auto& item) {
                                   return total + item.second.rxBytes;
                               })) /
                           1024.0))
            << '\n';

    std::ofstream packetSizes(out / "data-by-packet-size.csv");
    packetSizes << "packet_bytes,offered_packets,source_dead_drops,blocked_missing_learned,"
                   "correctly_blocked_no_link,stale_neighbor_attempts,outside_window_drops,"
                   "socket_rejected,mac_accepted_packets,"
                   "rx_packets,offered_completion_ratio,mac_pdr,tx_bytes,rx_bytes,mean_delay_ms,"
                   "p50_delay_ms,p95_delay_ms,p99_delay_ms\n";
    for (const auto& [bytes, stats] : g_stats.packetSizes)
    {
        packetSizes << bytes << ',' << stats.offeredPackets << ',' << stats.sourceDeadDrops << ','
                    << stats.blockedMissingLearned << ',' << stats.correctlyBlockedNoLink << ','
                    << stats.staleNeighborAttempts << ','
                    << stats.outsideWindowDrops << ',' << stats.socketRejected << ','
                    << stats.txPackets << ',' << stats.rxPackets << ','
                    << SafeRatio(stats.rxPackets, stats.offeredPackets) << ','
                    << SafeRatio(stats.rxPackets, stats.txPackets) << ',' << stats.txBytes << ','
                    << stats.rxBytes << ',' << Mean(stats.delaysMs) << ','
                    << Percentile(stats.delaysMs, 0.50) << ','
                    << Percentile(stats.delaysMs, 0.95) << ','
                    << Percentile(stats.delaysMs, 0.99) << '\n';
    }

    std::ofstream traffic(out / "traffic-flow-summary.csv");
    traffic << "offered_packets,source_dead_drops,blocked_missing_learned,"
               "correctly_blocked_no_link,stale_neighbor_attempts,"
               "outside_window_drops,socket_rejected,mac_accepted_packets,mac_dequeued,"
               "rx_packets,offered_completion_ratio,mac_pdr,mean_queue_delay_ms,"
               "p95_queue_delay_ms,final_high_mac_pending\n";
    traffic << offered << ',' << sourceDead << ',' << blockedMissingLearned << ','
            << correctlyBlockedNoLink << ',' << staleNeighborAttempts << ',' << outsideWindow << ','
            << socketRejected << ','
            << accepted << ',' << g_stats.dataMacDequeued << ',' << received << ','
            << SafeRatio(received, offered) << ',' << SafeRatio(received, accepted) << ','
            << Mean(queueDelaysMs) << ',' << Percentile(queueDelaysMs, 0.95) << ','
            << finalHighMacPending << '\n';

    std::ofstream events(out / "neighbor-events.csv");
    events << "time_s,observer,event,peer,superframe\n";
    for (const auto& event : g_topologyEvents)
    {
        events << std::fixed << std::setprecision(6) << event.timeSeconds << ','
               << +event.observer << ',' << event.event << ',' << +event.peer << ','
               << event.superframe << '\n';
    }

    std::ofstream topology(out / "topology-timeseries.csv");
    topology << "time_s,one_hop_precision,one_hop_recall,one_hop_f1,two_hop_precision,"
                "two_hop_recall,two_hop_f1,macro_one_hop_f1,macro_two_hop_f1,"
                "learned_one_hop,actual_one_hop,learned_two_hop,actual_two_hop,"
                "one_hop_false_positive,one_hop_false_negative,two_hop_false_positive,"
                "two_hop_false_negative,one_hop_error_entries,two_hop_error_entries,"
                "alive_nodes,exact_one_hop_nodes,exact_two_hop_nodes,exact_both_nodes,"
                "exact_one_hop_ratio,exact_two_hop_ratio,exact_both_ratio\n";
    for (const auto& sample : g_topologySamples)
    {
        topology << std::fixed << std::setprecision(6) << sample.timeSeconds << ','
                 << sample.oneHopPrecision << ',' << sample.oneHopRecall << ','
                 << sample.oneHopF1 << ',' << sample.twoHopPrecision << ','
                 << sample.twoHopRecall << ',' << sample.twoHopF1 << ','
                 << sample.macroOneHopF1 << ',' << sample.macroTwoHopF1 << ','
                 << sample.learnedOneHop << ',' << sample.actualOneHop << ','
                 << sample.learnedTwoHop << ',' << sample.actualTwoHop << ','
                 << sample.oneHopFalsePositive << ',' << sample.oneHopFalseNegative << ','
                 << sample.twoHopFalsePositive << ',' << sample.twoHopFalseNegative << ','
                 << sample.oneHopFalsePositive + sample.oneHopFalseNegative << ','
                 << sample.twoHopFalsePositive + sample.twoHopFalseNegative << ','
                 << sample.aliveNodes << ',' << sample.exactOneHopNodes << ','
                 << sample.exactTwoHopNodes << ',' << sample.exactBothNodes << ','
                 << SafeRatio(sample.exactOneHopNodes, sample.aliveNodes) << ','
                 << SafeRatio(sample.exactTwoHopNodes, sample.aliveNodes) << ','
                 << SafeRatio(sample.exactBothNodes, sample.aliveNodes) << '\n';
    }

    std::ofstream nodeMetrics(out / "node-topology-metrics.csv");
    nodeMetrics << "time_s,observer,learned_one_hop,actual_one_hop,one_hop_tp,one_hop_fp,"
                   "one_hop_fn,one_hop_precision,one_hop_recall,one_hop_f1,exact_one_hop,"
                   "learned_two_hop,actual_two_hop,two_hop_tp,two_hop_fp,two_hop_fn,"
                   "two_hop_precision,two_hop_recall,two_hop_f1,exact_two_hop,"
                   "learned_one_hop_ids,actual_one_hop_ids,learned_two_hop_ids,"
                   "actual_two_hop_ids\n";
    for (const auto& sample : g_nodeTopologySamples)
    {
        nodeMetrics << sample.timeSeconds << ',' << +sample.observer << ','
                    << sample.learnedOneHop << ',' << sample.actualOneHop << ','
                    << sample.oneHopTruePositive << ',' << sample.oneHopFalsePositive << ','
                    << sample.oneHopFalseNegative << ',' << sample.oneHopPrecision << ','
                    << sample.oneHopRecall << ',' << sample.oneHopF1 << ','
                    << sample.exactOneHop << ',' << sample.learnedTwoHop << ','
                    << sample.actualTwoHop << ',' << sample.twoHopTruePositive << ','
                    << sample.twoHopFalsePositive << ',' << sample.twoHopFalseNegative << ','
                    << sample.twoHopPrecision << ',' << sample.twoHopRecall << ','
                    << sample.twoHopF1 << ',' << sample.exactTwoHop << ','
                    << sample.learnedOneHopIds << ',' << sample.actualOneHopIds << ','
                    << sample.learnedTwoHopIds << ',' << sample.actualTwoHopIds << '\n';
    }

    std::ofstream groundTruth(out / "topology-events-ground-truth.csv");
    groundTruth << "event_id,time_s,event_type,node,old_x,old_y,new_x,new_y,changed_links,"
                   "affected_one_hop_nodes,affected_two_hop_nodes\n";
    for (std::size_t index = 0; index < g_groundTruthEvents.size(); ++index)
    {
        const auto& event = g_groundTruthEvents.at(index);
        groundTruth << event.id << ',' << event.timeSeconds << ',' << event.type << ',' << +event.node
                    << ',' << event.oldPosition.x << ',' << event.oldPosition.y << ','
                    << event.newPosition.x << ',' << event.newPosition.y << ','
                    << event.changedLinks << ',' << event.affectedOneHop.size() << ','
                    << event.affectedTwoHop.size() << '\n';
    }

    std::ofstream convergence(out / "convergence.csv");
    convergence << "event_id,event_type,event_time_s,first_detection_delay_s,"
                   "one_hop_convergence_delay_s,two_hop_convergence_delay_s\n";
    convergence << "-1,INITIAL,0,0,"
                << FindInitialConvergence(true) << ',' << FindInitialConvergence(false) << '\n';
    for (std::size_t index = 0; index < g_groundTruthEvents.size(); ++index)
    {
        const auto& event = g_groundTruthEvents.at(index);
        convergence << index << ',' << event.type << ',' << event.timeSeconds << ','
                    << FindFirstDetection(index) << ',' << FindConvergence(index, true) << ','
                    << FindConvergence(index, false) << '\n';
    }

    std::vector<double> oneHopSuccessfulConvergence;
    std::vector<double> twoHopSuccessfulConvergence;
    for (std::size_t index = 0; index < g_groundTruthEvents.size(); ++index)
    {
        const double oneHop = FindConvergence(index, true);
        const double twoHop = FindConvergence(index, false);
        if (oneHop >= 0.0)
        {
            oneHopSuccessfulConvergence.push_back(oneHop);
        }
        if (twoHop >= 0.0)
        {
            twoHopSuccessfulConvergence.push_back(twoHop);
        }
    }
    std::ofstream convergenceSummary(out / "convergence-summary.csv");
    convergenceSummary << "hop,total_events,converged_events,convergence_success_ratio,"
                          "mean_successful_convergence_s,max_successful_convergence_s\n";
    const auto writeConvergenceSummary = [&convergenceSummary](
                                             uint32_t hop,
                                             const std::vector<double>& successful) {
        convergenceSummary << hop << ',' << g_groundTruthEvents.size() << ','
                           << successful.size() << ','
                           << SafeRatio(successful.size(), g_groundTruthEvents.size()) << ','
                           << Mean(successful) << ','
                           << (successful.empty()
                                   ? -1.0
                                   : *std::max_element(successful.begin(), successful.end()))
                           << '\n';
    };
    writeConvergenceSummary(1, oneHopSuccessfulConvergence);
    writeConvergenceSummary(2, twoHopSuccessfulConvergence);

    std::ofstream eventDetection(out / "event-detection.csv");
    eventDetection << "event_id,event_type,event_time_s,observer,hop,changed_entries,"
                      "first_correct_delay_s,stable_correct_delay_s\n";
    for (std::size_t index = 0; index < g_groundTruthEvents.size(); ++index)
    {
        const auto& event = g_groundTruthEvents.at(index);
        for (const auto& [observer, changed] : event.affectedOneHop)
        {
            const auto result = FindNodeDetection(index, observer, true);
            eventDetection << event.id << ',' << event.type << ',' << event.timeSeconds << ','
                           << +observer << ",1," << changed << ','
                           << result.firstCorrectDelaySeconds << ','
                           << result.stableCorrectDelaySeconds << '\n';
        }
        for (const auto& [observer, changed] : event.affectedTwoHop)
        {
            const auto result = FindNodeDetection(index, observer, false);
            eventDetection << event.id << ',' << event.type << ',' << event.timeSeconds << ','
                           << +observer << ",2," << changed << ','
                           << result.firstCorrectDelaySeconds << ','
                           << result.stableCorrectDelaySeconds << '\n';
        }
    }

    std::ofstream dataOutcomes(out / "data-outcomes.csv");
    dataOutcomes << "demand_id,offered_time_s,source,destination,packet_bytes,kind,"
                    "actual_neighbor,learned_neighbor,decision,mac_accepted,received,"
                    "first_mac_submit_time_s,queue_delay_ms,receive_time_s,end_to_end_delay_ms\n";
    for (const auto& outcome : g_dataOutcomes)
    {
        dataOutcomes << outcome.demandId << ',' << outcome.offeredTimeSeconds << ','
                     << +outcome.source << ',' << +outcome.destination << ',' << outcome.bytes
                     << ',' << outcome.kind << ',' << outcome.actualNeighbor << ','
                     << outcome.learnedNeighbor << ',' << outcome.decision << ','
                     << outcome.macAccepted << ',' << outcome.received << ','
                     << outcome.firstMacSubmitTimeSeconds << ','
                     << outcome.queueDelayMilliseconds << ',' << outcome.receiveTimeSeconds << ','
                     << outcome.delayMilliseconds << '\n';
    }

    std::ofstream encodings(out / "encoding-decisions.csv");
    encodings << "time_s,node,neighbor_count,requested_encoding,selected_encoding,list_bytes,"
                 "bitmap_bytes,selected_bytes,bytes_saved_vs_list,bytes_saved_vs_bitmap\n";
    for (const auto& decision : g_encodingDecisions)
    {
        encodings << decision.timeSeconds << ',' << +decision.node << ','
                  << decision.neighborCount << ',' << decision.requested << ','
                  << decision.selected << ',' << decision.listBytes << ','
                  << decision.bitmapBytes << ',' << decision.selectedBytes << ','
                  << decision.bytesSavedVsList << ',' << decision.bytesSavedVsBitmap << '\n';
    }

    std::ofstream airtime(out / "airtime-summary.csv");
    airtime << "frame_class,ppdu_attempts,mpdu_attempts,retry_mpdus,mpdu_bytes,airtime_us,"
               "amsdu_mpdus,amsdu_subframes\n";
    for (const auto& [frameClass, stats] : g_airtimeMonitor.GetStats())
    {
        airtime << frameClass << ',' << stats.ppduAttempts << ',' << stats.mpduAttempts << ','
                << stats.retryMpdus << ',' << stats.mpduBytes << ','
                << stats.airtimeNanoseconds / 1000.0 << ',' << stats.amsduMpdus << ','
                << stats.amsduSubframes << '\n';
    }

    std::ofstream airtimeEvents(out / "airtime-events.csv");
    airtimeEvents << "time_s,node,frame_class,mpdu_attempts,retry_mpdus,mpdu_bytes,"
                     "airtime_us,amsdu_mpdus,amsdu_subframes,offset_in_superframe_us,"
                     "in_control_window\n";
    for (const auto& event : g_airtimeMonitor.GetEvents())
    {
        airtimeEvents << event.experimentTimeSeconds << ',' << event.node << ','
                      << event.frameClass << ',' << event.mpduAttempts << ','
                      << event.retryMpdus << ',' << event.mpduBytes << ','
                      << event.airtimeNanoseconds / 1000.0 << ',' << event.amsduMpdus << ','
                      << event.amsduSubframes << ','
                      << event.offsetInSuperframeMicroseconds << ','
                      << (event.offsetInSuperframeMicroseconds >= 0 &&
                          event.offsetInSuperframeMicroseconds <
                              CONTROL_END.GetMicroSeconds())
                      << '\n';
    }

    // Each observation is one serialized MPDU, including those in A-MPDU.
    // Averaging the bytes of an entire PSDU is not a valid frame-size sample.
    std::map<std::pair<std::string, uint64_t>, uint64_t> frameSizeCounts;
    std::ofstream mpduEvents(out / "mpdu-size-events.csv");
    mpduEvents << "time_s,node,frame_class,serialized_mpdu_bytes,retry,amsdu,offset_in_superframe_us\n";
    for (const auto& event : g_airtimeMonitor.GetMpduSizeEvents())
    {
        ++frameSizeCounts[{event.frameClass, event.serializedMpduBytes}];
        mpduEvents << event.experimentTimeSeconds << ',' << event.node << ','
                   << event.frameClass << ',' << event.serializedMpduBytes << ','
                   << event.retry << ',' << event.amsdu << ','
                   << event.offsetInSuperframeMicroseconds << '\n';
    }
    std::ofstream frameSizes(out / "frame-size-distribution.csv");
    frameSizes << "frame_class,serialized_mpdu_bytes,observations,evidence_source\n";
    for (const auto& [key, count] : frameSizeCounts)
    {
        frameSizes << key.first << ',' << key.second << ',' << count
                   << ",PhyTxPsduBegin\n";
    }

    std::ofstream queueBoundary(out / "queue-boundary.csv");
    queueBoundary << "time_s,superframe,node,high_mac_pending,wifi_mac_pending,policy\n";
    for (const auto& sample : g_queueBoundarySamples)
    {
        queueBoundary << sample.timeSeconds << ',' << sample.superframe << ',' << +sample.node
                      << ',' << sample.highMacPending << ',' << sample.wifiMacPending << ','
                      << sample.policy << '\n';
    }

    std::ofstream capacity(out / "control-window-capacity.csv");
    capacity << "superframe,contender_limit,requested,dequeued,deferred,"
                "sender_completion_ratio,expected_receiver_copies,received_copies,"
                "receiver_delivery_ratio\n";
    for (const auto& sample : g_controlWindowSamples)
    {
        capacity << sample.superframe << ',' << sample.contenderLimit << ',' << sample.requested
                 << ',' << sample.dequeued << ',' << sample.deferred << ','
                 << SafeRatio(sample.dequeued, sample.requested) << ','
                 << sample.expectedReceiverCopies << ',' << sample.receivedCopies << ','
                 << SafeRatio(sample.receivedCopies, sample.expectedReceiverCopies) << '\n';
    }

    // Keep every possible peer row, including UNSEEN and EXPIRED.  An expired
    // record remains available for diagnosis, while IsActive() excludes it
    // from Beacon construction and topology computation.
    std::ofstream oneHopFinal(out / "one-hop-table-final.csv");
    oneHopFinal << "observer,peer,state,active,actual_neighbor,correct,last_heard_s,"
                   "last_superframe,missed_rotations,advertised_neighbor_count,"
                   "advertised_neighbor_ids\n";
    for (uint8_t observer = 0; observer < NODE_COUNT; ++observer)
    {
        const auto actual = ActualOneHop(observer);
        for (uint8_t peer = 0; peer < NODE_COUNT; ++peer)
        {
            if (peer == observer)
            {
                continue;
            }
            const auto found = g_radios.at(observer).oneHop.find(peer);
            const OneHopEntry empty;
            const OneHopEntry& entry = found == g_radios.at(observer).oneHop.end()
                                           ? empty
                                           : found->second;
            const bool active = IsActive(entry);
            const bool truth = actual.count(peer) != 0;
            std::ostringstream advertised;
            bool first = true;
            for (const auto neighbor : entry.advertisedNeighbors)
            {
                advertised << (first ? "" : "|") << +neighbor;
                first = false;
            }
            oneHopFinal << +observer << ',' << +peer << ',' << ToString(entry.state) << ','
                        << active << ',' << truth << ',' << (active == truth) << ','
                        << (entry.lastHeard.IsZero()
                                ? -1.0
                                : (entry.lastHeard - g_start).GetSeconds())
                        << ',' << entry.lastSuperframe << ',' << +entry.missedRotations << ','
                        << entry.advertisedNeighbors.size() << ',' << advertised.str() << '\n';
        }
    }

    std::ofstream twoHopFinal(out / "two-hop-table-final.csv");
    twoHopFinal << "observer,target,learned,actual,correct,via_count,via_ids\n";
    for (uint8_t observer = 0; observer < NODE_COUNT; ++observer)
    {
        const auto actual = ActualTwoHop(observer);
        for (uint8_t target = 0; target < NODE_COUNT; ++target)
        {
            if (target == observer)
            {
                continue;
            }
            const auto found = g_radios.at(observer).twoHopVia.find(target);
            const bool learned = found != g_radios.at(observer).twoHopVia.end() &&
                                 !found->second.empty();
            const bool truth = actual.count(target) != 0;
            std::ostringstream via;
            if (found != g_radios.at(observer).twoHopVia.end())
            {
                bool first = true;
                for (const auto node : found->second)
                {
                    via << (first ? "" : "|") << +node;
                    first = false;
                }
            }
            twoHopFinal << +observer << ',' << +target << ',' << learned << ',' << truth << ','
                        << (learned == truth) << ','
                        << (found == g_radios.at(observer).twoHopVia.end() ? 0
                                                                         : found->second.size())
                        << ',' << via.str() << '\n';
        }
    }

    std::ofstream lifetime(out / "node-lifetime.csv");
    lifetime << "start_s,end_s,duration_s,observer,alive,exact_one_hop,exact_two_hop,"
                "exact_both,reason\n";
    for (const auto& interval : g_lifetimeIntervals)
    {
        lifetime << interval.startSeconds << ',' << interval.endSeconds << ','
                 << interval.endSeconds - interval.startSeconds << ',' << +interval.observer << ','
                 << interval.alive << ',' << interval.exactOneHop << ',' << interval.exactTwoHop
                 << ',' << (interval.exactOneHop && interval.exactTwoHop) << ','
                 << interval.reason << '\n';
    }

    std::ofstream lifetimeTransitions(out / "node-lifetime-transitions.csv");
    lifetimeTransitions
        << "time_s,observer,old_initialized,old_alive,old_exact_one_hop,"
           "old_exact_two_hop,new_alive,new_exact_one_hop,new_exact_two_hop,"
           "one_hop_fp,one_hop_fn,two_hop_fp,two_hop_fn,reason\n";
    for (const auto& transition : g_lifetimeTransitions)
    {
        lifetimeTransitions << transition.timeSeconds << ',' << +transition.observer << ','
                            << transition.oldInitialized << ',' << transition.oldAlive << ','
                            << transition.oldExactOneHop << ',' << transition.oldExactTwoHop << ','
                            << transition.newAlive << ',' << transition.newExactOneHop << ','
                            << transition.newExactTwoHop << ','
                            << transition.oneHopFalsePositive << ','
                            << transition.oneHopFalseNegative << ','
                            << transition.twoHopFalsePositive << ','
                            << transition.twoHopFalseNegative << ',' << transition.reason << '\n';
    }

    std::ofstream lifetimeSummary(out / "node-lifetime-summary.csv");
    lifetimeSummary << "observer,alive_duration_s,exact_one_duration_s,exact_two_duration_s,"
                       "exact_both_duration_s,lifetime_exact_both_ratio,error_intervals,"
                       "max_continuous_error_s,cold_start_first_exact_both_s,ever_converged,"
                       "post_cold_alive_duration_s,post_cold_exact_both_duration_s,"
                       "post_cold_exact_both_ratio\n";
    for (uint8_t observer = 0; observer < NODE_COUNT; ++observer)
    {
        double aliveDuration = 0.0;
        double exactOneDuration = 0.0;
        double exactTwoDuration = 0.0;
        double exactBothDuration = 0.0;
        double maxError = 0.0;
        double firstExactBoth = -1.0;
        uint32_t errorIntervals = 0;
        for (const auto& interval : g_lifetimeIntervals)
        {
            if (interval.observer != observer || !interval.alive)
            {
                continue;
            }
            const double duration = interval.endSeconds - interval.startSeconds;
            aliveDuration += duration;
            exactOneDuration += interval.exactOneHop ? duration : 0.0;
            exactTwoDuration += interval.exactTwoHop ? duration : 0.0;
            if (interval.exactOneHop && interval.exactTwoHop)
            {
                exactBothDuration += duration;
                if (firstExactBoth < 0.0)
                {
                    firstExactBoth = interval.startSeconds;
                }
            }
            else
            {
                ++errorIntervals;
                maxError = std::max(maxError, duration);
            }
        }
        double postColdAlive = 0.0;
        double postColdExactBoth = 0.0;
        if (firstExactBoth >= 0.0)
        {
            for (const auto& interval : g_lifetimeIntervals)
            {
                if (interval.observer != observer || !interval.alive ||
                    interval.endSeconds <= firstExactBoth)
                {
                    continue;
                }
                const double start = std::max(interval.startSeconds, firstExactBoth);
                const double duration = interval.endSeconds - start;
                postColdAlive += duration;
                postColdExactBoth +=
                    (interval.exactOneHop && interval.exactTwoHop) ? duration : 0.0;
            }
        }
        lifetimeSummary << +observer << ',' << aliveDuration << ',' << exactOneDuration << ','
                        << exactTwoDuration << ',' << exactBothDuration << ','
                        << (aliveDuration == 0.0 ? 1.0 : exactBothDuration / aliveDuration) << ','
                        << errorIntervals << ',' << maxError << ',' << firstExactBoth << ','
                        << (firstExactBoth >= 0.0) << ',' << postColdAlive << ','
                        << postColdExactBoth << ','
                        << (postColdAlive == 0.0 ? 1.0
                                                : postColdExactBoth / postColdAlive)
                        << '\n';
    }

    std::ofstream node27(out / "node-27-biography.csv");
    node27 << "time_s,learned_one_hop,actual_one_hop,one_hop_fp,one_hop_fn,exact_one_hop,"
              "learned_two_hop,actual_two_hop,two_hop_fp,two_hop_fn,exact_two_hop,"
              "learned_one_hop_ids,actual_one_hop_ids,learned_two_hop_ids,actual_two_hop_ids\n";
    for (const auto& sample : g_nodeTopologySamples)
    {
        if (sample.observer != 27)
        {
            continue;
        }
        node27 << sample.timeSeconds << ',' << sample.learnedOneHop << ',' << sample.actualOneHop
               << ',' << sample.oneHopFalsePositive << ',' << sample.oneHopFalseNegative << ','
               << sample.exactOneHop << ',' << sample.learnedTwoHop << ','
               << sample.actualTwoHop << ',' << sample.twoHopFalsePositive << ','
               << sample.twoHopFalseNegative << ',' << sample.exactTwoHop << ','
               << sample.learnedOneHopIds << ',' << sample.actualOneHopIds << ','
               << sample.learnedTwoHopIds << ',' << sample.actualTwoHopIds << '\n';
    }

    std::ofstream runConfig(out / "run-config.csv");
    runConfig << "control_frame,beacon_address,data_address,control_contenders,candidate_rule,membership_epoch,"
                 "neighbor_encoding,density,run,control_release_mode,control_release_spread_us,neighbor_remove_misses,neighbor_expire_misses,"
                 "traffic_mode,traffic_pattern,"
                 "topology_scenario,"
                 "data_queue_policy,amsdu_bytes,mac_queue_admission_limit,fixed_packet_bytes,data_release_end_ms,"
                 "duration_s,per_node_mbps,range_m,node_count,superframe_ms,control_window_ms,"
                 "control_queue_close_ms,"
                 "beacon_rotation_ms,neighbor_timeout_ms,stable_convergence_ms,"
                 "control_loss_rate,data_measurement_tag_on_air_bytes\n";
    runConfig << ToString(g_transport) << ',' << g_beaconAddress << ',' << g_dataAddress << ','
              << g_controlContenders
              << ",local-affine-permutation," << g_membershipEpoch << ','
              << ToString(g_encoding) << ','
              << g_density << ',' << g_run << ',' << g_controlReleaseMode << ','
              << g_controlReleaseSpreadUs << ','
              << g_neighborRemoveMisses << ','
              << g_neighborExpireMisses << ','
              << g_trafficMode << ',' << g_trafficPattern << ','
              << g_topologyScenario << ','
              << g_dataQueuePolicy << ',' << g_amsduBytes << ',' << g_macQueueAdmissionLimit << ','
              << g_fixedPacketBytes << ','
              << DATA_RELEASE_END.GetMilliSeconds() << ','
              << g_duration.GetSeconds() << ',' << g_perNodeMbps << ',' << g_maxRange << ','
              << NODE_COUNT << ',' << SUPERFRAME.GetMilliSeconds() << ','
              << CONTROL_END.GetMilliSeconds() << ','
              << CONTROL_QUEUE_CLOSE.GetMilliSeconds() << ','
              << g_beaconRotation.GetMilliSeconds() << ','
              << (3 * g_beaconRotation).GetMilliSeconds() << ','
              << g_stableConvergence.GetMilliSeconds() << ',' << g_controlLossRate << ",0\n";
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string controlFrame = "beacon";
    std::string neighborEncoding = "adaptive";
    std::string density = "sparse";
    uint32_t mcs = 3;
    bool pcap = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("controlFrame", "action or beacon", controlFrame);
    cmd.AddValue("beaconAddress",
                 "standard (three 48-bit addresses) or short (experimental 16-bit addresses)",
                 g_beaconAddress);
    cmd.AddValue("dataAddress",
                 "standard (three 48-bit addresses) or short (experimental 16-bit QoS Data)",
                 g_dataAddress);
    cmd.AddValue("controlContenders",
                 "Nominal regular Beacon contenders per control window",
                 g_controlContenders);
    cmd.AddValue("controlReleaseMode",
                 "simultaneous (V7 baseline), staggered (local 0.9 ms order), or jitter",
                 g_controlReleaseMode);
    cmd.AddValue("controlReleaseSpreadUs",
                 "Per-node deterministic Beacon release jitter within the control window; 0 preserves V7 baseline",
                 g_controlReleaseSpreadUs);
    cmd.AddValue("membershipEpoch",
                 "Common membership version used by the distributed candidate rule",
                 g_membershipEpoch);
    cmd.AddValue("neighborRemoveMisses",
                 "Missed complete rotations before a neighbor leaves the active topology",
                 g_neighborRemoveMisses);
    cmd.AddValue("neighborExpireMisses",
                 "Missed complete rotations before a neighbor is marked expired",
                 g_neighborExpireMisses);
    cmd.AddValue("neighborEncoding", "list, bitmap or adaptive", neighborEncoding);
    cmd.AddValue("density", "sparse, medium or dense", density);
    cmd.AddValue("topologyScenario", "random, scripted or static", g_topologyScenario);
    cmd.AddValue("controlLossRate",
                 "Injected topology-frame loss probability in [0,1]",
                 g_controlLossRate);
    cmd.AddValue("perNodeMbps", "Offered layer-2 load per node", g_perNodeMbps);
    cmd.AddValue("trafficPattern", "uniform or burst", g_trafficPattern);
    cmd.AddValue("dataQueuePolicy",
                 "flush (V3 baseline) or defer (V4 high-MAC queue)",
                 g_dataQueuePolicy);
    cmd.AddValue("amsduBytes", "BE A-MSDU maximum size; 0 disables A-MSDU", g_amsduBytes);
    cmd.AddValue("packetBytes", "Fixed application packet size; 0 keeps the 256/1024/1472 B mix",
                 g_fixedPacketBytes);
    cmd.AddValue("macQueueAdmissionLimit",
                 "Maximum AC_BE packets admitted per node by the V4 queue service",
                 g_macQueueAdmissionLimit);
    cmd.AddValue("duration", "Experiment duration in seconds", g_duration);
    cmd.AddValue("mcs", "Fixed HT MCS 0..7", mcs);
    cmd.AddValue("run", "ns-3 RNG run number", g_run);
    cmd.AddValue("pcap", "Write PCAP for nodes 0 and 27", pcap);
    cmd.AddValue("output", "Output directory", g_outputDirectory);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(controlFrame != "beacon", "V7 studies Beacon frames only");
    NS_ABORT_MSG_IF(g_beaconAddress != "standard" && g_beaconAddress != "short",
                    "beaconAddress must be standard or short");
    NS_ABORT_MSG_IF(g_dataAddress != "standard" && g_dataAddress != "short",
                    "dataAddress must be standard or short");
    NS_ABORT_MSG_IF(g_controlContenders == 0 || g_controlContenders > NODE_COUNT,
                    "controlContenders must be in [1,64]");
    NS_ABORT_MSG_IF(g_controlReleaseMode != "simultaneous" &&
                        g_controlReleaseMode != "staggered" &&
                        g_controlReleaseMode != "jitter",
                    "controlReleaseMode must be simultaneous, staggered or jitter");
    NS_ABORT_MSG_IF(g_controlReleaseMode == "staggered" && g_controlContenders > 9,
                    "staggered release supports at most 9 contenders in the 10 ms window");
    NS_ABORT_MSG_IF(g_controlReleaseMode == "jitter" && g_controlReleaseSpreadUs == 0,
                    "jitter release requires a positive controlReleaseSpreadUs");
    NS_ABORT_MSG_IF(g_controlReleaseSpreadUs > 8000,
                    "controlReleaseSpreadUs must be at most 8000 to preserve the control-window guard");
    NS_ABORT_MSG_IF(g_neighborRemoveMisses == 0,
                    "neighborRemoveMisses must be positive");
    NS_ABORT_MSG_IF(g_neighborExpireMisses <= g_neighborRemoveMisses,
                    "neighborExpireMisses must be greater than neighborRemoveMisses");
    NS_ABORT_MSG_IF(neighborEncoding != "list" && neighborEncoding != "bitmap" &&
                        neighborEncoding != "adaptive",
                    "neighborEncoding must be list, bitmap or adaptive");
    NS_ABORT_MSG_IF(density != "sparse" && density != "medium" && density != "dense",
                    "density must be sparse, medium or dense");
    NS_ABORT_MSG_IF(g_topologyScenario != "random" && g_topologyScenario != "scripted" &&
                        g_topologyScenario != "static",
                    "topologyScenario must be random, scripted or static");
    NS_ABORT_MSG_IF(mcs > 7, "mcs must be 0..7");
    NS_ABORT_MSG_IF(g_controlLossRate < 0.0 || g_controlLossRate > 1.0,
                    "controlLossRate must be in [0,1]");
    NS_ABORT_MSG_IF(g_dataQueuePolicy != "flush" && g_dataQueuePolicy != "defer",
                    "dataQueuePolicy must be flush or defer");
    NS_ABORT_MSG_IF(g_trafficPattern != "uniform" && g_trafficPattern != "burst",
                    "trafficPattern must be uniform or burst");
    NS_ABORT_MSG_IF(g_amsduBytes != 0 && g_amsduBytes < 256,
                    "amsduBytes must be 0 or at least 256");
    NS_ABORT_MSG_IF(g_fixedPacketBytes != 0 && g_fixedPacketBytes != 256 &&
                        g_fixedPacketBytes != 1024 && g_fixedPacketBytes != 1472,
                    "packetBytes must be 0, 256, 1024 or 1472");
    NS_ABORT_MSG_IF(g_macQueueAdmissionLimit == 0,
                    "macQueueAdmissionLimit must be positive");

    g_transport = ControlTransport::BEACON;
    WifiMacHeader::SetShortBeaconAddressesEnabled(g_beaconAddress == "short");
    WifiMacHeader::SetShortDataAddressesEnabled(g_dataAddress == "short");
    WifiMacHeader::ClearShortAddressMappings();
    QosFrameExchangeManager::SetExternalTxGate(MakeCallback(&AllowMacTransmission));
    const uint32_t rotationWindows =
        (NODE_COUNT + g_controlContenders - 1) / g_controlContenders;
    g_beaconRotation = SUPERFRAME * rotationWindows;
    g_stableConvergence = g_beaconRotation;
    if (neighborEncoding == "list")
    {
        g_encoding = NeighborEncoding::LIST;
    }
    else if (neighborEncoding == "bitmap")
    {
        g_encoding = NeighborEncoding::BITMAP;
    }
    else
    {
        g_encoding = NeighborEncoding::ADAPTIVE;
    }
    g_density = density;
    g_maxRange = density == "sparse" ? 36.0 : (density == "medium" ? 46.0 : 65.0);

    RngSeedManager::SetSeed(20260923);
    RngSeedManager::SetRun(g_run);
    g_packetKindRng = CreateObject<UniformRandomVariable>();
    g_packetKindRng->SetStream(10001);
    g_jitterRng = CreateObject<UniformRandomVariable>();
    g_jitterRng->SetStream(10002);
    g_destinationRng = CreateObject<UniformRandomVariable>();
    g_destinationRng->SetStream(10003);

    g_nodes.Create(NODE_COUNT);

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RangePropagationLossModel",
                               "MaxRange",
                               DoubleValue(g_maxRange));

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("ChannelSettings", StringValue("{36, 20, BAND_5GHZ, 0}"));
    phy.Set("TxPowerStart", DoubleValue(16.0));
    phy.Set("TxPowerEnd", DoubleValue(16.0));
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("HtMcs" + std::to_string(mcs)),
                                 "ControlMode",
                                 StringValue("OfdmRate6Mbps"));

    WifiMacHelper macHelper;
    macHelper.SetType("ns3::AdhocWifiMac", "QosSupported", BooleanValue(true));
    g_devices = wifi.Install(phy, macHelper, g_nodes);
    WifiHelper::AssignStreams(g_devices, 1);

    // Pre-joined experiment: IDs are assigned once and never recycled.  The
    // translation is internal to the codec, not an extra over-the-air field.
    for (uint32_t node = 0; node < NODE_COUNT; ++node)
    {
        WifiMacHeader::RegisterShortAddress(
            Mac48Address::ConvertFrom(g_devices.Get(node)->GetAddress()),
            static_cast<uint16_t>(node + 1));
    }
    WifiMacHeader::RegisterShortAddress(Mac48Address("02:00:00:00:ff:01"), 0xfffe);

    // Exercise the actual ns-3 Header::Serialize/Deserialize path, not only
    // GetSize() or a hand-calculated frame-length formula.
    for (const WifiMacType type : {WIFI_MAC_MGT_BEACON, WIFI_MAC_QOSDATA})
    {
        WifiMacHeader original(type);
        original.SetAddr1(Mac48Address::GetBroadcast());
        original.SetAddr2(Mac48Address::ConvertFrom(g_devices.Get(0)->GetAddress()));
        original.SetAddr3(Mac48Address("02:00:00:00:ff:01"));
        if (type == WIFI_MAC_QOSDATA)
        {
            original.SetQosTid(0);
        }
        Ptr<Packet> serialized = Create<Packet>();
        serialized->AddHeader(original);
        const uint32_t expected = type == WIFI_MAC_MGT_BEACON
                                      ? (g_beaconAddress == "short" ? 12 : 24)
                                      : (g_dataAddress == "short" ? 14 : 26);
        NS_ABORT_MSG_IF(serialized->GetSize() != expected,
                        "V7 codec header length mismatch for frame type " << type);
        WifiMacHeader restored;
        serialized->RemoveHeader(restored);
        NS_ABORT_MSG_IF(serialized->GetSize() != 0 ||
                            restored.GetAddr1() != original.GetAddr1() ||
                            restored.GetAddr2() != original.GetAddr2() ||
                            restored.GetAddr3() != original.GetAddr3(),
                        "V7 codec address round-trip failed for frame type " << type);
    }

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    for (uint32_t node = 0; node < NODE_COUNT; ++node)
    {
        positions->Add(Vector((node % 8) * g_spacing, (node / 8) * g_spacing, 0));
    }
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(g_nodes);

    const Mac48Address commonBssid("02:00:00:00:ff:01");
    for (uint32_t node = 0; node < NODE_COUNT; ++node)
    {
        Ptr<WifiNetDevice> device = DynamicCast<WifiNetDevice>(g_devices.Get(node));
        Ptr<AdhocWifiMac> mac = DynamicCast<AdhocWifiMac>(device->GetMac());
        NS_ABORT_MSG_IF(!mac, "Expected AdhocWifiMac");
        mac->SetAttribute("BE_MaxAmsduSize", UintegerValue(g_amsduBytes));
        mac->SetBssid(commonBssid, 0);
        g_macs.push_back(mac);
        g_phys.push_back(device->GetPhy(0));
        device->GetPhy(0)->TraceConnectWithoutContext(
            "PhyTxPsduBegin",
            MakeBoundCallback(&PhyTxPsduBegin, node));
        mac->TraceConnectWithoutContext("VendorSpecificActionRx",
                                        MakeBoundCallback(&ReceiveAction, node));
        mac->TraceConnectWithoutContext("TopologyBeaconRx",
                                        MakeBoundCallback(&ReceiveBeacon, node));
        mac->GetQosTxop(AC_VO)->GetWifiMacQueue()->TraceConnectWithoutContext(
            "Dequeue",
            MakeBoundCallback(&QueueDequeue, node));
        mac->GetQosTxop(AC_BE)->GetWifiMacQueue()->TraceConnectWithoutContext(
            "Dequeue",
            MakeBoundCallback(&QueueDequeue, node));
    }

    // Layer-2 data sockets only: no InternetStackHelper, IP, UDP, ARP or routing.
    PacketSocketHelper packetSocket;
    packetSocket.Install(g_nodes);
    g_sockets.resize(NODE_COUNT);
    for (uint32_t node = 0; node < NODE_COUNT; ++node)
    {
        Ptr<Socket> socket = Socket::CreateSocket(g_nodes.Get(node),
                                                   PacketSocketFactory::GetTypeId());
        PacketSocketAddress local;
        local.SetSingleDevice(g_devices.Get(node)->GetIfIndex());
        local.SetPhysicalAddress(g_devices.Get(node)->GetAddress());
        local.SetProtocol(DATA_PROTOCOL);
        NS_ABORT_MSG_IF(socket->Bind(local) < 0, "PacketSocket bind failed");
        socket->SetRecvCallback(MakeBoundCallback(&ReceiveData, node));
        g_sockets[node] = socket;
    }

    const uint64_t superframes = static_cast<uint64_t>(
        std::ceil(g_duration.GetSeconds() / SUPERFRAME.GetSeconds()));
    for (uint64_t sf = 0; sf < superframes; ++sf)
    {
        const Time base = g_start + SUPERFRAME * sf;
        Simulator::Schedule(base, &ScheduleControlWindow, sf);
        Simulator::Schedule(base + DATA_ACTIVE_START, &ScheduleDataWindow);
        Simulator::Schedule(base + DATA_RELEASE_END, &FreezeDataEdca);
        Simulator::Schedule(base + MilliSeconds(99), &AgeNeighbors);
    }

    for (Time offset = MilliSeconds(0); offset < g_duration;
         offset += TOPOLOGY_SAMPLE_PERIOD)
    {
        Simulator::Schedule(g_start + offset + MicroSeconds(1),
                            &SampleTopologyAndLifetime);
    }

    TopologyScenarioPlanner scenarioPlanner(NODE_COUNT, 8, g_spacing);
    const TopologyScenarioPlan scenario = g_topologyScenario == "random"
                                              ? scenarioPlanner.BuildRandom()
                                              : scenarioPlanner.BuildScripted();
    if (g_topologyScenario != "static")
    {
        for (const auto& event : scenario.events)
        {
            if (event.type == ScenarioEventType::FAIL)
            {
                Simulator::Schedule(g_start + event.offset, &FailNode, event.node);
            }
            else if (event.type == ScenarioEventType::RECOVER)
            {
                Simulator::Schedule(g_start + event.offset, &RecoverNode, event.node);
            }
            else
            {
                Simulator::Schedule(g_start + event.offset,
                                    &MoveNode,
                                    event.node,
                                    event.moveDelta);
            }
        }
    }

    if (pcap)
    {
        const std::filesystem::path out(g_outputDirectory);
        std::filesystem::create_directories(out);
        phy.EnablePcap((out / "node0").string(), g_devices.Get(0));
        phy.EnablePcap((out / "node27").string(), g_devices.Get(27));
    }

    Simulator::Stop(g_start + g_duration + Seconds(0.5));
    Simulator::Run();
    QosFrameExchangeManager::ClearExternalTxGate();
    FinalizeLifetimeIntervals();
    WriteResults();

    std::cout << "[CONFIG] v7 64-node IEEE 802.11n MAC, control=" << ToString(g_transport)
              << ", beaconAddress=" << g_beaconAddress
              << ", dataAddress=" << g_dataAddress
              << ", contenders=" << g_controlContenders
              << ", encoding=" << ToString(g_encoding) << ", density=" << g_density
              << ", traffic=" << g_trafficMode << '/' << g_trafficPattern
              << ", scenario=" << g_topologyScenario
              << ", queuePolicy=" << g_dataQueuePolicy << ", amsduBytes=" << g_amsduBytes
              << ", controlLoss=" << g_controlLossRate << ", range=" << g_maxRange
              << " m\n[CONTROL] tx=" << g_stats.controlTx << " rx=" << g_stats.controlRx
              << " topologyBytes=" << g_stats.topologyTxBytes
              << " macBytes=" << g_stats.macControlTxBytes << " list=" << g_stats.listFrames
              << " bitmap=" << g_stats.bitmapFrames
              << "\n[OUTPUT] " << g_outputDirectory << std::endl;

    Simulator::Destroy();
    return 0;
}
