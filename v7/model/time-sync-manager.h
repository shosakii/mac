#ifndef TIME_SYNC_MANAGER_H
#define TIME_SYNC_MANAGER_H

#include "logical-clock.h"

namespace ns3
{

/**
 * Extension point for the later synchronization experiment.  It deliberately
 * does nothing in the shared-clock baseline, but keeps synchronization out of
 * the topology Beacon implementation.
 */
class TimeSyncManager
{
  public:
    explicit TimeSyncManager(LogicalClock& clock);
    void SetEnabled(bool enabled);
    bool IsEnabled() const;

  private:
    LogicalClock& m_clock;
    bool m_enabled{false};
};

} // namespace ns3

#endif
