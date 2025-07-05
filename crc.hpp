/*

    Sega Saturn USB flash cart transfer utility
    Copyright © 2012, 2013, 2015 Anders Montonen
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

*/

#pragma once

#include <cstddef>
#include <cstdint>

namespace crc8
{

    /**
     * @brief Type for CRC values (8-bit).
     */
    using crc_t = uint8_t;

    /**
     * @brief CRC-8 polynomial (0x07).
     */
    constexpr crc_t CRC_POLY = 0x07;

    /**
     * @brief Initial XOR value for CRC calculation.
     */
    constexpr crc_t CRC_XORIN = 0x00;

    /**
     * @brief Final XOR value for CRC calculation.
     */
    constexpr crc_t CRC_XOROUT = 0x00;

    /**
     * @brief Reflect input bytes (not used for this CRC).
     */
    constexpr bool CRC_REFLECTIN = false;

    /**
     * @brief Reflect output CRC (not used for this CRC).
     */
    constexpr bool CRC_REFLECTOUT = false;

    /**
     * @brief Width of the CRC in bits.
     */
    constexpr std::size_t CRC_WIDTH = 8;

    /**
     * @brief Initialize CRC calculation.
     * @return Initial CRC value.
     */
    constexpr crc_t crc_init() noexcept { return CRC_XORIN; }

    /**
     * @brief Update CRC with a block of data.
     * @param crc Current CRC value.
     * @param data Pointer to data buffer.
     * @param data_len Number of bytes in data buffer.
     * @return Updated CRC value.
     */
    crc_t crc_update(crc_t crc, const unsigned char *data, std::size_t data_len) noexcept;

    /**
     * @brief Finalize CRC calculation.
     * @param crc Current CRC value.
     * @return Final CRC value after XOROUT.
     */
    constexpr crc_t crc_finalize(crc_t crc) noexcept { return crc ^ CRC_XOROUT; }

} // namespace crc8
