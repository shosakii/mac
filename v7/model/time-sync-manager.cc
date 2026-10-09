#include "time-sync-manager.h"

namespace ns3
{

TimeSyncManager::TimeSyncManager(LogicalClock& clock)
    : m_clock(clock)
{
}

void TimeSyncManager::SetEnabled(bool enabled) { m_enabled = enabled; }
bool TimeSyncManager::IsEnabled() const { return m_enabled; }

} // namespace ns3
