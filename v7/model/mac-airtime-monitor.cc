#include "mac-airtime-monitor.h"

#include "ns3/wifi-mac-header.h"
#include "ns3/wifi-mpdu.h"

#include <array>
#include <iterator>
#include <map>

namespace ns3
{

std::string
MacAirtimeMonitor::Classify(Ptr<const WifiMpdu> mpdu)
{
    const WifiMacHeader& header = mpdu->GetHeader();
    if (header.IsBeacon())
    {
        return "TOPOLOGY_BEACON";
    }
    if (header.IsAction() || header.IsActionNoAck())
    {
        std::array<uint8_t, 2> prefix{};
        if (mpdu->GetPacketSize() >= prefix.size())
        {
            mpdu->GetPacket()->CopyData(prefix.data(), prefix.size());
            if (prefix[0] == 127 && prefix[1] == 1)
            {
                return "TOPOLOGY_ACTION";
            }
        }
        return "OTHER_ACTION";
    }
    if (header.IsData())
    {
        return "DATA";
    }
    if (header.IsCtl())
    {
        return "OTHER_CONTROL";
    }
    if (header.IsMgt())
    {
        return "OTHER_MANAGEMENT";
    }
    return "OTHER";
}

void
MacAirtimeMonitor::Observe(uint32_t node,
                           double experimentTimeSeconds,
                           int64_t offsetInSuperframeMicroseconds,
                           WifiConstPsduMap psduMap,
                           const WifiTxVector& txVector,
                           WifiPhyBand band)
{
    std::map<std::string, uint64_t> bytesByClass;
    std::map<std::string, uint64_t> mpdusByClass;
    std::map<std::string, uint64_t> retriesByClass;
    std::map<std::string, uint64_t> amsduMpdusByClass;
    std::map<std::string, uint64_t> amsduSubframesByClass;
    uint64_t totalBytes = 0;

    for (const auto& [staId, psdu] : psduMap)
    {
        (void)staId;
        for (const auto& mpdu : *psdu)
        {
            const std::string frameClass = Classify(mpdu);
            const uint64_t bytes = mpdu->GetSize();
            m_mpduSizeEvents.push_back({experimentTimeSeconds,
                                       node,
                                       frameClass,
                                       static_cast<uint32_t>(bytes),
                                       mpdu->GetHeader().IsRetry(),
                                       mpdu->GetHeader().IsData() &&
                                           mpdu->GetHeader().IsQosAmsdu(),
                                       offsetInSuperframeMicroseconds});
            bytesByClass[frameClass] += bytes;
            ++mpdusByClass[frameClass];
            retriesByClass[frameClass] += mpdu->GetHeader().IsRetry() ? 1 : 0;
            if (mpdu->GetHeader().IsData() && mpdu->GetHeader().IsQosAmsdu())
            {
                ++amsduMpdusByClass[frameClass];
                amsduSubframesByClass[frameClass] +=
                    static_cast<uint64_t>(std::distance(mpdu->begin(), mpdu->end()));
            }
            totalBytes += bytes;
        }
    }

    const uint64_t airtimeNs = static_cast<uint64_t>(
        WifiPhy::CalculateTxDuration(psduMap, txVector, band).GetNanoSeconds());
    for (const auto& [frameClass, bytes] : bytesByClass)
    {
        auto& stats = m_stats[frameClass];
        ++stats.ppduAttempts;
        stats.mpduAttempts += mpdusByClass[frameClass];
        stats.retryMpdus += retriesByClass[frameClass];
        stats.mpduBytes += bytes;
        stats.airtimeNanoseconds +=
            totalBytes == 0 ? 0 : static_cast<uint64_t>(airtimeNs * bytes / totalBytes);
        stats.amsduMpdus += amsduMpdusByClass[frameClass];
        stats.amsduSubframes += amsduSubframesByClass[frameClass];
        m_events.push_back({experimentTimeSeconds,
                            node,
                            frameClass,
                            mpdusByClass[frameClass],
                            retriesByClass[frameClass],
                            bytes,
                            totalBytes == 0
                                ? 0
                                : static_cast<uint64_t>(airtimeNs * bytes / totalBytes),
                            amsduMpdusByClass[frameClass],
                            amsduSubframesByClass[frameClass],
                            offsetInSuperframeMicroseconds});
    }
}

const std::vector<AirtimeEvent>&
MacAirtimeMonitor::GetEvents() const
{
    return m_events;
}

const std::vector<MpduSizeEvent>&
MacAirtimeMonitor::GetMpduSizeEvents() const
{
    return m_mpduSizeEvents;
}

const std::map<std::string, AirtimeClassStats>&
MacAirtimeMonitor::GetStats() const
{
    return m_stats;
}

} // namespace ns3
