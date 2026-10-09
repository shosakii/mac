#ifndef MAC_AIRTIME_MONITOR_H
#define MAC_AIRTIME_MONITOR_H

#include "ns3/wifi-phy.h"
#include "ns3/wifi-mpdu.h"
#include "ns3/wifi-psdu.h"
#include "ns3/wifi-tx-vector.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ns3
{

struct AirtimeClassStats
{
    uint64_t ppduAttempts{0};
    uint64_t mpduAttempts{0};
    uint64_t retryMpdus{0};
    uint64_t mpduBytes{0};
    uint64_t airtimeNanoseconds{0};
    uint64_t amsduMpdus{0};
    uint64_t amsduSubframes{0};
};

struct AirtimeEvent
{
    double experimentTimeSeconds{0.0};
    uint32_t node{0};
    std::string frameClass;
    uint64_t mpduAttempts{0};
    uint64_t retryMpdus{0};
    uint64_t mpduBytes{0};
    uint64_t airtimeNanoseconds{0};
    uint64_t amsduMpdus{0};
    uint64_t amsduSubframes{0};
    int64_t offsetInSuperframeMicroseconds{0};
};

struct MpduSizeEvent
{
    double experimentTimeSeconds{0.0};
    uint32_t node{0};
    std::string frameClass;
    uint32_t serializedMpduBytes{0};
    bool retry{false};
    bool amsdu{false};
    int64_t offsetInSuperframeMicroseconds{0};
};

/** Records actual PSDU bytes, retries and calculated PHY airtime. */
class MacAirtimeMonitor
{
  public:
    void Observe(uint32_t node,
                 double experimentTimeSeconds,
                 int64_t offsetInSuperframeMicroseconds,
                 WifiConstPsduMap psduMap,
                 const WifiTxVector& txVector,
                 WifiPhyBand band);

    const std::map<std::string, AirtimeClassStats>& GetStats() const;
    const std::vector<AirtimeEvent>& GetEvents() const;
    const std::vector<MpduSizeEvent>& GetMpduSizeEvents() const;

  private:
    static std::string Classify(Ptr<const WifiMpdu> mpdu);

    std::map<std::string, AirtimeClassStats> m_stats;
    std::vector<AirtimeEvent> m_events;
    std::vector<MpduSizeEvent> m_mpduSizeEvents;
};

} // namespace ns3

#endif
