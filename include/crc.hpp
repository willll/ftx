#ifndef CRC_HPP
#define CRC_HPP

#include <cstddef>
#include <cstdint>

namespace crc8 {

using crc_t = uint8_t;

/**
 * @brief Update CRC-8 value with a block of data.
 *
 * Uses a precomputed lookup table for efficiency.
 *
 * @param crc Current CRC value.
 * @param data Pointer to data buffer.
 * @param data_len Number of bytes in data buffer.
 * @return Updated CRC value.
 */
crc_t crc_update(crc_t crc, const unsigned char* data, std::size_t data_len) noexcept;

} // namespace crc8

#endif // CRC_HPP
