#include "data-trace-tag.h"

#include "ns3/log.h"

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(DataTraceTag);

TypeId
DataTraceTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::DataTraceTag")
                            .SetParent<Tag>()
                            .SetGroupName("Network")
                            .AddConstructor<DataTraceTag>();
    return tid;
}

TypeId
DataTraceTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
DataTraceTag::GetSerializedSize() const
{
    return 24;
}

void
DataTraceTag::Serialize(TagBuffer i) const
{
    i.WriteU64(m_demandId);
    i.WriteU64(m_transmitTimeNs);
    i.WriteU32(m_sequence);
    i.WriteU16(m_superframe);
    i.WriteU8(m_source);
    i.WriteU8(m_destination);
}

void
DataTraceTag::Deserialize(TagBuffer i)
{
    m_demandId = i.ReadU64();
    m_transmitTimeNs = i.ReadU64();
    m_sequence = i.ReadU32();
    m_superframe = i.ReadU16();
    m_source = i.ReadU8();
    m_destination = i.ReadU8();
}

void
DataTraceTag::Print(std::ostream& os) const
{
    os << "demand=" << m_demandId << " src=" << +m_source << " dst=" << +m_destination
       << " seq=" << m_sequence << " sf=" << m_superframe
       << " txNs=" << m_transmitTimeNs;
}

void
DataTraceTag::Set(uint64_t demandId,
                  uint8_t source,
                  uint8_t destination,
                  uint32_t sequence,
                  uint16_t superframe,
                  Time transmitTime)
{
    m_demandId = demandId;
    m_source = source;
    m_destination = destination;
    m_sequence = sequence;
    m_superframe = superframe;
    m_transmitTimeNs = static_cast<uint64_t>(transmitTime.GetNanoSeconds());
}

uint64_t DataTraceTag::GetDemandId() const { return m_demandId; }
uint8_t DataTraceTag::GetSource() const { return m_source; }
uint8_t DataTraceTag::GetDestination() const { return m_destination; }
uint32_t DataTraceTag::GetSequence() const { return m_sequence; }
uint16_t DataTraceTag::GetSuperframe() const { return m_superframe; }
Time DataTraceTag::GetTransmitTime() const { return NanoSeconds(m_transmitTimeNs); }

} // namespace ns3
