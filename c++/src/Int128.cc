/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "orc/Int128.hh"
#include "Adaptor.hh"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace orc {
  NO_SANITIZE_ATTR
  Int128& Int128::operator<<=(uint32_t bits) {
    if (bits != 0) {
      if (bits < 64) {
        highbits_ <<= bits;
        highbits_ |= (lowbits_ >> (64 - bits));
        lowbits_ <<= bits;
      } else if (bits < 128) {
        highbits_ = static_cast<int64_t>(lowbits_) << (bits - 64);
        lowbits_ = 0;
      } else {
        highbits_ = 0;
        lowbits_ = 0;
      }
    }
    return *this;
  }

  NO_SANITIZE_ATTR
  Int128& Int128::operator>>=(uint32_t bits) {
    if (bits != 0) {
      if (bits < 64) {
        lowbits_ >>= bits;
        lowbits_ |= static_cast<uint64_t>(highbits_ << (64 - bits));
        highbits_ = static_cast<int64_t>(static_cast<uint64_t>(highbits_) >> bits);
      } else if (bits < 128) {
        lowbits_ = static_cast<uint64_t>(highbits_ >> (bits - 64));
        highbits_ = highbits_ >= 0 ? 0 : -1l;
      } else {
        highbits_ = highbits_ >= 0 ? 0 : -1l;
        lowbits_ = static_cast<uint64_t>(highbits_);
      }
    }
    return *this;
  }

  Int128 Int128::maximumValue() {
    return Int128(0x7fffffffffffffff, 0xffffffffffffffff);
  }

  Int128 Int128::minimumValue() {
    return Int128(static_cast<int64_t>(0x8000000000000000), 0x0);
  }

  Int128::Int128(const std::string& str) {
    lowbits_ = 0;
    highbits_ = 0;
    size_t length = str.length();
    if (length > 0) {
      bool isNegative = str[0] == '-';
      size_t posn = isNegative ? 1 : 0;
      while (posn < length) {
        size_t group = std::min(static_cast<size_t>(18), length - posn);
        int64_t chunk = std::stoll(str.substr(posn, group));
        int64_t multiple = 1;
        for (size_t i = 0; i < group; ++i) {
          multiple *= 10;
        }
        *this *= multiple;
        *this += chunk;
        posn += group;
      }
      if (isNegative) {
        negate();
      }
    }
  }

  Int128& Int128::operator*=(const Int128& right) {
    const uint64_t INT_MASK = 0xffffffff;
    const uint64_t CARRY_BIT = INT_MASK + 1;

    // Break the left and right numbers into 32 bit chunks
    // so that we can multiply them without overflow.
    uint64_t L0 = static_cast<uint64_t>(highbits_) >> 32;
    uint64_t L1 = static_cast<uint64_t>(highbits_) & INT_MASK;
    uint64_t L2 = lowbits_ >> 32;
    uint64_t L3 = lowbits_ & INT_MASK;
    uint64_t R0 = static_cast<uint64_t>(right.highbits_) >> 32;
    uint64_t R1 = static_cast<uint64_t>(right.highbits_) & INT_MASK;
    uint64_t R2 = right.lowbits_ >> 32;
    uint64_t R3 = right.lowbits_ & INT_MASK;

    uint64_t product = L3 * R3;
    lowbits_ = product & INT_MASK;
    uint64_t sum = product >> 32;
    product = L2 * R3;
    sum += product;
    highbits_ = sum < product ? CARRY_BIT : 0;
    product = L3 * R2;
    sum += product;
    if (sum < product) {
      highbits_ += CARRY_BIT;
    }
    lowbits_ += sum << 32;
    highbits_ += static_cast<int64_t>(sum >> 32);
    highbits_ += L1 * R3 + L2 * R2 + L3 * R1;
    highbits_ += (L0 * R3 + L1 * R2 + L2 * R1 + L3 * R0) << 32;
    return *this;
  }

  /**
   * Expands the given value into an array of ints so that we can work on
   * it. The array will be converted to an absolute value and the wasNegative
   * flag will be set appropriately. The array will remove leading zeros from
   * the value.
   * @param array an array of length 4 to set with the value
   * @param wasNegative a flag for whether the value was original negative
   * @result the output length of the array
   */
  int64_t Int128::fillInArray(uint32_t* array, bool& wasNegative) const {
    uint64_t high;
    uint64_t low;
    if (highbits_ < 0) {
      low = ~lowbits_ + 1;
      high = static_cast<uint64_t>(~highbits_);
      if (low == 0) {
        high += 1;
      }
      wasNegative = true;
    } else {
      low = lowbits_;
      high = static_cast<uint64_t>(highbits_);
      wasNegative = false;
    }
    if (high != 0) {
      if (high > UINT32_MAX) {
        array[0] = static_cast<uint32_t>(high >> 32);
        array[1] = static_cast<uint32_t>(high);
        array[2] = static_cast<uint32_t>(low >> 32);
        array[3] = static_cast<uint32_t>(low);
        return 4;
      } else {
        array[0] = static_cast<uint32_t>(high);
        array[1] = static_cast<uint32_t>(low >> 32);
        array[2] = static_cast<uint32_t>(low);
        return 3;
      }
    } else if (low >= UINT32_MAX) {
      array[0] = static_cast<uint32_t>(low >> 32);
      array[1] = static_cast<uint32_t>(low);
      return 2;
    } else if (low == 0) {
      return 0;
    } else {
      array[0] = static_cast<uint32_t>(low);
      return 1;
    }
  }

  /**
   * Find last set bit in a 32 bit integer. Bit 1 is the LSB and bit 32 is
   * the MSB. We can replace this with bsrq asm instruction on x64.
   */
  int64_t fls(uint32_t x) {
    int64_t bitpos = 0;
    while (x) {
      x >>= 1;
      bitpos += 1;
    }
    return bitpos;
  }

  /**
   * Shift the number in the array left by bits positions.
   * @param array the number to shift, must have length elements
   * @param length the number of entries in the array
   * @param bits the number of bits to shift (0 <= bits < 32)
   */
  void shiftArrayLeft(uint32_t* array, int64_t length, int64_t bits) {
    if (length > 0 && bits != 0) {
      for (int64_t i = 0; i < length - 1; ++i) {
        array[i] = (array[i] << bits) | (array[i + 1] >> (32 - bits));
      }
      array[length - 1] <<= bits;
    }
  }

  /**
   * Shift the number in the array right by bits positions.
   * @param array the number to shift, must have length elements
   * @param length the number of entries in the array
   * @param bits the number of bits to shift (0 <= bits < 32)
   */
  void shiftArrayRight(uint32_t* array, int64_t length, int64_t bits) {
    if (length > 0 && bits != 0) {
      for (int64_t i = length - 1; i > 0; --i) {
        array[i] = (array[i] >> bits) | (array[i - 1] << (32 - bits));
      }
      array[0] >>= bits;
    }
  }

  /**
   * Fix the signs of the result and remainder at the end of the division
   * based on the signs of the dividend and divisor.
   */
  void fixDivisionSigns(Int128& result, Int128& remainder, bool dividendWasNegative,
                        bool divisorWasNegative) {
    if (dividendWasNegative != divisorWasNegative) {
      result.negate();
    }
    if (dividendWasNegative) {
      remainder.negate();
    }
  }

  /**
   * Build a Int128 from a list of ints.
   */
  void buildFromArray(Int128& value, uint32_t* array, int64_t length) {
    switch (length) {
      case 0:
        value = 0;
        break;
      case 1:
        value = array[0];
        break;
      case 2:
        value = Int128(0, (static_cast<uint64_t>(array[0]) << 32) + array[1]);
        break;
      case 3:
        value = Int128(array[0], (static_cast<uint64_t>(array[1]) << 32) + array[2]);
        break;
      case 4:
        value = Int128((static_cast<int64_t>(array[0]) << 32) + array[1],
                       (static_cast<uint64_t>(array[2]) << 32) + array[3]);
        break;
      case 5:
        if (array[0] != 0) {
          throw std::logic_error("Can't build Int128 with 5 ints.");
        }
        value = Int128((static_cast<int64_t>(array[1]) << 32) + array[2],
                       (static_cast<uint64_t>(array[3]) << 32) + array[4]);
        break;
      default:
        throw std::logic_error("Unsupported length for building Int128");
    }
  }

  /**
   * Do a division where the divisor fits into a single 32 bit value.
   */
  Int128 singleDivide(uint32_t* dividend, int64_t dividendLength, uint32_t divisor,
                      Int128& remainder, bool dividendWasNegative, bool divisorWasNegative) {
    uint64_t r = 0;
    uint32_t resultArray[5];
    for (int64_t j = 0; j < dividendLength; j++) {
      r <<= 32;
      r += dividend[j];
      resultArray[j] = static_cast<uint32_t>(r / divisor);
      r %= divisor;
    }
    Int128 result;
    buildFromArray(result, resultArray, dividendLength);
    remainder = static_cast<int64_t>(r);
    fixDivisionSigns(result, remainder, dividendWasNegative, divisorWasNegative);
    return result;
  }

  Int128 Int128::divide(const Int128& divisor, Int128& remainder) const {
    // Split the dividend and divisor into integer pieces so that we can
    // work on them.
    uint32_t dividendArray[5];
    uint32_t divisorArray[4];
    bool dividendWasNegative;
    bool divisorWasNegative;
    // leave an extra zero before the dividend
    dividendArray[0] = 0;
    int64_t dividendLength = fillInArray(dividendArray + 1, dividendWasNegative) + 1;
    int64_t divisorLength = divisor.fillInArray(divisorArray, divisorWasNegative);

    // Handle some of the easy cases.
    if (dividendLength <= divisorLength) {
      remainder = *this;
      return 0;
    } else if (divisorLength == 0) {
      throw std::range_error("Division by 0 in Int128");
    } else if (divisorLength == 1) {
      return singleDivide(dividendArray, dividendLength, divisorArray[0], remainder,
                          dividendWasNegative, divisorWasNegative);
    }

    int64_t resultLength = dividendLength - divisorLength;
    uint32_t resultArray[4];

    // Normalize by shifting both by a multiple of 2 so that
    // the digit guessing is better. The requirement is that
    // divisorArray[0] is greater than 2**31.
    int64_t normalizeBits = 32 - fls(divisorArray[0]);
    shiftArrayLeft(divisorArray, divisorLength, normalizeBits);
    shiftArrayLeft(dividendArray, dividendLength, normalizeBits);

    // compute each digit in the result
    for (int64_t j = 0; j < resultLength; ++j) {
      // Guess the next digit. At worst it is two too large
      uint32_t guess = UINT32_MAX;
      uint64_t highDividend = static_cast<uint64_t>(dividendArray[j]) << 32 | dividendArray[j + 1];
      if (dividendArray[j] != divisorArray[0]) {
        guess = static_cast<uint32_t>(highDividend / divisorArray[0]);
      }

      // catch all of the cases where guess is two too large and most of the
      // cases where it is one too large
      uint32_t rhat =
          static_cast<uint32_t>(highDividend - guess * static_cast<uint64_t>(divisorArray[0]));
      while (static_cast<uint64_t>(divisorArray[1]) * guess >
             (static_cast<uint64_t>(rhat) << 32) + dividendArray[j + 2]) {
        guess -= 1;
        rhat += divisorArray[0];
        if (static_cast<uint64_t>(rhat) < divisorArray[0]) {
          break;
        }
      }

      // subtract off the guess * divisor from the dividend
      uint64_t mult = 0;
      for (int64_t i = divisorLength - 1; i >= 0; --i) {
        mult += static_cast<uint64_t>(guess) * divisorArray[i];
        uint32_t prev = dividendArray[j + i + 1];
        dividendArray[j + i + 1] -= static_cast<uint32_t>(mult);
        mult >>= 32;
        if (dividendArray[j + i + 1] > prev) {
          mult += 1;
        }
      }
      uint32_t prev = dividendArray[j];
      dividendArray[j] -= static_cast<uint32_t>(mult);

      // if guess was too big, we add back divisor
      if (dividendArray[j] > prev) {
        guess -= 1;
        uint32_t carry = 0;
        for (int64_t i = divisorLength - 1; i >= 0; --i) {
          uint64_t sum = static_cast<uint64_t>(divisorArray[i]) + dividendArray[j + i + 1] + carry;
          dividendArray[j + i + 1] = static_cast<uint32_t>(sum);
          carry = static_cast<uint32_t>(sum >> 32);
        }
        dividendArray[j] += carry;
      }

      resultArray[j] = guess;
    }

    // denormalize the remainder
    shiftArrayRight(dividendArray, dividendLength, normalizeBits);

    // return result and remainder
    Int128 result;
    buildFromArray(result, resultArray, resultLength);
    buildFromArray(remainder, dividendArray, dividendLength);
    fixDivisionSigns(result, remainder, dividendWasNegative, divisorWasNegative);
    return result;
  }

  std::string Int128::toString() const {
    // 10**18 - the largest power of 10 less than 63 bits
    const Int128 tenTo18(0xde0b6b3a7640000);
    // 10**36
    const Int128 tenTo36(0xc097ce7bc90715, 0xb34b9f1000000000);
    Int128 remainder;
    std::stringstream buf;
    bool needFill = false;

    // get anything above 10**36 and print it
    Int128 top = divide(tenTo36, remainder);
    if (top != 0) {
      buf << top.toLong();
      remainder.abs();
      needFill = true;
    }

    // now get anything above 10**18 and print it
    Int128 tail;
    top = remainder.divide(tenTo18, tail);
    if (needFill || top != 0) {
      if (needFill) {
        buf << std::setw(18) << std::setfill('0');
      } else {
        needFill = true;
        tail.abs();
      }
      buf << top.toLong();
    }

    // finally print the tail, which is less than 10**18
    if (needFill) {
      buf << std::setw(18) << std::setfill('0');
    }
    buf << tail.toLong();
    return buf.str();
  }

  std::string Int128::toDecimalString(int32_t scale, bool trimTrailingZeros) const {
    std::string str = toString();
    std::string result;
    if (scale == 0) {
      return str;
    } else if (*this < 0) {
      int32_t len = static_cast<int32_t>(str.length());
      if (len - 1 > scale) {
        result = str.substr(0, static_cast<size_t>(len - scale)) + "." +
                 str.substr(static_cast<size_t>(len - scale), static_cast<size_t>(len));
      } else if (len - 1 == scale) {
        result = "-0." + str.substr(1, std::string::npos);
      } else {
        result = "-0.";
        for (int32_t i = 0; i < scale - len + 1; ++i) {
          result += "0";
        }
        result += str.substr(1, std::string::npos);
      }
    } else {
      int32_t len = static_cast<int32_t>(str.length());
      if (len > scale) {
        result = str.substr(0, static_cast<size_t>(len - scale)) + "." +
                 str.substr(static_cast<size_t>(len - scale), static_cast<size_t>(len));
      } else if (len == scale) {
        result = "0." + str;
      } else {
        result = "0.";
        for (int32_t i = 0; i < scale - len; ++i) {
          result += "0";
        }
        result += str;
      }
    }
    if (trimTrailingZeros) {
      size_t pos = result.find_last_not_of('0');
      if (result[pos] == '.') {
        result = result.substr(0, pos);
      } else {
        result = result.substr(0, pos + 1);
      }
    }
    return result;
  }

  std::string Int128::toHexString() const {
    std::stringstream buf;
    buf << std::hex << "0x" << std::setw(16) << std::setfill('0') << highbits_ << std::setw(16)
        << std::setfill('0') << lowbits_;
    return buf.str();
  }

  double Int128::toDouble() const {
    if (fitsInLong()) {
      return static_cast<double>(toLong());
    }
    return static_cast<double>(lowbits_) + std::ldexp(static_cast<double>(highbits_), 64);
  }

  const static int32_t MAX_PRECISION_64 = 18;
  const static int32_t MAX_PRECISION_128 = 38;
  const static int64_t POWERS_OF_TEN[MAX_PRECISION_64 + 1] = {1,
                                                              10,
                                                              100,
                                                              1000,
                                                              10000,
                                                              100000,
                                                              1000000,
                                                              10000000,
                                                              100000000,
                                                              1000000000,
                                                              10000000000,
                                                              100000000000,
                                                              1000000000000,
                                                              10000000000000,
                                                              100000000000000,
                                                              1000000000000000,
                                                              10000000000000000,
                                                              100000000000000000,
                                                              1000000000000000000};

  Int128 scaleUpInt128ByPowerOfTen(Int128 value, int32_t power, bool& overflow) {
    overflow = false;
    Int128 remainder;

    while (power > 0) {
      int32_t step = std::min(power, MAX_PRECISION_64);
      if (value > 0 && Int128::maximumValue().divide(POWERS_OF_TEN[step], remainder) < value) {
        overflow = true;
        return Int128::maximumValue();
      } else if (value < 0 &&
                 Int128::minimumValue().divide(POWERS_OF_TEN[step], remainder) > value) {
        overflow = true;
        return Int128::minimumValue();
      }

      value *= POWERS_OF_TEN[step];
      power -= step;
    }

    return value;
  }

  Int128 scaleDownInt128ByPowerOfTen(Int128 value, int32_t power) {
    Int128 remainder;
    while (power > 0) {
      int32_t step = std::min(std::abs(power), MAX_PRECISION_64);
      value = value.divide(POWERS_OF_TEN[step], remainder);
      power -= step;
    }
    return value;
  }

  std::pair<bool, Int128> convertDecimal(Int128 value, int32_t fromScale, int32_t toPrecision,
                                         int32_t toScale, bool round) {
    if (toPrecision > MAX_PRECISION_128 || toPrecision < 1 || toScale < 0 ||
        toScale > toPrecision || fromScale < 0 ||
        std::abs(fromScale - toScale) > MAX_PRECISION_128) {
      std::stringstream buf;
      buf << "Invalid argument: fromScale=" << fromScale << ", toPrecision=" << toPrecision
          << ", toScale=" << toScale;
      throw std::invalid_argument(buf.str());
    }
    std::pair<bool, Int128> result;
    bool negative = value < 0;
    result.second = value.abs();
    result.first = false;

    Int128 upperBound = scaleUpInt128ByPowerOfTen(1, toPrecision, result.first);
    int8_t roundOffset = 0;
    int32_t deltaScale = fromScale - toScale;

    if (deltaScale > 0) {
      Int128 scale = scaleUpInt128ByPowerOfTen(1, deltaScale, result.first), remainder;
      result.second = result.second.divide(scale, remainder);
      remainder *= 2;
      if (round && remainder >= scale) {
        upperBound -= 1;
        roundOffset = 1;
      }
    } else if (deltaScale < 0) {
      if (result.second > upperBound) {
        result.first = true;
        return result;
      }
      result.second = scaleUpInt128ByPowerOfTen(result.second, -deltaScale, result.first);
    }

    if (result.second > upperBound) {
      result.first = true;
      return result;
    }

    result.second += roundOffset;
    if (negative) {
      result.second *= -1;
    }
    return result;
  }

  template <typename T>
  std::enable_if_t<std::is_floating_point_v<T>, std::pair<bool, Int128>> convertDecimal(
      T value, int32_t precision, int32_t scale) {
    const static T upperbound = std::ldexp(static_cast<T>(1), 127);
    const static T lowerbound = -upperbound;

    std::pair<bool, Int128> result = {false, 0};
    if (precision > MAX_PRECISION_128 || precision < 1 || scale > precision || scale < 0) {
      result.first = true;
      return result;
    }

    if (std::isnan(value) || value <= lowerbound || value >= upperbound) {
      result.first = true;
      return result;
    }

    bool isNegative = (value < 0);
    Int128 i128, remainder;
    value = std::fabs(value);
    if (value >= std::ldexp(static_cast<T>(1.0), 64)) {
      int64_t hi = static_cast<int64_t>(std::ldexp(value, -64));
      uint64_t lo = static_cast<uint64_t>(value - std::ldexp(static_cast<T>(hi), 64));
      i128 = Int128(hi, lo);
    } else {
      i128 = Int128(0, static_cast<uint64_t>(value));
    }
    value = value - std::floor(value);

    bool overflow = false;
    i128 = scaleUpInt128ByPowerOfTen(i128, scale, overflow);
    if (overflow || i128 >= scaleUpInt128ByPowerOfTen(1, precision, overflow)) {
      result.first = true;
      return result;
    }

    value = value * static_cast<T>(pow(10, scale));
    i128 += static_cast<int64_t>(std::round(value));
    if (isNegative) {
      i128 = i128.negate();
    }
    result.second = i128;
    return result;
  }

  template std::pair<bool, Int128> convertDecimal(float value, int32_t precision, int32_t scale);

  template std::pair<bool, Int128> convertDecimal(double value, int32_t precision, int32_t scale);

}  // namespace orc
