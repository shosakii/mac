#ifndef TOPOLOGY_SCENARIO_H
#define TOPOLOGY_SCENARIO_H

#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"
#include "ns3/vector.h"

#include <cstdint>
#include <vector>

namespace ns3
{

enum class ScenarioEventType : uint8_t
{
    FAIL,
    RECOVER,
    MOVE
};

struct TopologyScenarioEvent
{
    ScenarioEventType type{ScenarioEventType::FAIL};
    uint8_t node{0};
    Time offset{Seconds(0)};
    Vector moveDelta{0.0, 0.0, 0.0};
};

struct TopologyScenarioPlan
{
    std::vector<TopologyScenarioEvent> events;
};

/** Builds reproducible, symmetric-link topology changes for the V3 study. */
class TopologyScenarioPlanner
{
  public:
    TopologyScenarioPlanner(uint8_t nodeCount, uint8_t columns, double spacingMeters);

    TopologyScenarioPlan BuildRandom();
    TopologyScenarioPlan BuildScripted() const;

  private:
    std::vector<uint8_t> InteriorNodes() const;

    uint8_t m_nodeCount;
    uint8_t m_columns;
    double m_spacing;
    Ptr<UniformRandomVariable> m_nodeRng;
    Ptr<UniformRandomVariable> m_timeRng;
    Ptr<UniformRandomVariable> m_directionRng;
};

} // namespace ns3

#endif
