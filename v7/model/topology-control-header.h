#ifndef TOPOLOGY_CONTROL_HEADER_H
#define TOPOLOGY_CONTROL_HEADER_H

#include "ns3/header.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ns3
{

enum class ControlTransport : uint8_t
{
    ACTION,
    BEACON
};

enum class NeighborEncoding : uint8_t
{
    LIST,
    BITMAP,
    ADAPTIVE
};

std::string ToString(ControlTransport mode);
std::string ToString(NeighborEncoding mode);

/** Compact one-hop topology information carried by an Action or Beacon frame. */
class TopologyControlHeader : public Header
{
  public:
    TopologyControlHeader();
    explicit TopologyControlHeader(ControlTransport transport);

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    void Configure(ControlTransport transport,
                   NeighborEncoding requested,
                   uint16_t superframe,
                   const std::set<uint8_t>& neighbors);

    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator i) const override;
    uint32_t Deserialize(Buffer::Iterator i) override;
    void Print(std::ostream& os) const override;

    bool IsValid() const;
    uint16_t GetSuperframe() const;
    std::set<uint8_t> GetNeighbors() const;
    bool UsesBitmap() const;

  private:
    ControlTransport m_transport{ControlTransport::ACTION};
    uint16_t m_superframe{0};
    std::vector<uint8_t> m_neighbors;
    bool m_bitmap{false};
    bool m_valid{false};
};

} // namespace ns3

#endif
