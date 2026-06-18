/**
 * @file crc.hpp
 * @brief CRC-8 checksum computation utilities.
 * @details Provides efficient CRC-8 calculation using a lookup table.
 */

#ifndef CRC_HPP
#define CRC_HPP

#include <cstddef>
#include <cstdint>

/**
 * @namespace crc8
 * @brief CRC-8 checksum operations.
 */
namespace crc8 {

/**
 * @typedef uint8_t crc_t
 * @brief Type alias for CRC-8 checksum value.
 */
using crc_t = uint8_t;

/**
 * @brief Update CRC-8 value with a block of data.
 *
 * Uses a precomputed lookup table for O(n) efficient computation.
 * Combines multiple bytes into a single CRC value suitable for
 * Saturn cartridge communication protocols.
 *
 * @param crc Current CRC value (initialize with 0 for new computation).
 * @param data Pointer to input data buffer.
 * @param data_len Number of bytes in data buffer.
 * @return Updated CRC-8 value after processing all data bytes.
 * @note This function is noexcept and safe for interrupt contexts.
 */
crc_t crc_update(crc_t crc, const unsigned char* data, std::size_t data_len) noexcept;

} // namespace crc8

#endif // CRC_HPP
