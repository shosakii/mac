#include "logical-clock.h"

#include "ns3/abort.h"

namespace ns3
{

LogicalClock::LogicalClock(Time epoch, Time superframe)
    : m_epoch(epoch),
      m_superframe(superframe)
{
    NS_ABORT_MSG_IF(superframe.IsZero() || superframe.IsNegative(),
                    "Superframe period must be positive");
}

uint64_t
LogicalClock::GetAbsoluteSuperframe(Time now) const
{
    if (now < m_epoch)
    {
        return m_epochCounter;
    }
    return m_epochCounter + static_cast<uint64_t>((now - m_epoch).GetNanoSeconds() /
                                                   m_superframe.GetNanoSeconds());
}

uint16_t
LogicalClock::GetWireSuperframe(Time now) const
{
    return static_cast<uint16_t>(GetAbsoluteSuperframe(now) & 0xffffU);
}

Time
LogicalClock::GetOffsetInSuperframe(Time now) const
{
    if (now < m_epoch)
    {
        return Seconds(0);
    }
    return NanoSeconds((now - m_epoch).GetNanoSeconds() % m_superframe.GetNanoSeconds());
}

void
LogicalClock::SetSynchronizedEpoch(Time receiveTime, uint16_t superframe)
{
    m_epoch = receiveTime;
    m_epochCounter = superframe;
}

} // namespace ns3
