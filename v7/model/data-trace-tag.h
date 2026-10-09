#ifndef DATA_TRACE_TAG_H
#define DATA_TRACE_TAG_H

#include "ns3/tag.h"
#include "ns3/nstime.h"

#include <cstdint>

namespace ns3
{

/**
 * Simulator-only data measurement metadata.
 *
 * PacketTag bytes are not serialized into the 802.11 frame and therefore do
 * not change MPDU length or airtime.  This tag must never be presented as a
 * proposed over-the-air MAC header.
 */
class DataTraceTag : public Tag
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer i) const override;
    void Deserialize(TagBuffer i) override;
    void Print(std::ostream& os) const override;

    void Set(uint64_t demandId,
             uint8_t source,
             uint8_t destination,
             uint32_t sequence,
             uint16_t superframe,
             Time transmitTime);

    uint64_t GetDemandId() const;
    uint8_t GetSource() const;
    uint8_t GetDestination() const;
    uint32_t GetSequence() const;
    uint16_t GetSuperframe() const;
    Time GetTransmitTime() const;

  private:
    uint64_t m_demandId{0};
    uint64_t m_transmitTimeNs{0};
    uint32_t m_sequence{0};
    uint16_t m_superframe{0};
    uint8_t m_source{0};
    uint8_t m_destination{0};
};

} // namespace ns3

#endif
