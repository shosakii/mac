#ifndef CONTROL_WINDOW_SCHEDULER_H
#define CONTROL_WINDOW_SCHEDULER_H

#include "ns3/nstime.h"

#include <cstdint>
#include <vector>

namespace ns3
{

enum class ControlMessageType : uint8_t
{
    TOPOLOGY_BEACON,
    TIME_SYNC
};

struct ControlSlot
{
    Time offset;
    ControlMessageType type;
    uint16_t sender;
};

/** Produces control-window assignments independently of frame implementations. */
class ControlWindowScheduler
{
  public:
    ControlWindowScheduler(uint16_t nodeCount, Time window, Time releaseOffset);

    /**
     * Return whether one radio is a regular contender in this superframe.
     * Every radio can evaluate this locally from the shared superframe number,
     * membership epoch, node identifier and configured nominal group size.
     */
    bool IsRegularCandidate(uint64_t absoluteSuperframe,
                            uint16_t node,
                            uint16_t contenderLimit,
                            uint16_t membershipEpoch = 0) const;

    /** Local release order among the nominal contenders; no central slot list. */
    uint16_t CandidatePosition(uint16_t node,
                               uint16_t contenderLimit,
                               uint16_t membershipEpoch = 0) const;

    /** A locally remembered deferred Beacon retries in the next window. */
    bool ShouldAttempt(uint64_t absoluteSuperframe,
                       uint16_t node,
                       uint16_t contenderLimit,
                       bool locallyDeferred,
                       uint16_t membershipEpoch = 0) const;

    std::vector<ControlSlot> Build(uint64_t absoluteSuperframe,
                                   uint16_t contenderLimit,
                                   const std::vector<uint16_t>& priorityNodes = {}) const;

  private:
    uint32_t CandidateRank(uint16_t node, uint16_t membershipEpoch) const;
    uint16_t m_nodeCount;
    Time m_window;
    Time m_releaseOffset;
};

} // namespace ns3

#endif
