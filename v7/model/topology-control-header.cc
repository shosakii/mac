#include "topology-control-header.h"

#include "ns3/abort.h"

#include <array>

namespace ns3
{

namespace
{
constexpr uint8_t TOPOLOGY_IE_ID = 221; // Vendor Specific IE extension carrier
constexpr std::array<uint8_t, 3> EXPERIMENTAL_OUI{0x02, 0x00, 0x00};
constexpr uint8_t TOPOLOGY_SUBTYPE = 1;
constexpr uint8_t TOPOLOGY_VERSION = 1;
constexpr uint8_t ACTION_CATEGORY = 127;
constexpr uint8_t ACTION_CODE = 1;
} // namespace

std::string
ToString(ControlTransport mode)
{
    return mode == ControlTransport::ACTION ? "action" : "beacon";
}

std::string
ToString(NeighborEncoding mode)
{
    if (mode == NeighborEncoding::LIST)
    {
        return "list";
    }
    if (mode == NeighborEncoding::BITMAP)
    {
        return "bitmap";
    }
    return "adaptive";
}

TopologyControlHeader::TopologyControlHeader() = default;

TopologyControlHeader::TopologyControlHeader(ControlTransport transport)
    : m_transport(transport)
{
}

TypeId
TopologyControlHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::TopologyControlHeader")
                            .SetParent<Header>()
                            .SetGroupName("Wifi")
                            .AddConstructor<TopologyControlHeader>();
    return tid;
}

TypeId
TopologyControlHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
TopologyControlHeader::Configure(ControlTransport transport,
                                 NeighborEncoding requested,
                                 uint16_t superframe,
                                 const std::set<uint8_t>& neighbors)
{
    NS_ABORT_MSG_IF(neighbors.size() > 63, "A node can have at most 63 peers");
    m_transport = transport;
    m_superframe = superframe;
    m_neighbors.assign(neighbors.begin(), neighbors.end());
    m_bitmap = requested == NeighborEncoding::BITMAP ||
               (requested == NeighborEncoding::ADAPTIVE && neighbors.size() >= 4);
    m_valid = true;
}

uint32_t
TopologyControlHeader::GetSerializedSize() const
{
    if (m_transport == ControlTransport::ACTION)
    {
        return 2 + 1 + 2 + (m_bitmap ? 8 : 2 * m_neighbors.size());
    }
    // IE ID + length + OUI + subtype + version + flags + member epoch +
    // superframe + neighbor count + payload.
    return 14 + (m_bitmap ? 8 : 2 * m_neighbors.size());
}

void
TopologyControlHeader::Serialize(Buffer::Iterator i) const
{
    if (m_transport == ControlTransport::ACTION)
    {
        i.WriteU8(ACTION_CATEGORY);
        i.WriteU8(ACTION_CODE);
    }
    else
    {
        i.WriteU8(TOPOLOGY_IE_ID);
        i.WriteU8(static_cast<uint8_t>(GetSerializedSize() - 2));
        for (const auto value : EXPERIMENTAL_OUI)
        {
            i.WriteU8(value);
        }
        i.WriteU8(TOPOLOGY_SUBTYPE);
        i.WriteU8(TOPOLOGY_VERSION);
        i.WriteU8(m_bitmap ? 0x01 : 0x00);
        i.WriteHtonU16(0); // fixed membership epoch in the already-joined V5 experiment
        i.WriteHtonU16(m_superframe);
        i.WriteHtonU16(static_cast<uint16_t>(m_neighbors.size()));
    }

    if (m_transport == ControlTransport::ACTION)
    {
        const uint8_t descriptor = static_cast<uint8_t>((m_bitmap ? 0x80 : 0x00) |
                                                         (m_neighbors.size() & 0x3f));
        i.WriteU8(descriptor);
        i.WriteHtonU16(m_superframe);
    }

    if (!m_bitmap)
    {
        for (const auto id : m_neighbors)
        {
            i.WriteHtonU16(id);
        }
        return;
    }

    std::array<uint8_t, 8> bitmap{};
    for (const auto id : m_neighbors)
    {
        bitmap.at(id / 8) |= static_cast<uint8_t>(1U << (id % 8));
    }
    for (const auto value : bitmap)
    {
        i.WriteU8(value);
    }
}

uint32_t
TopologyControlHeader::Deserialize(Buffer::Iterator i)
{
    const auto start = i;
    uint8_t declaredLength = 0;
    uint16_t count = 0;
    if (m_transport == ControlTransport::ACTION)
    {
        const uint8_t category = i.ReadU8();
        const uint8_t action = i.ReadU8();
        if (category != ACTION_CATEGORY || action != ACTION_CODE)
        {
            m_valid = false;
            return 2;
        }
    }
    else
    {
        const uint8_t id = i.ReadU8();
        declaredLength = i.ReadU8();
        std::array<uint8_t, 3> oui{};
        for (auto& value : oui)
        {
            value = i.ReadU8();
        }
        const uint8_t subtype = i.ReadU8();
        const uint8_t version = i.ReadU8();
        const uint8_t flags = i.ReadU8();
        (void)i.ReadNtohU16(); // membership epoch
        m_superframe = i.ReadNtohU16();
        count = i.ReadNtohU16();
        m_bitmap = (flags & 0x01) != 0;
        if (id != TOPOLOGY_IE_ID || declaredLength < 12 || oui != EXPERIMENTAL_OUI ||
            subtype != TOPOLOGY_SUBTYPE || version != TOPOLOGY_VERSION || count > 63)
        {
            m_valid = false;
            return i.GetDistanceFrom(start);
        }
    }

    bool shortCounter = false;
    if (m_transport == ControlTransport::ACTION)
    {
        const uint8_t descriptor = i.ReadU8();
        m_bitmap = (descriptor & 0x80) != 0;
        shortCounter = (descriptor & 0x40) != 0;
        count = descriptor & 0x3f;
        m_superframe = shortCounter ? i.ReadU8() : i.ReadNtohU16();
    }
    m_neighbors.clear();

    if (m_bitmap)
    {
        for (uint8_t byteIndex = 0; byteIndex < 8; ++byteIndex)
        {
            const uint8_t value = i.ReadU8();
            for (uint8_t bit = 0; bit < 8; ++bit)
            {
                if ((value & (1U << bit)) != 0)
                {
                    m_neighbors.push_back(static_cast<uint8_t>(byteIndex * 8 + bit));
                }
            }
        }
    }
    else
    {
        for (uint8_t n = 0; n < count; ++n)
        {
            const uint16_t id = i.ReadNtohU16();
            if (id >= 64)
            {
                m_valid = false;
                return i.GetDistanceFrom(start);
            }
            m_neighbors.push_back(static_cast<uint8_t>(id));
        }
    }

    const uint32_t consumed = i.GetDistanceFrom(start);
    const uint32_t expected = m_transport == ControlTransport::ACTION
                                  ? 2 + 1 + (shortCounter ? 1 : 2) +
                                        (m_bitmap ? 8 : 2 * count)
                                  : 14 + (m_bitmap ? 8 : 2 * count);
    m_valid = consumed == expected && m_neighbors.size() == count &&
              (m_transport == ControlTransport::ACTION || declaredLength == consumed - 2);
    return consumed;
}

void
TopologyControlHeader::Print(std::ostream& os) const
{
    os << "sf=" << m_superframe << " encoding=" << (m_bitmap ? "bitmap" : "list")
       << " neighbors=";
    for (const auto id : m_neighbors)
    {
        os << +id << ' ';
    }
}

bool TopologyControlHeader::IsValid() const { return m_valid; }
uint16_t TopologyControlHeader::GetSuperframe() const { return m_superframe; }
std::set<uint8_t> TopologyControlHeader::GetNeighbors() const
{
    return {m_neighbors.begin(), m_neighbors.end()};
}
bool TopologyControlHeader::UsesBitmap() const { return m_bitmap; }

} // namespace ns3
