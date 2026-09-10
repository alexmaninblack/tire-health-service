// SPDX-FileCopyrightText: 2026 maninblack
// SPDX-License-Identifier: Apache-2.0
#include "tire_health/runtime/runtime.hpp"
#include <algorithm>
#include <stdexcept>
namespace tire_health::runtime {
namespace {
std::uint32_t rotate_left(std::uint32_t value, unsigned count) {
    return (value << count) | (value >> (32U - count));
}

std::array<std::uint8_t, 20> sha1(std::string_view input) {
    std::vector<std::uint8_t> message(input.begin(), input.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(message.size()) * 8U;
    message.push_back(0x80U);
    while (message.size() % 64U != 56U) {
        message.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        message.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
    }

    std::array<std::uint32_t, 5> state = {
        0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U, 0xc3d2e1f0U};
    for (std::size_t offset = 0; offset < message.size(); offset += 64U) {
        std::array<std::uint32_t, 80> words{};
        for (std::size_t index = 0; index < 16U; ++index) {
            const std::size_t at = offset + index * 4U;
            words[index] = (static_cast<std::uint32_t>(message[at]) << 24U) |
                           (static_cast<std::uint32_t>(message[at + 1U]) << 16U) |
                           (static_cast<std::uint32_t>(message[at + 2U]) << 8U) |
                           static_cast<std::uint32_t>(message[at + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            words[index] = rotate_left(
                words[index - 3U] ^ words[index - 8U] ^ words[index - 14U] ^
                    words[index - 16U],
                1U);
        }

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        for (std::size_t index = 0; index < words.size(); ++index) {
            std::uint32_t function{};
            std::uint32_t constant{};
            if (index < 20U) {
                function = (b & c) | ((~b) & d);
                constant = 0x5a827999U;
            } else if (index < 40U) {
                function = b ^ c ^ d;
                constant = 0x6ed9eba1U;
            } else if (index < 60U) {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8f1bbcdcU;
            } else {
                function = b ^ c ^ d;
                constant = 0xca62c1d6U;
            }
            const std::uint32_t temporary = rotate_left(a, 5U) + function + e +
                                            constant + words[index];
            e = d;
            d = c;
            c = rotate_left(b, 30U);
            b = a;
            a = temporary;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }

    std::array<std::uint8_t, 20> digest{};
    for (std::size_t index = 0; index < state.size(); ++index) {
        digest[index * 4U] = static_cast<std::uint8_t>(state[index] >> 24U);
        digest[index * 4U + 1U] = static_cast<std::uint8_t>(state[index] >> 16U);
        digest[index * 4U + 2U] = static_cast<std::uint8_t>(state[index] >> 8U);
        digest[index * 4U + 3U] = static_cast<std::uint8_t>(state[index]);
    }
    return digest;
}

int hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

std::array<std::uint8_t, 16> decode_uuid(const std::string& value) {
    if (value.size() != 36U || value[8] != '-' || value[13] != '-' ||
        value[18] != '-' || value[23] != '-') {
        throw std::invalid_argument("UUID must use lowercase RFC 4122 text form");
    }
    std::array<std::uint8_t, 16> bytes{};
    std::size_t at = 0U;
    int high = -1;
    for (char character : value) {
        if (character == '-') {
            continue;
        }
        const int nibble = hex_value(character);
        if (nibble < 0) {
            throw std::invalid_argument("UUID must use lowercase hexadecimal digits");
        }
        if (high < 0) {
            high = nibble;
        } else {
            bytes.at(at++) = static_cast<std::uint8_t>((high << 4) | nibble);
            high = -1;
        }
    }
    if (at != bytes.size() || high >= 0) {
        throw std::invalid_argument("invalid UUID byte count");
    }
    return bytes;
}

std::string encode_uuid(const std::array<std::uint8_t, 16>& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(36U);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4U || index == 6U || index == 8U || index == 10U) {
            output.push_back('-');
        }
        output.push_back(digits[bytes[index] >> 4U]);
        output.push_back(digits[bytes[index] & 0x0fU]);
    }
    return output;
}


}
std::string uuid_v5(
    const std::string& namespace_uuid,
    const std::vector<std::string>& fields) {
    std::string bytes;
    const auto namespace_bytes = decode_uuid(namespace_uuid);
    bytes.assign(
        reinterpret_cast<const char*>(namespace_bytes.data()), namespace_bytes.size());
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (fields[index].find_first_of(std::string("\0\n\r", 3U)) != std::string::npos) {
            throw std::invalid_argument("UUIDv5 field contains a forbidden delimiter byte");
        }
        if (index != 0U) {
            bytes.push_back('\n');
        }
        bytes += fields[index];
    }
    const auto digest = sha1(bytes);
    std::array<std::uint8_t, 16> uuid{};
    std::copy_n(digest.begin(), uuid.size(), uuid.begin());
    uuid[6] = static_cast<std::uint8_t>((uuid[6] & 0x0fU) | 0x50U);
    uuid[8] = static_cast<std::uint8_t>((uuid[8] & 0x3fU) | 0x80U);
    return encode_uuid(uuid);
}


}
