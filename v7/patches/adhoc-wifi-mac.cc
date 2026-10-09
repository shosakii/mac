/*
 * Copyright (c) 2006, 2009 INRIA
 * Copyright (c) 2009 MIRKO BANCHI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 *          Mirko Banchi <mk.banchi@gmail.com>
 */

#include "adhoc-wifi-mac.h"

#include "ht/ht-operation.h"
#include "mgt-action-headers.h"
#include "mgt-headers.h"
#include "qos-txop.h"
#include "supported-rates.h"

#include "ns3/eht-capabilities.h"
#include "ns3/he-capabilities.h"
#include "ns3/ht-capabilities.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/vht-capabilities.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("AdhocWifiMac");

NS_OBJECT_ENSURE_REGISTERED(AdhocWifiMac);

TypeId
AdhocWifiMac::GetTypeId()
{
    static TypeId tid = TypeId("ns3::AdhocWifiMac")
                            .SetParent<WifiMac>()
                            .SetGroupName("Wifi")
                            .AddConstructor<AdhocWifiMac>()
                            .AddTraceSource(
                                "VendorSpecificActionRx",
                                "A vendor-specific Action management frame received by an "
                                "ad-hoc station.",
                                MakeTraceSourceAccessor(
                                    &AdhocWifiMac::m_vendorSpecificActionRxTrace),
                                "ns3::AdhocWifiMac::VendorSpecificActionRxCallback")
                            .AddTraceSource(
                                "TopologyBeaconRx",
                                "A topology Information Element received in a Beacon frame.",
                                MakeTraceSourceAccessor(&AdhocWifiMac::m_topologyBeaconRxTrace),
                                "ns3::AdhocWifiMac::TopologyBeaconRxCallback");
    return tid;
}

AdhocWifiMac::AdhocWifiMac()
{
    NS_LOG_FUNCTION(this);
    // Let the lower layers know that we are acting in an IBSS
    SetTypeOfStation(ADHOC_STA);
}

AdhocWifiMac::~AdhocWifiMac()
{
    NS_LOG_FUNCTION(this);
}

void
AdhocWifiMac::DoCompleteConfig()
{
    NS_LOG_FUNCTION(this);
}

bool
AdhocWifiMac::CanForwardPacketsTo(Mac48Address to) const
{
    return true;
}

void
AdhocWifiMac::SendVendorSpecificAction(Ptr<Packet> packet, Mac48Address to)
{
    NS_LOG_FUNCTION(this << packet << to);
    NS_ABORT_MSG_IF(!packet || packet->GetSize() < 2,
                    "A vendor-specific Action body must contain category and action fields");

    WifiActionHeader action;
    packet->PeekHeader(action);
    NS_ABORT_MSG_IF(action.GetCategory() != WifiActionHeader::VENDOR_SPECIFIC_ACTION,
                    "The Action category must be Vendor Specific (127)");

    WifiMacHeader hdr(WIFI_MAC_MGT_ACTION);
    Ptr<WifiMpdu> mpdu = Create<WifiMpdu>(packet, hdr);
    Enqueue(mpdu, to, GetAddress());
}

void
AdhocWifiMac::SendTopologyBeacon(Ptr<Packet> topologyIe, Time beaconInterval)
{
    NS_LOG_FUNCTION(this << topologyIe << beaconInterval);
    NS_ABORT_MSG_IF(!topologyIe || topologyIe->GetSize() < 5,
                    "Topology Beacon must carry a complete topology IE");

    // Construct the minimum 802.11n Beacon body used by this experiment.
    // The private topology IE remains packet payload and therefore follows
    // all standard Information Elements serialized by MgtBeaconHeader.
    MgtBeaconHeader beacon;
    beacon.Get<Ssid>() = Ssid("");

    AllSupportedRates rates;
    for (const uint64_t rate : {6000000ULL,
                                9000000ULL,
                                12000000ULL,
                                18000000ULL,
                                24000000ULL,
                                36000000ULL,
                                48000000ULL,
                                54000000ULL})
    {
        rates.AddSupportedRate(rate);
    }
    rates.SetBasicRate(6000000);
    rates.SetBasicRate(12000000);
    rates.SetBasicRate(24000000);
    beacon.Get<SupportedRates>() = rates.rates;
    beacon.SetBeaconIntervalUs(beaconInterval.GetMicroSeconds());
    beacon.Capabilities().SetIbss();
    beacon.Capabilities().SetShortSlotTime(true);

    if (GetHtSupported(SINGLE_LINK_OP_ID))
    {
        beacon.Get<HtCapabilities>() = GetHtCapabilities(SINGLE_LINK_OP_ID);
        HtOperation operation;
        operation.SetPrimaryChannel(36);
        operation.SetStaChannelWidth(0); // 20 MHz
        operation.SetHtProtection(NO_PROTECTION);
        operation.SetBasicMcsSet(1, 0); // MCS 0 is the basic HT MCS
        beacon.Get<HtOperation>() = operation;
    }
    // ns-3.47 does not model the IBSS Parameter Set IE in MgtBeaconHeader.
    // Add the standard four-octet IE explicitly: Element ID 6, length 2,
    // ATIM Window 0.  This makes the V5 standard Beacon baseline 108 bytes
    // before the private topology IE and FCS.
    const uint8_t ibssParameterSet[4] = {6, 2, 0, 0};
    Ptr<Packet> body = Create<Packet>(ibssParameterSet, sizeof(ibssParameterSet));
    body->AddAtEnd(topologyIe);
    topologyIe = body;
    topologyIe->AddHeader(beacon);

    WifiMacHeader hdr(WIFI_MAC_MGT_BEACON);
    hdr.SetAddr1(Mac48Address::GetBroadcast());
    hdr.SetAddr2(GetAddress());
    hdr.SetAddr3(GetBssid(SINGLE_LINK_OP_ID));
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();
    auto txop = GetQosSupported() ? StaticCast<Txop>(GetQosTxop(AC_VO)) : GetTxop();
    NS_ASSERT(txop);
    txop->Queue(Create<WifiMpdu>(topologyIe, hdr));
}

void
AdhocWifiMac::Enqueue(Ptr<WifiMpdu> mpdu, Mac48Address to, Mac48Address from)
{
    NS_LOG_FUNCTION(this << *mpdu << to << from);

    if (GetWifiRemoteStationManager()->IsBrandNew(to))
    {
        // In ad hoc mode, we assume that every destination supports all the rates we support.
        if (GetHtSupported(SINGLE_LINK_OP_ID))
        {
            GetWifiRemoteStationManager()->AddAllSupportedMcs(to);
            GetWifiRemoteStationManager()->AddStationHtCapabilities(
                to,
                GetHtCapabilities(SINGLE_LINK_OP_ID));
        }
        if (GetVhtSupported(SINGLE_LINK_OP_ID))
        {
            GetWifiRemoteStationManager()->AddStationVhtCapabilities(
                to,
                GetVhtCapabilities(SINGLE_LINK_OP_ID));
        }
        if (GetHeSupported())
        {
            GetWifiRemoteStationManager()->AddStationHeCapabilities(
                to,
                GetHeCapabilities(SINGLE_LINK_OP_ID));
            if (Is6GhzBand(SINGLE_LINK_OP_ID))
            {
                GetWifiRemoteStationManager()->AddStationHe6GhzCapabilities(
                    to,
                    GetHe6GhzBandCapabilities(SINGLE_LINK_OP_ID));
            }
        }
        if (GetEhtSupported())
        {
            GetWifiRemoteStationManager()->AddStationEhtCapabilities(
                to,
                GetEhtCapabilities(SINGLE_LINK_OP_ID));
        }
        GetWifiRemoteStationManager()->AddAllSupportedModes(to);
        GetWifiRemoteStationManager()->RecordDisassociated(to);
    }

    auto& hdr = mpdu->GetHeader();

    hdr.SetAddr1(to);
    hdr.SetAddr2(GetAddress());
    hdr.SetAddr3(GetBssid(SINGLE_LINK_OP_ID));
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();

    auto txop = hdr.IsQosData()
                    ? StaticCast<Txop>(GetQosTxop(hdr.GetQosTid()))
                    : (hdr.IsMgt() && GetQosSupported() ? StaticCast<Txop>(GetQosTxop(AC_VO))
                                                        : GetTxop());
    NS_ASSERT(txop);
    txop->Queue(mpdu);
}

void
AdhocWifiMac::SetLinkUpCallback(Callback<void> linkUp)
{
    NS_LOG_FUNCTION(this << &linkUp);
    WifiMac::SetLinkUpCallback(linkUp);

    // The approach taken here is that, from the point of view of a STA
    // in IBSS mode, the link is always up, so we immediately invoke the
    // callback if one is set
    linkUp();
}

void
AdhocWifiMac::Receive(Ptr<const WifiMpdu> mpdu, uint8_t linkId)
{
    NS_LOG_FUNCTION(this << *mpdu << +linkId);
    const WifiMacHeader* hdr = &mpdu->GetHeader();
    NS_ASSERT(!hdr->IsCtl());
    Mac48Address from = hdr->GetAddr2();
    Mac48Address to = hdr->GetAddr1();
    if (GetWifiRemoteStationManager()->IsBrandNew(from))
    {
        // In ad hoc mode, we assume that every destination supports all the rates we support.
        if (GetHtSupported(SINGLE_LINK_OP_ID))
        {
            GetWifiRemoteStationManager()->AddAllSupportedMcs(from);
            GetWifiRemoteStationManager()->AddStationHtCapabilities(
                from,
                GetHtCapabilities(SINGLE_LINK_OP_ID));
        }
        if (GetVhtSupported(SINGLE_LINK_OP_ID))
        {
            GetWifiRemoteStationManager()->AddStationVhtCapabilities(
                from,
                GetVhtCapabilities(SINGLE_LINK_OP_ID));
        }
        if (GetHeSupported())
        {
            GetWifiRemoteStationManager()->AddStationHeCapabilities(
                from,
                GetHeCapabilities(SINGLE_LINK_OP_ID));
        }
        if (GetEhtSupported())
        {
            GetWifiRemoteStationManager()->AddStationEhtCapabilities(
                from,
                GetEhtCapabilities(SINGLE_LINK_OP_ID));
        }
        GetWifiRemoteStationManager()->AddAllSupportedModes(from);
        GetWifiRemoteStationManager()->RecordDisassociated(from);
    }
    if (hdr->IsData())
    {
        if (hdr->IsQosData() && hdr->IsQosAmsdu())
        {
            NS_LOG_DEBUG("Received A-MSDU from" << from);
            DeaggregateAmsduAndForward(mpdu);
        }
        else
        {
            ForwardUp(mpdu->GetPacket(), from, to);
        }
        return;
    }

    if (hdr->IsBeacon())
    {
        Ptr<Packet> remaining = mpdu->GetPacket()->Copy();
        MgtBeaconHeader beacon;
        const uint32_t removed = remaining->RemoveHeader(beacon);
        if (removed > 0 && remaining->GetSize() >= 4)
        {
            uint8_t prefix[4]{};
            remaining->CopyData(prefix, sizeof(prefix));
            if (prefix[0] == 6 && prefix[1] == 2)
            {
                remaining->RemoveAtStart(4);
            }
            // Known standard IEs were consumed by MgtBeaconHeader. The private
            // topology IE is deliberately left as the remaining packet body.
            if (remaining->GetSize() >= 14)
            {
                m_topologyBeaconRxTrace(remaining, from, to);
            }
        }
        return;
    }

    if (hdr->IsAction() && mpdu->GetPacket()->GetSize() >= 2)
    {
        WifiActionHeader action;
        mpdu->GetPacket()->PeekHeader(action);
        if (action.GetCategory() == WifiActionHeader::VENDOR_SPECIFIC_ACTION)
        {
            m_vendorSpecificActionRxTrace(mpdu->GetPacket(), from, to);
            return;
        }
    }

    // Invoke the receive handler of our parent class to deal with any other frames
    WifiMac::Receive(mpdu, linkId);
}

} // namespace ns3
