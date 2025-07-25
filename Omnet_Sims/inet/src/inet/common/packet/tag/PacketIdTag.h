#ifndef __INET_PACKETIDTAG_H
#define __INET_PACKETIDTAG_H

#include "inet/common/INETDefs.h"
#include "inet/common/TagBase_m.h"      // ← qui l’include corretto

namespace inet {

class PacketIdTag : public TagBase {
  protected:
    uint64_t packetId = 0;
  public:
    PacketIdTag() = default;
    PacketIdTag(const PacketIdTag& other) : TagBase(other), packetId(other.packetId) {}
    virtual PacketIdTag *dup() const override { return new PacketIdTag(*this); }
    void setPacketId(uint64_t id)    { packetId = id; }
    uint64_t getPacketId() const     { return packetId; }
    // serializzazione
    virtual void parsimPack(cCommBuffer *b) const override {
        TagBase::parsimPack(b);
        b->pack(packetId);
    }
    virtual void parsimUnpack(cCommBuffer *b) override {
        TagBase::parsimUnpack(b);
        b->unpack(packetId);
    }
};

} // namespace inet

#endif // __INET_PACKETIDTAG_H
