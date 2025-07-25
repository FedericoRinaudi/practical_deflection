#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>

// 1) hash_combine (inline perché definito in header)
template <typename T>
inline void hash_combine(std::size_t& seed, T const& v) {
    constexpr std::size_t magic = 0x9e3779b97f4a7c15ULL;
    std::hash<T> hasher;
    seed ^= hasher(v) + magic + (seed << 6) + (seed >> 2);
}

// 2) helper variadico per combinare più valori
template <typename... Ts>
inline std::size_t hash_value(Ts const&... args) {
    std::size_t seed = 0;
    (hash_combine(seed, args), ...);
    return seed;
}

// 3) la struct chiave
struct FlowKey {
    uint32_t seqnum;
    uint16_t port_src;
    uint16_t port_dst;
    uint32_t ip_src;
    uint32_t ip_dst;

    bool operator==(FlowKey const& o) const noexcept {
        return seqnum  == o.seqnum
            && port_src == o.port_src
            && port_dst == o.port_dst
            && ip_src   == o.ip_src
            && ip_dst   == o.ip_dst;
    }

    // Restituisce l'hash come numero - usa direttamente hash_value
    std::size_t getHash() const noexcept {
        return hash_value(seqnum, port_src, port_dst, ip_src, ip_dst);
    }

    // Restituisce l'hash ridotto a un valore sicuro per OMNeT++
    std::size_t getSafeHash() const noexcept {
        return getHash() & 0xFFFFFFFFu;
    }

    // Conversione esplicita in size_t (opzionale)
    explicit operator std::size_t() const noexcept {
        return getHash();
    }
};

// 4) specializzazione di std::hash<FlowKey>
namespace std {
    template <>
    struct hash<FlowKey> {
        std::size_t operator()(FlowKey const& k) const noexcept {
            return k.getHash();
        }
    };
}