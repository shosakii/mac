/*
 * Copyright (c) 2006, 2009 INRIA
 * Copyright (c) 2009 MIRKO BANCHI
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 *          Mirko Banchi <mk.banchi@gmail.com>
 */

#ifndef ADHOC_WIFI_MAC_H
#define ADHOC_WIFI_MAC_H

#include "wifi-mac.h"

#include "ns3/traced-callback.h"

namespace ns3
{

/**
 * @ingroup wifi
 *
 * @brief Wifi MAC high model for an ad-hoc Wifi MAC
 */
class AdhocWifiMac : public WifiMac
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    AdhocWifiMac();
    ~AdhocWifiMac() override;

    void SetLinkUpCallback(Callback<void> linkUp) override;
    bool CanForwardPacketsTo(Mac48Address to) const override;

    /**
     * Queue a vendor-specific Action management frame in ad-hoc mode.
     * The packet body must start with an IEEE 802.11 Action category field
     * whose value is 127 (Vendor Specific).
     */
    void SendVendorSpecificAction(Ptr<Packet> packet, Mac48Address to);

    /**
     * Queue a genuine Beacon management frame in ad-hoc mode.
     * topologyIe begins with the simulation-private IE id and length.
     */
    void SendTopologyBeacon(Ptr<Packet> topologyIe, Time beaconInterval);

    /** Callback signature for received vendor-specific Action frames. */
    typedef void (*VendorSpecificActionRxCallback)(Ptr<const Packet> packet,
                                                   Mac48Address from,
                                                   Mac48Address to);

    /** Callback signature for the private topology IE carried by a Beacon. */
    typedef void (*TopologyBeaconRxCallback)(Ptr<const Packet> topologyIe,
                                             Mac48Address from,
                                             Mac48Address to);

  private:
    void Receive(Ptr<const WifiMpdu> mpdu, uint8_t linkId) override;
    void DoCompleteConfig() override;
    void Enqueue(Ptr<WifiMpdu> mpdu, Mac48Address to, Mac48Address from) override;

    TracedCallback<Ptr<const Packet>, Mac48Address, Mac48Address>
        m_vendorSpecificActionRxTrace;
    TracedCallback<Ptr<const Packet>, Mac48Address, Mac48Address>
        m_topologyBeaconRxTrace;
};

} // namespace ns3

#endif /* ADHOC_WIFI_MAC_H */
