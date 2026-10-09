#ifndef LOGICAL_CLOCK_H
#define LOGICAL_CLOCK_H

#include "ns3/nstime.h"

#include <cstdint>

namespace ns3
{

/**
 * Protocol time built on top of Simulator::Now().  ns-3 time is never reset;
 * a future synchronization frame changes only this logical epoch.
 */
class LogicalClock
{
  public:
    LogicalClock(Time epoch, Time superframe);

    uint64_t GetAbsoluteSuperframe(Time now) const;
    uint16_t GetWireSuperframe(Time now) const;
    Time GetOffsetInSuperframe(Time now) const;

    void SetSynchronizedEpoch(Time receiveTime, uint16_t superframe);

  private:
    Time m_epoch;
    Time m_superframe;
    uint64_t m_epochCounter{0};
};

} // namespace ns3

#endif
