#include "topology-scenario.h"

#include "ns3/abort.h"

#include <array>
#include <set>

namespace ns3
{

TopologyScenarioPlanner::TopologyScenarioPlanner(uint8_t nodeCount,
                                                 uint8_t columns,
                                                 double spacingMeters)
    : m_nodeCount(nodeCount),
      m_columns(columns),
      m_spacing(spacingMeters),
      m_nodeRng(CreateObject<UniformRandomVariable>()),
      m_timeRng(CreateObject<UniformRandomVariable>()),
      m_directionRng(CreateObject<UniformRandomVariable>())
{
    NS_ABORT_MSG_IF(nodeCount == 0 || columns < 3 || nodeCount % columns != 0,
                    "TopologyScenarioPlanner requires a rectangular grid");
    m_nodeRng->SetStream(10011);
    m_timeRng->SetStream(10012);
    m_directionRng->SetStream(10013);
}

std::vector<uint8_t>
TopologyScenarioPlanner::InteriorNodes() const
{
    const uint8_t rows = m_nodeCount / m_columns;
    std::vector<uint8_t> result;
    for (uint8_t node = 0; node < m_nodeCount; ++node)
    {
        const uint8_t row = node / m_columns;
        const uint8_t column = node % m_columns;
        if (row > 0 && row + 1 < rows && column > 0 && column + 1 < m_columns)
        {
            result.push_back(node);
        }
    }
    return result;
}

TopologyScenarioPlan
TopologyScenarioPlanner::BuildScripted() const
{
    NS_ABORT_MSG_IF(m_nodeCount <= 36, "Scripted V3 scenario requires at least 37 nodes");
    TopologyScenarioPlan plan;
    const Vector outward{0.8 * m_spacing, 0.6 * m_spacing, 0.0};
    plan.events = {
        {ScenarioEventType::FAIL, 27, Seconds(4.05), {}},
        {ScenarioEventType::RECOVER, 27, Seconds(9.03), {}},
        {ScenarioEventType::MOVE, 36, Seconds(14.07), outward},
        {ScenarioEventType::MOVE, 36, Seconds(19.04), {-outward.x, -outward.y, 0.0}},
        {ScenarioEventType::FAIL, 18, Seconds(24.08), {}},
        {ScenarioEventType::RECOVER, 18, Seconds(29.06), {}},
    };
    return plan;
}

TopologyScenarioPlan
TopologyScenarioPlanner::BuildRandom()
{
    const auto interior = InteriorNodes();
    NS_ABORT_MSG_IF(interior.size() < 3, "Not enough interior nodes for V3 events");

    std::set<uint8_t> selected;
    while (selected.size() < 3)
    {
        selected.insert(interior.at(m_nodeRng->GetInteger(0, interior.size() - 1)));
    }
    auto selectedIt = selected.begin();
    const uint8_t firstFailure = *selectedIt++;
    const uint8_t movingNode = *selectedIt++;
    const uint8_t secondFailure = *selectedIt;

    const std::array<Vector, 8> directions = {
        Vector{1.0, 0.0, 0.0}, Vector{-1.0, 0.0, 0.0},
        Vector{0.0, 1.0, 0.0}, Vector{0.0, -1.0, 0.0},
        Vector{0.8, 0.6, 0.0}, Vector{0.8, -0.6, 0.0},
        Vector{-0.8, 0.6, 0.0}, Vector{-0.8, -0.6, 0.0},
    };
    const Vector unit = directions.at(m_directionRng->GetInteger(0, directions.size() - 1));
    const Vector delta{unit.x * m_spacing, unit.y * m_spacing, 0.0};
    const double jitter1 = m_timeRng->GetValue(-0.04, 0.04);
    const double jitter2 = m_timeRng->GetValue(-0.04, 0.04);

    TopologyScenarioPlan plan;
    plan.events = {
        {ScenarioEventType::FAIL, firstFailure, Seconds(4.05 + jitter1), {}},
        {ScenarioEventType::RECOVER, firstFailure, Seconds(9.03 + jitter2), {}},
        {ScenarioEventType::MOVE, movingNode, Seconds(14.07 - jitter1), delta},
        {ScenarioEventType::MOVE,
         movingNode,
         Seconds(19.04 - jitter2),
         {-delta.x, -delta.y, 0.0}},
        {ScenarioEventType::FAIL, secondFailure, Seconds(24.08 + jitter2), {}},
        {ScenarioEventType::RECOVER, secondFailure, Seconds(29.06 + jitter1), {}},
    };
    return plan;
}

} // namespace ns3
