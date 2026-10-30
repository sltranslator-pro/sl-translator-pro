#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Official Firestorm/Second Life UDP decoder used by the production DLL.
// The wire format is documented by Firestorm's message_template.msg and
// llmessage/message.cpp:
//   flags(1) sequence(4 BE) extra(1) [zero-coded payload] [acks]
// Zero-coding is applied to the payload after the 6-byte packet id. ACKs are
// appended AFTER compression, so they must be stripped from the raw datagram
// first. Message numbers are then read from the decoded payload:
//   High: 1 byte, Medium: FF + 1 byte, Low: FF FF + 2-byte BE number.

namespace slpacket {

using Uuid = std::array<uint8_t, 16>;

constexpr uint8_t kZeroCodeFlag = 0x80;
constexpr uint8_t kAckFlag = 0x10;
constexpr size_t kHeaderSize = 6;

constexpr uint16_t kMsgAgentUpdate = 4;          // High
constexpr uint16_t kMsgUseCircuitCode = 3;       // Low
constexpr uint16_t kMsgChatFromViewer = 80;      // Low
constexpr uint16_t kMsgImprovedInstantMessage = 254; // Low
constexpr uint16_t kMsgChatFromSimulator = 139;  // Low

enum class Frequency : uint8_t { High = 0, Medium = 1, Low = 2 };

struct PacketView {
    uint8_t flags = 0;
    uint32_t sequence = 0;
    uint8_t extraHeaderLength = 0;
    Frequency frequency = Frequency::High;
    uint16_t messageNumber = 0;
    size_t messageNameLength = 0;
    size_t bodyOffset = 0;
    size_t datagramDataEnd = 0;
    std::vector<uint8_t> decodedPayload;
};

inline bool IsZeroUuid(const Uuid& id) {
    for (uint8_t b : id) if (b != 0) return false;
    return true;
}

inline bool DecodeZeroCodedPayload(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(size);
    for (size_t i = 0; i < size;) {
        uint8_t value = data[i++];
        if (value != 0) {
            out.push_back(value);
            continue;
        }
        if (i >= size) return false;
        uint8_t count = data[i++];
        if (count == 0) {
            out.insert(out.end(), 256, 0);
        } else {
            out.insert(out.end(), count, 0);
        }
        if (out.size() > 65536) return false;
    }
    return true;
}

inline bool DecodePacket(const uint8_t* data, size_t size, PacketView& out) {
    if (!data || size < kHeaderSize + 1) return false;

    out = {};
    out.flags = data[0];
    out.sequence = (uint32_t(data[1]) << 24) | (uint32_t(data[2]) << 16) |
                   (uint32_t(data[3]) << 8) | uint32_t(data[4]);
    out.extraHeaderLength = data[5];

    size_t dataEnd = size;
    if ((out.flags & kAckFlag) != 0) {
        uint8_t ackCount = data[size - 1];
        size_t ackBytes = size_t(ackCount) * 4 + 1;
        if (ackBytes >= size - kHeaderSize) return false;
        dataEnd -= ackBytes;
    }
    if (dataEnd <= kHeaderSize) return false;
    out.datagramDataEnd = dataEnd;

    const uint8_t* encodedPayload = data + kHeaderSize;
    size_t encodedPayloadSize = dataEnd - kHeaderSize;
    if ((out.flags & kZeroCodeFlag) != 0) {
        if (!DecodeZeroCodedPayload(encodedPayload, encodedPayloadSize, out.decodedPayload)) return false;
    } else {
        out.decodedPayload.assign(encodedPayload, encodedPayload + encodedPayloadSize);
    }
    if (out.decodedPayload.empty()) return false;

    const auto& p = out.decodedPayload;
    if (p[0] != 0xFF) {
        out.frequency = Frequency::High;
        out.messageNameLength = 1;
        out.messageNumber = p[0];
    } else if (p.size() >= 2 && p[1] != 0xFF) {
        out.frequency = Frequency::Medium;
        out.messageNameLength = 2;
        out.messageNumber = p[1];
    } else if (p.size() >= 4 && p[0] == 0xFF && p[1] == 0xFF) {
        out.frequency = Frequency::Low;
        out.messageNameLength = 4;
        out.messageNumber = uint16_t((uint16_t(p[2]) << 8) | p[3]);
    } else {
        return false;
    }

    out.bodyOffset = out.messageNameLength + out.extraHeaderLength;
    return out.bodyOffset <= p.size();
}

inline bool DecodePacket(const char* buf, int len, PacketView& out) {
    if (!buf || len <= 0) return false;
    return DecodePacket(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len), out);
}

inline bool MatchesLow(const PacketView& packet, uint16_t messageNumber) {
    return packet.frequency == Frequency::Low && packet.messageNumber == messageNumber;
}

inline bool MatchesHigh(const PacketView& packet, uint16_t messageNumber) {
    return packet.frequency == Frequency::High && packet.messageNumber == messageNumber;
}

inline bool CopyUuid(const PacketView& packet, size_t bodyRelativeOffset, Uuid& uuid) {
    size_t pos = packet.bodyOffset + bodyRelativeOffset;
    if (pos + uuid.size() > packet.decodedPayload.size()) return false;
    memcpy(uuid.data(), packet.decodedPayload.data() + pos, uuid.size());
    return !IsZeroUuid(uuid);
}

inline bool TryExtractOwnAgentUuid(const PacketView& packet, Uuid& uuid) {
    if (MatchesHigh(packet, kMsgAgentUpdate))
        return CopyUuid(packet, 0, uuid);
    if (MatchesLow(packet, kMsgUseCircuitCode))
        return CopyUuid(packet, 4 + 16, uuid);
    return false;
}

inline bool ReadBytes(const PacketView& packet, size_t& pos, void* dest, size_t count) {
    if (pos + count > packet.decodedPayload.size()) return false;
    memcpy(dest, packet.decodedPayload.data() + pos, count);
    pos += count;
    return true;
}

inline bool ReadU8(const PacketView& packet, size_t& pos, uint8_t& value) {
    return ReadBytes(packet, pos, &value, 1);
}

inline bool ReadU16LE(const PacketView& packet, size_t& pos, uint16_t& value) {
    uint8_t bytes[2] = {};
    if (!ReadBytes(packet, pos, bytes, 2)) return false;
    value = uint16_t(bytes[0] | (bytes[1] << 8));
    return true;
}

inline bool ReadS32LE(const PacketView& packet, size_t& pos, int32_t& value) {
    uint8_t bytes[4] = {};
    if (!ReadBytes(packet, pos, bytes, 4)) return false;
    value = int32_t(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
    return true;
}

inline bool ReadStringV1(const PacketView& packet, size_t& pos, std::string& value) {
    uint8_t length = 0;
    if (!ReadU8(packet, pos, length)) return false;
    if (pos + length > packet.decodedPayload.size()) return false;
    value.assign(reinterpret_cast<const char*>(packet.decodedPayload.data() + pos), length);
    pos += length;
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return true;
}

inline bool ReadStringV2(const PacketView& packet, size_t& pos, std::string& value) {
    uint16_t length = 0;
    if (!ReadU16LE(packet, pos, length)) return false;
    if (length == 0 || pos + length > packet.decodedPayload.size()) return false;
    value.assign(reinterpret_cast<const char*>(packet.decodedPayload.data() + pos), length);
    pos += length;
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return true;
}

struct ChatFromViewerView {
    Uuid agentId{};
    Uuid sessionId{};
    std::string message;
    uint8_t chatType = 0;
    int32_t channel = 0;
};

inline bool ExtractChatFromViewer(const PacketView& packet, ChatFromViewerView& out) {
    if (!MatchesLow(packet, kMsgChatFromViewer)) return false;
    size_t pos = packet.bodyOffset;
    if (!ReadBytes(packet, pos, out.agentId.data(), out.agentId.size())) return false;
    if (!ReadBytes(packet, pos, out.sessionId.data(), out.sessionId.size())) return false;
    if (!ReadStringV2(packet, pos, out.message)) return false;
    if (!ReadU8(packet, pos, out.chatType)) return false;
    if (!ReadS32LE(packet, pos, out.channel)) return false;
    return true;
}

struct ChatFromSimulatorView {
    std::string fromName;
    Uuid sourceId{};
    Uuid ownerId{};
    uint8_t sourceType = 0;
    uint8_t chatType = 0;
    uint8_t audible = 0;
    std::string message;
    size_t sourceTypeDecodedOffset = 0;
};

inline bool ExtractChatFromSimulator(const PacketView& packet, ChatFromSimulatorView& out) {
    if (!MatchesLow(packet, kMsgChatFromSimulator)) return false;
    size_t pos = packet.bodyOffset;
    if (!ReadStringV1(packet, pos, out.fromName)) return false;
    if (!ReadBytes(packet, pos, out.sourceId.data(), out.sourceId.size())) return false;
    if (!ReadBytes(packet, pos, out.ownerId.data(), out.ownerId.size())) return false;
    out.sourceTypeDecodedOffset = pos;
    if (!ReadU8(packet, pos, out.sourceType)) return false;
    if (!ReadU8(packet, pos, out.chatType)) return false;
    if (!ReadU8(packet, pos, out.audible)) return false;
    pos += 12;
    if (pos > packet.decodedPayload.size()) return false;
    if (!ReadStringV2(packet, pos, out.message)) return false;
    return true;
}

} // namespace slpacket
