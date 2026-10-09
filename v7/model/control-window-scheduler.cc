#include "control-window-scheduler.h"

#include "ns3/abort.h"

#include <numeric>

namespace ns3
{

ControlWindowScheduler::ControlWindowScheduler(uint16_t nodeCount,
                                               Time window,
                                               Time releaseOffset)
    : m_nodeCount(nodeCount),
      m_window(window),
      m_releaseOffset(releaseOffset)
{
    NS_ABORT_MSG_IF(nodeCount == 0, "The control scheduler needs at least one radio");
    NS_ABORT_MSG_IF(releaseOffset >= window,
                    "The contender release time must be inside the control window");
}

uint32_t
ControlWindowScheduler::CandidateRank(uint16_t node, uint16_t membershipEpoch) const
{
    NS_ABORT_MSG_IF(node >= m_nodeCount, "Node identifier is outside the membership");
    uint32_t rank = 0;
    if (m_nodeCount > 1)
    {
        // Select a stride near one quarter of the membership size and advance
        // until it is coprime with N.  Its modular inverse maps a node ID to a
        // unique rank.  For N=64 this chooses stride=17 and inverse=49, exactly
        // preserving the validated schedule; unlike a hard-coded table, the
        // same local calculation also works for N=100, 128, 256, and so on.
        uint32_t stride = static_cast<uint32_t>(m_nodeCount) / 4U + 1U;
        while (std::gcd(stride, static_cast<uint32_t>(m_nodeCount)) != 1U)
        {
            ++stride;
        }
        uint32_t inverse = 1;
        while ((stride * inverse) % m_nodeCount != 1U)
        {
            ++inverse;
        }
        rank = (static_cast<uint32_t>(node) * inverse + membershipEpoch) % m_nodeCount;
    }
    return rank;
}

uint16_t
ControlWindowScheduler::CandidatePosition(uint16_t node,
                                          uint16_t contenderLimit,
                                          uint16_t membershipEpoch) const
{
    NS_ABORT_MSG_IF(contenderLimit == 0 || contenderLimit > m_nodeCount,
                    "The contender limit must be in [1,nodeCount]");
    return static_cast<uint16_t>(CandidateRank(node, membershipEpoch) % contenderLimit);
}

bool
ControlWindowScheduler::IsRegularCandidate(uint64_t absoluteSuperframe,
                                           uint16_t node,
                                           uint16_t contenderLimit,
                                           uint16_t membershipEpoch) const
{
    NS_ABORT_MSG_IF(contenderLimit == 0 || contenderLimit > m_nodeCount,
                    "The contender limit must be in [1,nodeCount]");
    const uint32_t groupCount = (m_nodeCount + contenderLimit - 1) / contenderLimit;
    const uint32_t activeGroup = static_cast<uint32_t>(absoluteSuperframe % groupCount);
    return CandidateRank(node, membershipEpoch) / contenderLimit == activeGroup;
}

bool
ControlWindowScheduler::ShouldAttempt(uint64_t absoluteSuperframe,
                                      uint16_t node,
                                      uint16_t contenderLimit,
                                      bool locallyDeferred,
                                      uint16_t membershipEpoch) const
{
    return locallyDeferred ||
           IsRegularCandidate(absoluteSuperframe, node, contenderLimit, membershipEpoch);
}

std::vector<ControlSlot>
ControlWindowScheduler::Build(uint64_t absoluteSuperframe,
                              uint16_t contenderLimit,
                              const std::vector<uint16_t>& priorityNodes) const
{
    NS_ABORT_MSG_IF(contenderLimit == 0 || contenderLimit > m_nodeCount,
                    "The contender limit must be in [1,nodeCount]");
    std::vector<ControlSlot> slots;
    std::vector<bool> deferred(m_nodeCount, false);
    for (const auto node : priorityNodes)
    {
        if (node < m_nodeCount)
        {
            deferred[node] = true;
        }
    }
    for (uint16_t node = 0; node < m_nodeCount; ++node)
    {
        if (ShouldAttempt(absoluteSuperframe, node, contenderLimit, deferred[node]))
        {
            slots.push_back({m_releaseOffset, ControlMessageType::TOPOLOGY_BEACON, node});
        }
    }
    return slots;
}

} // namespace ns3
