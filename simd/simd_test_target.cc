// Copyright 2017 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// WARNING: this translation unit is compiled with different flags. To prevent
// ODR violations, all functions defined here or in dependent headers must be
// inlined and/or within namespace SIMD_NAMESPACE.

#include "simd/simd_test_target.h"
#include "simd/simd.h"

namespace pik {
namespace SIMD_NAMESPACE {
namespace {

// Prevents the compiler from eliding the computations that led to "output".
// Works by indicating to the compiler that "output" is being read and modified.
// The +r constraint avoids unnecessary writes to memory, but only works for
// built-in types.
template <class T>
inline void PreventElision(T&& output) {
#ifndef _MSC_VER
  asm volatile("" : "+r"(output) : : "memory");
#endif
}

// Avoids having to pass this to every test.
NotifyFailure g_notify_failure;

// Compare non-vector T.
template <typename T>
void AssertEqual(const T expected, const T actual, const int line = -1,
                 const int lane = 0, const char* name = "builtin") {
  char expected_buf[64];
  char actual_buf[64];
  ToString(expected, expected_buf);
  ToString(actual, actual_buf);
  // Rely on string comparison to ensure similar floats are "equal".
  if (!StringsEqual(expected_buf, actual_buf)) {
    g_notify_failure(SIMD_TARGET::value, line, name, lane, expected_buf,
                     actual_buf);
  }
}

#define ASSERT_EQ(expected, actual) AssertEqual(expected, actual, __LINE__)

// Compare expected vector to vector.
template <class D, class V>
void AssertVecEqual(D d, const V expected, const V actual, const int line) {
  SIMD_ALIGN typename D::T expected_lanes[d.N];
  SIMD_ALIGN typename D::T actual_lanes[d.N];
  store(expected, d, expected_lanes);
  store(actual, d, actual_lanes);
  for (size_t i = 0; i < d.N; ++i) {
    AssertEqual(expected_lanes[i], actual_lanes[i], line, i, vec_name<D>());
  }
}

// Compare expected lanes to vector.
template <class D, class V>
void AssertVecEqual(D d, const typename D::T (&expected)[D::N], const V actual,
                    const int line) {
  AssertVecEqual(d, load_unaligned(d, expected), actual, line);
}

#define ASSERT_VEC_EQ(d, expected, actual) \
  AssertVecEqual(d, expected, actual, __LINE__)

// Type lists

template <class Test, typename T>
void Call() {
  Test().template operator()(T(), Full<T>());
}

// Calls Test::operator()(T, D) for each lane type.
template <class Test>
void ForeachUnsignedLaneType() {
  Call<Test, uint8_t>();
  Call<Test, uint16_t>();
  Call<Test, uint32_t>();
  Call<Test, uint64_t>();
}

template <class Test>
void ForeachSignedLaneType() {
  Call<Test, int8_t>();
  Call<Test, int16_t>();
  Call<Test, int32_t>();
  Call<Test, int64_t>();
}

template <class Test>
void ForeachFloatLaneType() {
  Call<Test, float>();
  Call<Test, double>();
}

template <class Test>
void ForeachLaneType() {
  ForeachUnsignedLaneType<Test>();
  ForeachSignedLaneType<Test>();
  ForeachFloatLaneType<Test>();
}

namespace examples {

namespace {
void FloorLog2(const uint8_t* SIMD_RESTRICT values,
               uint8_t* SIMD_RESTRICT log2) {
  // Descriptors for all required data types:
  const Full<int32_t> d32;
  const Full<float> df;
  const Part<uint8_t, d32.N> d8;

  const auto u8 = load(d8, values);
  const auto bits = cast_to(d32, convert_to(df, convert_to(d32, u8)));
  const auto exponent = shift_right<23>(bits) - set1(d32, 127);
  store(convert_to(d8, exponent), d8, log2);
}
}  // namespace

void TestFloorLog2() {
  const size_t kStep = Full<int32_t>::N;
  const size_t kBytes = 32;
  static_assert(kBytes % kStep == 0, "Must be a multiple of kStep");

  uint8_t in[kBytes];
  uint8_t expected[kBytes];
  RandomState rng = {1234};
  for (size_t i = 0; i < kBytes; ++i) {
    expected[i] = Random32(&rng) & 7;
    in[i] = 1u << expected[i];
  }
  uint8_t out[32];
  for (size_t i = 0; i < kBytes; i += kStep) {
    FloorLog2(in + i, out + i);
  }
  int sum = 0;
  for (size_t i = 0; i < kBytes; ++i) {
    ASSERT_EQ(expected[i], out[i]);
    sum += out[i];
  }
  PreventElision(sum);
}

void Copy(const uint8_t* SIMD_RESTRICT from, const size_t size,
          uint8_t* SIMD_RESTRICT to) {
  // Width-agnostic (library-specified N)
  const Full<uint8_t> d;
  const Scalar<uint8_t> ds;
  size_t i = 0;
  for (; i + d.N <= size; i += d.N) {
    const auto bytes = load(d, from + i);
    store(bytes, d, to + i);
  }

  for (; i < size; ++i) {
    // (Same loop body as above, could factor into a shared template)
    const auto bytes = load(ds, from + i);
    store(bytes, ds, to + i);
  }
}

void TestCopy() {
  RandomState rng = {1234};
  const size_t kSize = 34;
  SIMD_ALIGN uint8_t from[kSize];
  for (size_t i = 0; i < kSize; ++i) {
    from[i] = Random32(&rng) & 0xFF;
  }
  SIMD_ALIGN uint8_t to[kSize];
  Copy(from, kSize, to);
  for (size_t i = 0; i < kSize; ++i) {
    ASSERT_EQ(from[i], to[i]);
  }
}

template <typename T>
void MulAdd(const T* SIMD_RESTRICT mul_array, const T* SIMD_RESTRICT add_array,
            const size_t size, T* SIMD_RESTRICT x_array) {
  // Type-agnostic (caller-specified lane type) and width-agnostic (uses
  // best available instruction set).
  const Full<T> d;
  for (size_t i = 0; i < size; i += d.N) {
    const auto mul = load(d, mul_array + i);
    const auto add = load(d, add_array + i);
    auto x = load(d, x_array + i);
    x = mul_add(mul, x, add);
    store(x, d, x_array + i);
  }
}

template <typename T>
T SumMulAdd() {
  RandomState rng = {1234};
  const size_t kSize = 64;
  SIMD_ALIGN T mul[kSize];
  SIMD_ALIGN T x[kSize];
  SIMD_ALIGN T add[kSize];
  for (size_t i = 0; i < kSize; ++i) {
    mul[i] = Random32(&rng) & 0xF;
    x[i] = Random32(&rng) & 0xFF;
    add[i] = Random32(&rng) & 0xFF;
  }
  MulAdd(mul, add, kSize, x);
  double sum = 0.0;
  for (auto xi : x) {
    sum += xi;
  }
  return sum;
}

void TestExamples() {
  TestFloorLog2();
  TestCopy();

  ASSERT_EQ(75598.0f, SumMulAdd<float>());
  ASSERT_EQ(75598.0, SumMulAdd<double>());
}

}  // namespace examples

namespace basic {

// util.h

void TestLimits() {
  ASSERT_EQ(uint8_t(0), LimitsMin<uint8_t>());
  ASSERT_EQ(uint16_t(0), LimitsMin<uint16_t>());
  ASSERT_EQ(uint32_t(0), LimitsMin<uint32_t>());
  ASSERT_EQ(uint64_t(0), LimitsMin<uint64_t>());

  ASSERT_EQ(int8_t(-128), LimitsMin<int8_t>());
  ASSERT_EQ(int16_t(-32768), LimitsMin<int16_t>());
  ASSERT_EQ(int32_t(0x80000000u), LimitsMin<int32_t>());
  ASSERT_EQ(int64_t(0x8000000000000000ull), LimitsMin<int64_t>());

  ASSERT_EQ(uint8_t(0xFF), LimitsMax<uint8_t>());
  ASSERT_EQ(uint16_t(0xFFFF), LimitsMax<uint16_t>());
  ASSERT_EQ(uint32_t(0xFFFFFFFFu), LimitsMax<uint32_t>());
  ASSERT_EQ(uint64_t(0xFFFFFFFFFFFFFFFFull), LimitsMax<uint64_t>());

  ASSERT_EQ(int8_t(0x7F), LimitsMax<int8_t>());
  ASSERT_EQ(int16_t(0x7FFF), LimitsMax<int16_t>());
  ASSERT_EQ(int32_t(0x7FFFFFFFu), LimitsMax<int32_t>());
  ASSERT_EQ(int64_t(0x7FFFFFFFFFFFFFFFull), LimitsMax<int64_t>());
}

// Test the ToString used to output test failures

void TestToString() {
  char buf[32];
  const char* end;

  end = ToString(int64_t(0), buf);
  ASSERT_EQ('0', end[-1]);
  ASSERT_EQ('\0', end[0]);

  end = ToString(int64_t(3), buf);
  ASSERT_EQ('3', end[-1]);
  ASSERT_EQ('\0', end[0]);

  end = ToString(int64_t(-1), buf);
  ASSERT_EQ('-', end[-2]);
  ASSERT_EQ('1', end[-1]);
  ASSERT_EQ('\0', end[0]);

  ToString(0x7FFFFFFFFFFFFFFFLL, buf);
  ASSERT_EQ(true, StringsEqual("9223372036854775807", buf));

  ToString(int64_t(0x8000000000000000ULL), buf);
  ASSERT_EQ(true, StringsEqual("-9223372036854775808", buf));

  ToString(0.0, buf);
  ASSERT_EQ(true, StringsEqual("0.0", buf));
  ToString(4.0, buf);
  ASSERT_EQ(true, StringsEqual("4.0", buf));
  ToString(-1.0, buf);
  ASSERT_EQ(true, StringsEqual("-1.0", buf));
  ToString(-1.25, buf);
  ASSERT_EQ("-1.2500000000000000", const_cast<const char*>(buf));
  ToString(2.125f, buf);
  ASSERT_EQ("2.12500000", const_cast<const char*>(buf));
}

struct TestIsUnsigned {
  template <typename T, class D>
  void operator()(T, D d) const {
    static_assert(!IsFloat<T>(), "Expected !IsFloat");
    static_assert(!IsSigned<T>(), "Expected !IsSigned");
  }
};

struct TestIsSigned {
  template <typename T, class D>
  void operator()(T, D d) const {
    static_assert(!IsFloat<T>(), "Expected !IsFloat");
    static_assert(IsSigned<T>(), "Expected IsSigned");
  }
};

struct TestIsFloat {
  template <typename T, class D>
  void operator()(T, D d) const {
    static_assert(IsFloat<T>(), "Expected IsFloat");
    static_assert(IsSigned<T>(), "Floats are also considered signed");
  }
};

void TestType() {
  ForeachUnsignedLaneType<TestIsUnsigned>();
  ForeachSignedLaneType<TestIsSigned>();
  ForeachFloatLaneType<TestIsFloat>();
}

// Ensures wraparound (mod 2^bits)
struct TestOverflowT {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v1 = set1(d, T(1));
    const auto vmax = set1(d, LimitsMax<T>());
    const auto vmin = set1(d, LimitsMin<T>());
    // Unsigned underflow / negative -> positive
    ASSERT_VEC_EQ(d, vmax, vmin - v1);
    // Unsigned overflow / positive -> negative
    ASSERT_VEC_EQ(d, vmin, vmax + v1);
  }
};

void TestOverflow() {
  ForeachUnsignedLaneType<TestOverflowT>();
  ForeachSignedLaneType<TestOverflowT>();
}

struct TestName {
  template <typename T, class D>
  void operator()(T, D d) const {
    char expected[7] = {IsFloat<T>() ? 'f' : (IsSigned<T>() ? 'i' : 'u')};
    char* end = ToString(sizeof(T) * 8, expected + 1);
    if (D::Target::value != SIMD_NONE) {
      *end++ = 'x';
      end = ToString(d.N, end);
    }
    if (!StringsEqual(expected, vec_name<D>())) {
      g_notify_failure(SIMD_TARGET::value, __LINE__, expected, -1, expected,
                       vec_name<D>());
    }
  }
};

struct TestSet {
  template <typename T, class D>
  void operator()(T, D d) const {
    // setzero
    const auto v0 = setzero(d);
    T expected[d.N] = {T(0)};
    ASSERT_VEC_EQ(d, expected, v0);

    // set1
    const auto v2 = set1(d, T(2));
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = 2;
    }
    ASSERT_VEC_EQ(d, expected, v2);

    // iota
    const auto vi = iota(d, T(5));
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = 5 + i;
    }
    ASSERT_VEC_EQ(d, expected, vi);

    // undefined
    const auto vu = undefined(d);
    store(vu, d, expected);
  }
};

struct TestCopyAndAssign {
  template <typename T, class D>
  void operator()(T, D d) const {
    using V = typename D::V;

    // copy V
    const auto v3 = iota(d, 3);
    V v3b(v3);
    ASSERT_VEC_EQ(d, v3, v3b);

    // assign V
    V v3c;
    v3c = v3;
    ASSERT_VEC_EQ(d, v3, v3c);
  }
};

struct TestHalf {
  template <typename T, class D>
  void operator()(T, D d) const {
#if SIMD_TARGET_VALUE != SIMD_NONE
    size_t i;
    constexpr size_t N2 = (d.N + 1) / 2;
    const Part<T, N2> d2;

    const auto v = iota(d, 1);
    SIMD_ALIGN T lanes[d.N] = {0};

    store(lower_half(v), d2, lanes);
    i = 0;
    for (; i < N2; ++i) {
      ASSERT_EQ(T(1 + i), lanes[i]);
    }
    // Other half remains unchanged
    for (; i < d.N; ++i) {
      ASSERT_EQ(T(0), lanes[i]);
    }
    store(lower_half(v), d2, lanes);  // Also test the wrapper
    i = 0;
    for (; i < N2; ++i) {
      ASSERT_EQ(T(1 + i), lanes[i]);
    }
    // Other half remains unchanged
    for (; i < d.N; ++i) {
      ASSERT_EQ(T(0), lanes[i]);
    }

    store(upper_half(v), d2, lanes);
    i = 0;
    for (; i < N2; ++i) {
      ASSERT_EQ(T(N2 + 1 + i), lanes[i]);
    }
    // Other half remains unchanged
    for (; i < d.N; ++i) {
      ASSERT_EQ(T(0), lanes[i]);
    }
    store(upper_half(v), d2, lanes);  // Also test the wrapper
    i = 0;
    for (; i < N2; ++i) {
      ASSERT_EQ(T(N2 + 1 + i), lanes[i]);
    }
    // Other half remains unchanged
    for (; i < d.N; ++i) {
      ASSERT_EQ(T(0), lanes[i]);
    }

    store(any_part(d2, v), d2, lanes);
    i = 0;
    for (; i < N2; ++i) {
      ASSERT_EQ(T(1 + i), lanes[i]);
    }
    // Other half remains unchanged
    for (; i < d.N; ++i) {
      ASSERT_EQ(T(0), lanes[i]);
    }

    // Ensure part lanes are contiguous
    const auto vi = iota(d2, 1);
    store(vi, d2, lanes);
    for (size_t i = 1; i < N2; ++i) {
      ASSERT_EQ(T(lanes[i - 1] + 1), lanes[i]);
    }
#endif
  }
};

struct TestQuarterT {
  template <typename T, class D>
  void operator()(T, D d) const {
    constexpr size_t N4 = (d.N + 3) / 4;
    const Part<T, N4> d4;

    const auto v = iota(d, 1);
    SIMD_ALIGN T lanes[d.N];
    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = 123;
    }
    const auto lo = any_part(d4, v);
    store(lo, d4, lanes);
    size_t i = 0;
    for (; i < N4; ++i) {
      ASSERT_EQ(T(i + 1), lanes[i]);
    }
    // Other lanes remain unchanged
    for (; i < d.N; ++i) {
      ASSERT_EQ(T(123), lanes[i]);
    }
  }
};

void TestQuarter() {
  Call<TestQuarterT, uint8_t>();
  Call<TestQuarterT, uint16_t>();
  Call<TestQuarterT, uint32_t>();
  Call<TestQuarterT, int8_t>();
  Call<TestQuarterT, int16_t>();
  Call<TestQuarterT, int32_t>();
  Call<TestQuarterT, float>();
}

void TestBasic() {
  TestLimits();
  TestToString();
  TestType();
  ForeachLaneType<TestName>();
  TestOverflow();
  ForeachLaneType<TestSet>();
  ForeachLaneType<TestCopyAndAssign>();
  ForeachLaneType<TestHalf>();
  TestQuarter();
}

}  // namespace basic

namespace arithmetic {

struct TestPlusMinus {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v2 = iota(D(), 2);
    const auto v3 = iota(D(), 3);
    const auto v4 = iota(D(), 4);

    T lanes[d.N];
    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = (2 + i) + (3 + i);
    }
    ASSERT_VEC_EQ(d, lanes, v2 + v3);
    ASSERT_VEC_EQ(d, v3, (v2 + v3) - v2);

    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = (2 + i) + (4 + i);
    }
    auto sum = v2;
    sum += v4;  // sum == 6,8..
    ASSERT_VEC_EQ(d, lanes, sum);

    sum -= v4;
    ASSERT_VEC_EQ(d, v2, sum);
  }
};

struct TestUnsignedSaturatingArithmetic {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v0 = setzero(d);
    const auto vi = iota(d, 1);
    const auto vm = set1(d, LimitsMax<T>());

    ASSERT_VEC_EQ(d, v0 + v0, saturated_add(v0, v0));
    ASSERT_VEC_EQ(d, v0 + vi, saturated_add(v0, vi));
    ASSERT_VEC_EQ(d, v0 + vm, saturated_add(v0, vm));
    ASSERT_VEC_EQ(d, vm, saturated_add(vi, vm));
    ASSERT_VEC_EQ(d, vm, saturated_add(vm, vm));

    ASSERT_VEC_EQ(d, v0, saturated_subtract(v0, v0));
    ASSERT_VEC_EQ(d, v0, saturated_subtract(v0, vi));
    ASSERT_VEC_EQ(d, v0, saturated_subtract(vi, vi));
    ASSERT_VEC_EQ(d, v0, saturated_subtract(vi, vm));
    ASSERT_VEC_EQ(d, vm - vi, saturated_subtract(vm, vi));
  }
};

struct TestSignedSaturatingArithmetic {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v0 = setzero(d);
    const auto vi = iota(d, 1);
    const auto vpm = set1(d, LimitsMax<T>());
    const auto vn = iota(d, -T(d.N));
    const auto vnm = set1(d, LimitsMin<T>());

    ASSERT_VEC_EQ(d, v0, saturated_add(v0, v0));
    ASSERT_VEC_EQ(d, vi, saturated_add(v0, vi));
    ASSERT_VEC_EQ(d, vpm, saturated_add(v0, vpm));
    ASSERT_VEC_EQ(d, vpm, saturated_add(vi, vpm));
    ASSERT_VEC_EQ(d, vpm, saturated_add(vpm, vpm));

    ASSERT_VEC_EQ(d, v0, saturated_subtract(v0, v0));
    ASSERT_VEC_EQ(d, v0 - vi, saturated_subtract(v0, vi));
    ASSERT_VEC_EQ(d, vn, saturated_subtract(vn, v0));
    ASSERT_VEC_EQ(d, vnm, saturated_subtract(vnm, vi));
    ASSERT_VEC_EQ(d, vnm, saturated_subtract(vnm, vpm));
  }
};

void TestSaturatingArithmetic() {
  Call<TestUnsignedSaturatingArithmetic, uint8_t>();
  Call<TestUnsignedSaturatingArithmetic, uint16_t>();
  Call<TestSignedSaturatingArithmetic, int8_t>();
  Call<TestSignedSaturatingArithmetic, int16_t>();
}

struct TestAverageT {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v0 = setzero(d);
    const auto v1 = set1(d, T(1));
    const auto v2 = set1(d, T(2));

    ASSERT_VEC_EQ(d, v0, average_round(v0, v0));
    ASSERT_VEC_EQ(d, v1, average_round(v0, v1));
    ASSERT_VEC_EQ(d, v1, average_round(v1, v1));
    ASSERT_VEC_EQ(d, v2, average_round(v1, v2));
    ASSERT_VEC_EQ(d, v2, average_round(v2, v2));
  }
};

void TestAverage() {
  Call<TestAverageT, uint8_t>();
  Call<TestAverageT, uint16_t>();
}

struct TestAbsT {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v0 = setzero(d);
    const auto vp1 = set1(d, T(1));
    const auto vn1 = set1(d, T(-1));
    const auto vpm = set1(d, LimitsMax<T>());
    const auto vnm = set1(d, LimitsMin<T>());

    ASSERT_VEC_EQ(d, v0, abs(v0));
    ASSERT_VEC_EQ(d, vp1, abs(vp1));
    ASSERT_VEC_EQ(d, vp1, abs(vn1));
    ASSERT_VEC_EQ(d, vpm, abs(vpm));
    ASSERT_VEC_EQ(d, vnm, abs(vnm));
  }
};

void TestAbs() {
  Call<TestAbsT, int8_t>();
  Call<TestAbsT, int16_t>();
  Call<TestAbsT, int32_t>();
}

struct TestUnsignedShifts {
  template <typename T, class D>
  void operator()(T, D d) const {
    constexpr int kSign = (sizeof(T) * 8) - 1;
    const auto v0 = setzero(d);
    const auto vi = iota(d, 0);
    SIMD_ALIGN T expected[d.N];

    // Shifting out of right side => zero
    ASSERT_VEC_EQ(d, v0, shift_right<7>(vi));
    ASSERT_VEC_EQ(d, v0, shift_right_same(vi, set_shift_right_count(d, 7)));

    // Simple left shift
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T(i << 1);
    }
    ASSERT_VEC_EQ(d, expected, shift_left<1>(vi));
    ASSERT_VEC_EQ(d, expected, shift_left_same(vi, set_shift_left_count(d, 1)));

    // Simple right shift
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T(i >> 1);
    }
    ASSERT_VEC_EQ(d, expected, shift_right<1>(vi));
    ASSERT_VEC_EQ(d, expected,
                  shift_right_same(vi, set_shift_right_count(d, 1)));

    // Verify truncation for left-shift
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = static_cast<T>((i << kSign) & ~T(0));
    }
    ASSERT_VEC_EQ(d, expected, shift_left<kSign>(vi));
    ASSERT_VEC_EQ(d, expected,
                  shift_left_same(vi, set_shift_left_count(d, kSign)));
  }
};

struct TestSignedShifts {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v0 = setzero(d);
    const auto vi = iota(d, 0);
    SIMD_ALIGN T expected[d.N];

    // Shifting out of right side => zero
    ASSERT_VEC_EQ(d, v0, shift_right<7>(vi));
    ASSERT_VEC_EQ(d, v0, shift_right_same(vi, set_shift_right_count(d, 7)));

    // Simple left shift
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T(i << 1);
    }
    ASSERT_VEC_EQ(d, expected, shift_left<1>(vi));
    ASSERT_VEC_EQ(d, expected, shift_left_same(vi, set_shift_left_count(d, 1)));

    // Simple right shift
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T(i >> 1);
    }
    ASSERT_VEC_EQ(d, expected, shift_right<1>(vi));
    ASSERT_VEC_EQ(d, expected,
                  shift_right_same(vi, set_shift_right_count(d, 1)));

    // Sign extension
    constexpr T min = LimitsMin<T>();
    const auto vn = iota(d, min);
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T((min + i) >> 1);
    }
    ASSERT_VEC_EQ(d, expected, shift_right<1>(vn));
    ASSERT_VEC_EQ(d, expected,
                  shift_right_same(vn, set_shift_right_count(d, 1)));

    // Shifting negative left
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T((min + i) << 1);
    }
    ASSERT_VEC_EQ(d, expected, shift_left<1>(vn));
    ASSERT_VEC_EQ(d, expected, shift_left_same(vn, set_shift_left_count(d, 1)));
  }
};

#if SIMD_TARGET_VALUE != SIMD_SSE4

struct TestUnsignedVarShifts {
  template <typename T, class D>
  void operator()(T, D d) const {
    constexpr int kSign = (sizeof(T) * 8) - 1;
    const auto v0 = setzero(d);
    const auto v1 = set1(d, 1);
    const auto vi = iota(d, 0);
    SIMD_ALIGN T expected[d.N];

    // Shifting out of right side => zero
    ASSERT_VEC_EQ(d, v0, vi >> set1(d, 7));

    // Simple left shift
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T(i << 1);
    }
    ASSERT_VEC_EQ(d, expected, vi << set1(d, 1));

    // Simple right shift
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T(i >> 1);
    }
    ASSERT_VEC_EQ(d, expected, vi >> set1(d, 1));

    // Verify truncation for left-shift
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = static_cast<T>((i << kSign) & ~T(0));
    }
    ASSERT_VEC_EQ(d, expected, vi << set1(d, kSign));

    // Verify variable left shift (assumes < 32 lanes)
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T(1) << i;
    }
    ASSERT_VEC_EQ(d, expected, v1 << vi);
  }
};

struct TestSignedVarLeftShifts {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v1 = set1(d, 1);
    const auto vi = iota(d, 0);

    SIMD_ALIGN T expected[d.N];

    // Simple left shift
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T(i << 1);
    }
    ASSERT_VEC_EQ(d, expected, vi << v1);

    // Shifting negative numbers left
    constexpr T min = LimitsMin<T>();
    const auto vn = iota(d, min);
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T((min + i) << 1);
    }
    ASSERT_VEC_EQ(d, expected, vn << v1);

    // Differing shift counts (assumes < 32 lanes)
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T(1) << i;
    }
    ASSERT_VEC_EQ(d, expected, v1 << vi);
  }
};

struct TestSignedVarRightShifts {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v0 = setzero(d);
    const auto vi = iota(d, 0);
    const auto vmax = set1(d, LimitsMax<T>());
    SIMD_ALIGN T expected[d.N];

    // Shifting out of right side => zero
    ASSERT_VEC_EQ(d, v0, vi >> set1(d, 7));

    // Simple right shift
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T(i >> 1);
    }
    ASSERT_VEC_EQ(d, expected, vi >> set1(d, 1));

    // Sign extension
    constexpr T min = LimitsMin<T>();
    const auto vn = iota(d, min);
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = T((min + i) >> 1);
    }
    ASSERT_VEC_EQ(d, expected, vn >> set1(d, 1));

    // Differing shift counts (assumes < 32 lanes)
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = LimitsMax<T>() >> i;
    }
    ASSERT_VEC_EQ(d, expected, vmax >> vi);
  }
};

#endif

void TestShifts() {
  // No u8.
  Call<TestUnsignedShifts, uint16_t>();
  Call<TestUnsignedShifts, uint32_t>();
  Call<TestUnsignedShifts, uint64_t>();
  // No i8.
  Call<TestSignedShifts, int16_t>();
  Call<TestSignedShifts, int32_t>();
  // No i64/f32/f64.

#if SIMD_TARGET_VALUE != SIMD_SSE4
  Call<TestUnsignedVarShifts, uint32_t>();
  Call<TestUnsignedVarShifts, uint64_t>();
  Call<TestSignedVarLeftShifts, int32_t>();
  Call<TestSignedVarRightShifts, int32_t>();
  Call<TestSignedVarLeftShifts, int64_t>();
// No i64 (right-shift).
#endif
}

struct TestUnsignedMinMax {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v0 = setzero(d);
    const auto v1 = iota(d, 1);
    const auto v2 = iota(d, 2);
    const auto v_max = iota(d, LimitsMax<T>() - d.N + 1);
    ASSERT_VEC_EQ(d, v1, min(v1, v2));
    ASSERT_VEC_EQ(d, v2, max(v1, v2));
    ASSERT_VEC_EQ(d, v0, min(v1, v0));
    ASSERT_VEC_EQ(d, v1, max(v1, v0));
    ASSERT_VEC_EQ(d, v1, min(v1, v_max));
    ASSERT_VEC_EQ(d, v_max, max(v1, v_max));
    ASSERT_VEC_EQ(d, v0, min(v0, v_max));
    ASSERT_VEC_EQ(d, v_max, max(v0, v_max));
  }
};

struct TestSignedMinMax {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v1 = iota(d, 1);
    const auto v2 = iota(d, 2);
    const auto v_neg = iota(d, -T(d.N));
    const auto v_neg_max = iota(d, LimitsMin<T>());
    ASSERT_VEC_EQ(d, v1, min(v1, v2));
    ASSERT_VEC_EQ(d, v2, max(v1, v2));
    ASSERT_VEC_EQ(d, v_neg, min(v1, v_neg));
    ASSERT_VEC_EQ(d, v1, max(v1, v_neg));
    ASSERT_VEC_EQ(d, v_neg_max, min(v1, v_neg_max));
    ASSERT_VEC_EQ(d, v1, max(v1, v_neg_max));
    ASSERT_VEC_EQ(d, v_neg_max, min(v_neg, v_neg_max));
    ASSERT_VEC_EQ(d, v_neg, max(v_neg, v_neg_max));
  }
};

struct TestFloatMinMax {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v1 = iota(d, 1);
    const auto v2 = iota(d, 2);
    const auto v_neg = iota(d, -T(d.N));
    ASSERT_VEC_EQ(d, v1, min(v1, v2));
    ASSERT_VEC_EQ(d, v2, max(v1, v2));
    ASSERT_VEC_EQ(d, v_neg, min(v1, v_neg));
    ASSERT_VEC_EQ(d, v1, max(v1, v_neg));
  }
};

void TestMinMax() {
  Call<TestUnsignedMinMax, uint8_t>();
  Call<TestUnsignedMinMax, uint16_t>();
  Call<TestUnsignedMinMax, uint32_t>();
  // No u64.
  Call<TestSignedMinMax, int8_t>();
  Call<TestSignedMinMax, int16_t>();
  Call<TestSignedMinMax, int32_t>();
  // No i64.
  Call<TestFloatMinMax, float>();
  Call<TestFloatMinMax, double>();
}

struct TestUnsignedMul {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v0 = setzero(d);
    const auto v1 = set1(d, T(1));
    const auto vi = iota(d, 1);
    const auto vj = iota(d, 3);
    T lanes[d.N];
    ASSERT_VEC_EQ(d, v0, v0 * v0);
    ASSERT_VEC_EQ(d, v1, v1 * v1);
    ASSERT_VEC_EQ(d, vi, v1 * vi);
    ASSERT_VEC_EQ(d, vi, vi * v1);

    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = (1 + i) * (1 + i);
    }
    ASSERT_VEC_EQ(d, lanes, vi * vi);

    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = (1 + i) * (3 + i);
    }
    ASSERT_VEC_EQ(d, lanes, vi * vj);

    const T max = LimitsMax<T>();
    const auto vmax = set1(d, max);
    ASSERT_VEC_EQ(d, vmax, vmax * v1);
    ASSERT_VEC_EQ(d, vmax, v1 * vmax);

    const size_t bits = sizeof(T) * 8;
    const uint64_t mask = (1ull << bits) - 1;
    const T max2 = (uint64_t(max) * max) & mask;
    ASSERT_VEC_EQ(d, set1(d, max2), vmax * vmax);
  }
};

struct TestSignedMul {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v0 = setzero(d);
    const auto v1 = set1(d, T(1));
    const auto vi = iota(d, 1);
    const auto vn = iota(d, -T(d.N));
    T lanes[d.N];
    ASSERT_VEC_EQ(d, v0, v0 * v0);
    ASSERT_VEC_EQ(d, v1, v1 * v1);
    ASSERT_VEC_EQ(d, vi, v1 * vi);
    ASSERT_VEC_EQ(d, vi, vi * v1);

    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = (1 + i) * (1 + i);
    }
    ASSERT_VEC_EQ(d, lanes, vi * vi);

    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = (-T(d.N) + i) * (1 + i);
    }
    ASSERT_VEC_EQ(d, lanes, vn * vi);
    ASSERT_VEC_EQ(d, lanes, vi * vn);
  }
};

void TestMul() {
  // No u8.
  Call<TestUnsignedMul, uint16_t>();
  Call<TestUnsignedMul, uint32_t>();
  // No u64,i8.
  Call<TestSignedMul, int16_t>();
  Call<TestSignedMul, int32_t>();
  // No i64.
}

struct TestMulHi16 {
  template <typename T, class D>
  void operator()(T, D d) const {
    SIMD_ALIGN T in_lanes[d.N];
    SIMD_ALIGN T expected_lanes[d.N];
    const auto vi = iota(d, 1);
    const auto vni = iota(d, -T(d.N));

    const auto v0 = setzero(d);
    ASSERT_VEC_EQ(d, v0, ext::mul_high(v0, v0));
    ASSERT_VEC_EQ(d, v0, ext::mul_high(v0, vi));
    ASSERT_VEC_EQ(d, v0, ext::mul_high(vi, v0));

    // Large positive squared
    for (size_t i = 0; i < d.N; ++i) {
      in_lanes[i] = LimitsMax<T>() >> i;
      expected_lanes[i] = (int32_t(in_lanes[i]) * in_lanes[i]) >> 16;
    }
    auto v = load(d, in_lanes);
    ASSERT_VEC_EQ(d, expected_lanes, ext::mul_high(v, v));

    // Large positive * small positive
    for (size_t i = 0; i < d.N; ++i) {
      expected_lanes[i] = (int32_t(in_lanes[i]) * (1 + i)) >> 16;
    }
    ASSERT_VEC_EQ(d, expected_lanes, ext::mul_high(v, vi));
    ASSERT_VEC_EQ(d, expected_lanes, ext::mul_high(vi, v));

    // Large positive * small negative
    for (size_t i = 0; i < d.N; ++i) {
      expected_lanes[i] = (int32_t(in_lanes[i]) * (i - d.N)) >> 16;
    }
    ASSERT_VEC_EQ(d, expected_lanes, ext::mul_high(v, vni));
    ASSERT_VEC_EQ(d, expected_lanes, ext::mul_high(vni, v));
  }
};

template <typename T1, typename T2>
void TestMulEvenT() {
  const Full<T1> d1;
  const Full<T2> d2;  // wider type, half the lanes

  const auto v0 = setzero(d1);
  ASSERT_VEC_EQ(d2, setzero(d2), mul_even(v0, v0));

  // scalar has N=1 and we write to "lane 1" below, though it isn't used by
  // the actual mul_even.
  SIMD_ALIGN T1 in_lanes[SIMD_MAX(d1.N, 2)];
  SIMD_ALIGN T2 expected[d2.N];
  for (size_t i = 0; i < d1.N; i += 2) {
    in_lanes[i + 0] = LimitsMax<T1>() >> i;
    in_lanes[i + 1] = 1;  // will be overwritten with upper half of result
    expected[i / 2] = T2(in_lanes[i + 0]) * in_lanes[i + 0];
  }

  const auto v = load(d1, in_lanes);
  ASSERT_VEC_EQ(d2, expected, mul_even(v, v));
}

void TestMulEven() {
  TestMulEvenT<int32_t, int64_t>();
  TestMulEvenT<uint32_t, uint64_t>();
}

struct TestMulAdd {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v0 = setzero(d);
    const auto v1 = iota(d, 1);
    const auto v2 = iota(d, 2);
    T lanes[d.N];
    ASSERT_VEC_EQ(d, v0, mul_add(v0, v0, v0));
    ASSERT_VEC_EQ(d, v2, mul_add(v0, v1, v2));
    ASSERT_VEC_EQ(d, v2, mul_add(v1, v0, v2));
    ASSERT_VEC_EQ(d, v0, nmul_add(v0, v0, v0));
    ASSERT_VEC_EQ(d, v2, nmul_add(v0, v1, v2));
    ASSERT_VEC_EQ(d, v2, nmul_add(v1, v0, v2));

    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = (i + 1) * (i + 2);
    }
    ASSERT_VEC_EQ(d, lanes, mul_add(v2, v1, v0));
    ASSERT_VEC_EQ(d, lanes, mul_add(v1, v2, v0));
    ASSERT_VEC_EQ(d, lanes, nmul_add(neg(v2), v1, v0));
    ASSERT_VEC_EQ(d, lanes, nmul_add(v1, neg(v2), v0));

for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = (i + 2) * (i + 2) + (i + 1);
    }
    ASSERT_VEC_EQ(d, lanes, mul_add(v2, v2, v1));
    ASSERT_VEC_EQ(d, lanes, nmul_add(neg(v2), v2, v1));

    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = -T(i + 2) * (i + 2) + (1 + i);
    }
    ASSERT_VEC_EQ(d, lanes, nmul_add(v2, v2, v1));

    ASSERT_VEC_EQ(d, v0, ext::mul_subtract(v0, v0, v0));
    ASSERT_VEC_EQ(d, v0, ext::nmul_subtract(v0, v0, v0));

    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = -T(i + 2);
    }
    ASSERT_VEC_EQ(d, lanes, ext::mul_subtract(v0, v1, v2));
    ASSERT_VEC_EQ(d, lanes, ext::mul_subtract(v1, v0, v2));
    ASSERT_VEC_EQ(d, lanes, ext::nmul_subtract(neg(v0), v1, v2));
    ASSERT_VEC_EQ(d, lanes, ext::nmul_subtract(v1, neg(v0), v2));

    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = (i + 1) * (i + 2);
    }
    ASSERT_VEC_EQ(d, lanes, ext::mul_subtract(v1, v2, v0));
    ASSERT_VEC_EQ(d, lanes, ext::mul_subtract(v2, v1, v0));
    ASSERT_VEC_EQ(d, lanes, ext::nmul_subtract(neg(v1), v2, v0));
    ASSERT_VEC_EQ(d, lanes, ext::nmul_subtract(v2, neg(v1), v0));

    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = (i + 2) * (i + 2) - (1 + i);
    }
    ASSERT_VEC_EQ(d, lanes, ext::mul_subtract(v2, v2, v1));
    ASSERT_VEC_EQ(d, lanes, ext::nmul_subtract(neg(v2), v2, v1));
  }
};

struct TestSquareRoot {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto vi = iota(d, 0);
    ASSERT_VEC_EQ(d, vi, sqrt(vi * vi));
  }
};

struct TestReciprocalSquareRoot {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v = set1(d, 123.0f);
    SIMD_ALIGN float lanes[d.N];
    store(approximate_reciprocal_sqrt(v), d, lanes);
    for (size_t i = 0; i < d.N; ++i) {
      float err = lanes[i] - 0.090166f;
      if (err < 0.0f) err = -err;
      ASSERT_EQ(true, err < 1E-4f);
    }
  }
};

struct TestRound {
  template <typename T, class D>
  void operator()(T, D d) const {
    // Integer positive
    {
      const auto v = iota(d, 4.0);
      ASSERT_VEC_EQ(d, v, ceil(v));
      ASSERT_VEC_EQ(d, v, floor(v));
      ASSERT_VEC_EQ(d, v, round(v));
      ASSERT_VEC_EQ(d, v, trunc(v));
    }

    // Integer negative
    {
      const auto v = iota(d, T(-32.0));
      ASSERT_VEC_EQ(d, v, ceil(v));
      ASSERT_VEC_EQ(d, v, floor(v));
      ASSERT_VEC_EQ(d, v, round(v));
      ASSERT_VEC_EQ(d, v, trunc(v));
    }

    // Huge positive
    {
      const auto v = set1(d, T(1E15));
      ASSERT_VEC_EQ(d, v, ceil(v));
      ASSERT_VEC_EQ(d, v, floor(v));
    }

    // Huge negative
    {
      const auto v = set1(d, T(-1E15));
      ASSERT_VEC_EQ(d, v, ceil(v));
      ASSERT_VEC_EQ(d, v, floor(v));
    }

    // Above positive
    {
      const auto v = iota(d, T(2.0001));
      const auto v3 = iota(d, T(3));
      const auto v2 = iota(d, T(2));
      ASSERT_VEC_EQ(d, v3, ceil(v));
      ASSERT_VEC_EQ(d, v2, floor(v));
      ASSERT_VEC_EQ(d, v2, round(v));
      ASSERT_VEC_EQ(d, v2, trunc(v));
    }

    // Below positive
    {
      const auto v = iota(d, T(3.9999));
      const auto v4 = iota(d, T(4));
      const auto v3 = iota(d, T(3));
      ASSERT_VEC_EQ(d, v4, ceil(v));
      ASSERT_VEC_EQ(d, v3, floor(v));
      ASSERT_VEC_EQ(d, v4, round(v));
      ASSERT_VEC_EQ(d, v3, trunc(v));
    }

    // Above negative
    {
      // WARNING: using iota => ensure negative value is low enough that
      // even 16 lanes remain negative, otherwise trunc will behave differently
      // for positive/negative values.
      const auto v = iota(d, T(-19.9999));
      const auto v3 = iota(d, T(-19));
      const auto v4 = iota(d, T(-20));
      ASSERT_VEC_EQ(d, v3, ceil(v));
      ASSERT_VEC_EQ(d, v4, floor(v));
      ASSERT_VEC_EQ(d, v4, round(v));
      ASSERT_VEC_EQ(d, v3, trunc(v));
    }

    // Below negative
    {
      const auto v = iota(d, T(-18.0001));
      const auto v2 = iota(d, T(-18));
      const auto v3 = iota(d, T(-19));
      ASSERT_VEC_EQ(d, v2, ceil(v));
      ASSERT_VEC_EQ(d, v3, floor(v));
      ASSERT_VEC_EQ(d, v2, round(v));
      ASSERT_VEC_EQ(d, v2, trunc(v));
    }
  }
};

struct TestIntFromFloat {
  template <typename T, class D>
  void operator()(T, D d) const {
    const Full<float> df;

    // Integer positive
    ASSERT_VEC_EQ(d, iota(d, 4), convert_to(d, iota(df, 4.0f)));
    ASSERT_VEC_EQ(d, iota(d, 4), nearest_int(iota(df, 4.0f)));

    // Integer negative
    ASSERT_VEC_EQ(d, iota(d, -32), convert_to(d, iota(df, -32.0f)));
    ASSERT_VEC_EQ(d, iota(d, -32), nearest_int(iota(df, -32.0f)));

    // Above positive
    ASSERT_VEC_EQ(d, iota(d, 2), convert_to(d, iota(df, 2.001f)));
    ASSERT_VEC_EQ(d, iota(d, 2), nearest_int(iota(df, 2.001f)));

    // Below positive
    ASSERT_VEC_EQ(d, iota(d, 3), convert_to(d, iota(df, 3.9999f)));
    ASSERT_VEC_EQ(d, iota(d, 4), nearest_int(iota(df, 3.9999f)));

    // Above negative
    ASSERT_VEC_EQ(d, iota(d, -23), convert_to(d, iota(df, -23.9999f)));
    ASSERT_VEC_EQ(d, iota(d, -24), nearest_int(iota(df, -23.9999f)));

    // Below negative
    ASSERT_VEC_EQ(d, iota(d, -24), convert_to(d, iota(df, -24.001f)));
    ASSERT_VEC_EQ(d, iota(d, -24), nearest_int(iota(df, -24.001f)));
  }
};

struct TestFloatFromInt {
  template <typename T, class D>
  void operator()(T, D d) const {
    const Full<int32_t> di;

    // Integer positive
    ASSERT_VEC_EQ(d, iota(d, 4.0f), convert_to(d, iota(di, 4)));

    // Integer negative
    ASSERT_VEC_EQ(d, iota(d, -32.0f), convert_to(d, iota(di, -32)));

    // Above positive
    ASSERT_VEC_EQ(d, iota(d, 2.0f), convert_to(d, iota(di, 2)));

    // Below positive
    ASSERT_VEC_EQ(d, iota(d, 4.0f), convert_to(d, iota(di, 4)));

    // Above negative
    ASSERT_VEC_EQ(d, iota(d, -4.0f), convert_to(d, iota(di, -4)));

    // Below negative
    ASSERT_VEC_EQ(d, iota(d, -2.0f), convert_to(d, iota(di, -2)));
  }
};

struct TestSumsOfU8 {
  template <typename T, class D>
  void operator()(T, D d) const {
#if SIMD_TARGET_VALUE != SIMD_NONE
    const Full<uint8_t> d8;
    SIMD_ALIGN uint8_t in_bytes[d8.N];
    uint64_t sums[d.N] = {0};
    for (size_t i = 0; i < d8.N; ++i) {
      const size_t group = i / 8;
      in_bytes[i] = 2 * i + 1;
      sums[group] += in_bytes[i];
    }
    const auto v = load(d8, in_bytes);
    ASSERT_VEC_EQ(d, sums, ext::sums_of_u8x8(v));
#endif
}
};

struct TestHorzSumT {
  template <typename T, class D>
  void operator()(T, D d) const {
    SIMD_ALIGN T in_lanes[d.N];
    double sum = 0.0;
    for (size_t i = 0; i < d.N; ++i) {
      in_lanes[i] = 1u << i;
      sum += in_lanes[i];
    }
    const auto v = load(d, in_lanes);
    const auto expected = set1(d, T(sum));
    ASSERT_VEC_EQ(d, expected, ext::sum_of_lanes(v));
  }
};

void TestHorzSum() {
  // No u16.
  Call<TestHorzSumT, uint32_t>();
  Call<TestHorzSumT, uint64_t>();

  // No i8/i16.
  Call<TestHorzSumT, int32_t>();
  Call<TestHorzSumT, int64_t>();

  Call<TestHorzSumT, float>();
  Call<TestHorzSumT, double>();
}

void TestArithmetic() {
  ForeachLaneType<TestPlusMinus>();
  TestSaturatingArithmetic();

  TestShifts();
  TestMinMax();
  TestAverage();
  TestAbs();
  TestMul();
  Call<TestMulHi16, int16_t>();
  TestMulEven();

  ForeachFloatLaneType<TestMulAdd>();
  ForeachFloatLaneType<TestSquareRoot>();
  Call<TestReciprocalSquareRoot, float>();
  ForeachFloatLaneType<TestRound>();
  Call<TestIntFromFloat, int32_t>();
  Call<TestFloatFromInt, float>();

  Call<TestSumsOfU8, uint64_t>();
  TestHorzSum();
}

}  // namespace arithmetic

namespace compare {

struct TestSignedCompare {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v2 = iota(d, 2);
    const auto v2b = iota(d, 2);
    const auto vn = iota(d, -T(d.N));
    const auto yes = set1(d, static_cast<T>(-1));
    const auto no = setzero(d);

    ASSERT_VEC_EQ(d, no, v2 == vn);
    ASSERT_VEC_EQ(d, yes, v2 == v2b);

    ASSERT_VEC_EQ(d, yes, v2 > vn);
    ASSERT_VEC_EQ(d, yes, vn < v2);
    ASSERT_VEC_EQ(d, no, v2 < vn);
    ASSERT_VEC_EQ(d, no, vn > v2);
  }
};

struct TestUnsignedCompare {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v2 = iota(d, 2);
    const auto v2b = iota(d, 2);
    const auto v3 = iota(d, 3);
    const auto yes = set1(d, T(~0ull));
    const auto no = setzero(d);

    ASSERT_VEC_EQ(d, no, v2 == v3);
    ASSERT_VEC_EQ(d, yes, v2 == v2b);
  }
};

struct TestFloatCompare {
  template <typename T, class D>
  void operator()(T, D d) const {
    constexpr size_t N8 = Full<uint8_t>::N;
    const auto v2 = iota(d, 2);
    const auto v2b = iota(d, 2);
    const auto vn = iota(d, -T(d.N));
    const auto no = setzero(d);

    ASSERT_VEC_EQ(d, no, v2 == vn);
    ASSERT_VEC_EQ(d, no, v2 < vn);
    ASSERT_VEC_EQ(d, no, vn > v2);

    // Equality is represented as 1-bits, which is a NaN, so compare bytes.
    uint8_t yes[N8];
    SetBytes(0xFF, &yes);

    SIMD_ALIGN T lanes[d.N];
    store(v2 == v2b, d, lanes);
    ASSERT_EQ(true, BytesEqual(lanes, yes, N8));
    store(v2 > vn, d, lanes);
    ASSERT_EQ(true, BytesEqual(lanes, yes, N8));
    store(vn < v2, d, lanes);
    ASSERT_EQ(true, BytesEqual(lanes, yes, N8));
  }
};

// Returns "bits" after zeroing any upper bits that wouldn't be returned by
// movemask for the given vector "D".
template <class D>
uint32_t ValidBits(D d, const uint32_t bits) {
  const uint64_t mask = (1ull << d.N) - 1;
  return bits & mask;
}

void TestMovemask() {
  const Full<uint8_t> d;
  SIMD_ALIGN const uint8_t bytes[32] = {
      0x80, 0xFF, 0x7F, 0x00,  0x01, 0x10, 0x20, 0x40,
      0x80, 0x02, 0x04, 0x08,  0xC0, 0xC1, 0xFE, 0x0F,
      0x0F, 0xFE, 0xC1, 0xC0,  0x08, 0x04, 0x02, 0x80,
      0x40, 0x20, 0x10, 0x01,  0x00, 0x7F, 0xFF, 0x80
  };
  ASSERT_EQ(ValidBits(d, 0xC08E7103), ext::movemask(load(d, bytes)));

  SIMD_ALIGN const float lanes[8] = {-1.0f,  1E30f, -0.0f, 1E-30f,
                                     1E-30f, -0.0f, 1E30f, -1.0f};
  const Full<float> df;
  ASSERT_EQ(ValidBits(df, 0xa5), ext::movemask(load(df, lanes)));

  const Full<double> dd;
  SIMD_ALIGN const double lanes2[4] = {1E300, -1E-300, -0.0, 1E-10};
  ASSERT_EQ(ValidBits(dd, 6), ext::movemask(load(dd, lanes2)));
}

struct TestAllZero {
  template <typename T, class D>
  void operator()(T, D d) const {
    const T max = LimitsMax<T>();
    const T min_nonzero = LimitsMin<T>() + 1;

    // all lanes zero
    auto v = setzero(d);
    SIMD_ALIGN T lanes[d.N];
    store(v, d, lanes);

    // Set each lane to nonzero and ensure !all_zero
    for (size_t i = 0; i < d.N; ++i) {
      lanes[i] = max;
      v = load(d, lanes);
      ASSERT_EQ(false, ext::all_zero(v));

      lanes[i] = min_nonzero;
      v = load(d, lanes);
      ASSERT_EQ(false, ext::all_zero(v));

      // Reset to all zero
      lanes[i] = T(0);
      v = load(d, lanes);
      ASSERT_EQ(true, ext::all_zero(v));
    }
  }
};

void TestCompare() {
  ForeachSignedLaneType<TestSignedCompare>();
  ForeachUnsignedLaneType<TestUnsignedCompare>();
  ForeachFloatLaneType<TestFloatCompare>();

  TestMovemask();

  ForeachUnsignedLaneType<TestAllZero>();
  ForeachSignedLaneType<TestAllZero>();
  // No float.
}

}  // namespace compare

namespace logical {

struct TestLogicalT {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v0 = setzero(d);
    const auto vi = iota(d, 0);

    ASSERT_VEC_EQ(d, v0, v0 & vi);
    ASSERT_VEC_EQ(d, v0, vi & v0);
    ASSERT_VEC_EQ(d, vi, vi & vi);

    ASSERT_VEC_EQ(d, vi, v0 | vi);
    ASSERT_VEC_EQ(d, vi, vi | v0);
    ASSERT_VEC_EQ(d, vi, vi | vi);

    ASSERT_VEC_EQ(d, vi, v0 ^ vi);
    ASSERT_VEC_EQ(d, vi, vi ^ v0);
    ASSERT_VEC_EQ(d, v0, vi ^ vi);

    ASSERT_VEC_EQ(d, vi, andnot(v0, vi));
    ASSERT_VEC_EQ(d, v0, andnot(vi, v0));
    ASSERT_VEC_EQ(d, v0, andnot(vi, vi));

    auto v = vi;
    v &= vi;
    ASSERT_VEC_EQ(d, vi, v);
    v &= v0;
    ASSERT_VEC_EQ(d, v0, v);

    v |= vi;
    ASSERT_VEC_EQ(d, vi, v);
    v |= v0;
    ASSERT_VEC_EQ(d, vi, v);

    v ^= vi;
    ASSERT_VEC_EQ(d, v0, v);
    v ^= v0;
    ASSERT_VEC_EQ(d, v0, v);
  }
};

struct TestSelect {
  template <typename T, class D>
  void operator()(T, D d) const {
    RandomState rng = {1234};
    const T mask0(0);
    const uint64_t ones = ~0ull;
    T mask1;
    CopyBytes<sizeof(T)>(&ones, &mask1);

    SIMD_ALIGN T lanes1[d.N];
    SIMD_ALIGN T lanes2[d.N];
    SIMD_ALIGN T masks[d.N];
    for (size_t i = 0; i < d.N; ++i) {
      lanes1[i] = int32_t(Random32(&rng));
      lanes2[i] = int32_t(Random32(&rng));
      masks[i] = (Random32(&rng) & 1024) ? mask0 : mask1;
    }

    SIMD_ALIGN T out_lanes[d.N];
    store(select(load(d, lanes1), load(d, lanes2), load(d, masks)), d,
          out_lanes);
    for (size_t i = 0; i < d.N; ++i) {
      ASSERT_EQ((masks[i] == mask0) ? lanes1[i] : lanes2[i], out_lanes[i]);
    }
  }
};

struct TestSelectSign {
  template <typename T, class D>
  void operator()(T, D d) const {
    RandomState rng = {1234};

    SIMD_ALIGN T lanes1[d.N];
    SIMD_ALIGN T lanes2[d.N];
    SIMD_ALIGN T masks[d.N];
    for (size_t i = 0; i < d.N; ++i) {
      lanes1[i] = Random32(&rng);
      lanes2[i] = Random32(&rng);
      masks[i] = (Random32(&rng) & 1024) ? lanes1[i] : -lanes2[i];
    }

    SIMD_ALIGN T out_lanes[d.N];
    const auto selector = condition_from_sign(load(d, masks));
    store(select(load(d, lanes1), load(d, lanes2), selector), d, out_lanes);
    for (size_t i = 0; i < d.N; ++i) {
      ASSERT_EQ((masks[i] < T(0.0)) ? lanes2[i] : lanes1[i], out_lanes[i]);
    }
  }
};

void TestLogical() {
  ForeachLaneType<TestLogicalT>();
  ForeachLaneType<TestSelect>();
  ForeachFloatLaneType<TestSelectSign>();
}

}  // namespace logical

namespace memory {

struct TestLoadStore {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto hi = iota(d, 1 + d.N);
    const auto lo = iota(d, 1);
    SIMD_ALIGN T lanes[2 * d.N];
    store(hi, d, lanes + d.N);
    store(lo, d, lanes);

    // Aligned load
    const auto lo2 = load(d, lanes);
    ASSERT_VEC_EQ(d, lo2, lo);

    // Aligned store
    SIMD_ALIGN T lanes2[2 * d.N];
    store(lo2, d, lanes2);
    store(hi, d, lanes2 + d.N);
    for (size_t i = 0; i < 2 * d.N; ++i) {
      ASSERT_EQ(lanes[i], lanes2[i]);
    }

    // Unaligned load
    const auto vu = load_unaligned(d, lanes + 1);
    SIMD_ALIGN T lanes3[d.N];
    store(vu, d, lanes3);
    for (size_t i = 0; i < d.N; ++i) {
      ASSERT_EQ(T(i + 2), lanes3[i]);
    }

    // Unaligned store
    store_unaligned(lo2, d, lanes2 + d.N / 2);
    size_t i = 0;
    for (; i < d.N / 2; ++i) {
      ASSERT_EQ(lanes[i], lanes2[i]);
    }
    for (; i < 3 * d.N / 2; ++i) {
      ASSERT_EQ(T(i - d.N / 2 + 1), lanes2[i]);
    }
    // Subsequent values remain unchanged.
    for (; i < 2 * d.N; ++i) {
      ASSERT_EQ(T(i + 1), lanes2[i]);
    }
  }
};

struct TestLoadDup128 {
  template <typename T, class D>
  void operator()(T, D d) const {
#if SIMD_TARGET_VALUE != SIMD_NONE
    constexpr size_t N128 = 16 / sizeof(T);
    alignas(16) T lanes[N128];
    for (size_t i = 0; i < N128; ++i) {
      lanes[i] = 1 + i;
    }
    const auto v = load_dup128(d, lanes);
    SIMD_ALIGN T out[d.N];
    store(v, d, out);
    for (size_t i = 0; i < d.N; ++i) {
      ASSERT_EQ(T(i % N128 + 1), out[i]);
    }
#endif
  }
};

struct TestStreamT {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v = iota(d, 0);
    SIMD_ALIGN T out[d.N];
    stream(v, d, out);
    store_fence();
    for (size_t i = 0; i < d.N; ++i) {
      ASSERT_EQ(T(i), out[i]);
    }
  }
};

#if SIMD_TARGET_VALUE == SIMD_AVX2

template <typename Offset, int kShift>
struct TestGatherT {
  template <typename T, class D>
  void operator()(T, D d) const {
    static_assert(sizeof(T) == (1 << kShift), "Incorrect kShift");

    // Base points to middle; |max_offset| + sizeof(T) <= kNumBytes / 2.
    constexpr size_t kNumBytes = 64;
    uint8_t bytes[kNumBytes];
    for (size_t i = 0; i < kNumBytes; ++i) {
      bytes[i] = i + 1;
    }
    const uint8_t* middle = bytes + kNumBytes / 2;

    // Offsets: combinations of aligned, repeated, negative.
    SIMD_ALIGN Offset offset_lanes[SIMD_MAX(d.N, 8)] = {2,   12,  4,   4,
                                                        -16, -16, -21, -20};

    SIMD_ALIGN T expected[d.N];
    for (size_t i = 0; i < d.N; ++i) {
      CopyBytes<sizeof(T)>(middle + offset_lanes[i], &expected[i]);
    }

    const auto offsets = load(Full<Offset>(), offset_lanes);
    auto actual =
        ext::gather_offset(d, reinterpret_cast<const T*>(middle), offsets);
    ASSERT_VEC_EQ(d, expected, actual);

    // Indices
    SIMD_ALIGN const Offset index_lanes[SIMD_MAX(d.N, 8)] = {1, -2, 0,  1,
                                                             3, -2, -1, 2};
    for (size_t i = 0; i < d.N; ++i) {
      CopyBytes<sizeof(T)>(middle + index_lanes[i] * sizeof(T), &expected[i]);
    }
    const auto indices = load(Full<Offset>(), index_lanes);
    actual = ext::gather_index(d, reinterpret_cast<const T*>(middle), indices);
    ASSERT_VEC_EQ(d, expected, actual);
  }
};

template <typename Offset, int kShift>
struct TestFloatGatherT {
  template <typename T, class D>
  void operator()(T, D d) const {
    static_assert(sizeof(T) == (1 << kShift), "Incorrect kShift");

    constexpr size_t kNumValues = 16;
    // Base points to middle; |max_index| < kNumValues / 2.
    SIMD_ALIGN const T values[SIMD_MAX(d.N, kNumValues)] = {
        T(100.0), T(110.0), T(111.0), T(128.0), T(1024.0), T(-1.0),
        T(-2.0),  T(-3.0),  T(0.25),  T(0.5),   T(0.75),   T(1.25),
        T(1.5),   T(1.75),  T(-0.25), T(-0.5)};
    const T* middle = values + kNumValues / 2;

    // Indices: combinations of aligned, repeated, negative.
    SIMD_ALIGN const Offset index_lanes[SIMD_MAX(d.N, 8)] = {1, -6, 0,  1,
                                                             3, -6, -1, 7};
    SIMD_ALIGN T expected[d.N];
    for (size_t i = 0; i < d.N; ++i) {
      CopyBytes<sizeof(T)>(middle + index_lanes[i], &expected[i]);
    }
    const auto indices = load(Full<Offset>(), index_lanes);
    auto actual = ext::gather_index(d, middle, indices);
    ASSERT_VEC_EQ(d, expected, actual);

    // Offsets: same as index * sizeof(T).
    const auto offsets = shift_left<kShift>(indices);
    actual = ext::gather_offset(d, middle, offsets);
    ASSERT_VEC_EQ(d, expected, actual);
  }
};

#endif  // SIMD_TARGET_VALUE == SIMD_AVX2

void TestStream() {
  // No u8,u16.
  Call<TestStreamT, uint32_t>();
  Call<TestStreamT, uint64_t>();
  // No i8,i16.
  Call<TestStreamT, int32_t>();
  Call<TestStreamT, int64_t>();
  Call<TestStreamT, float>();
  Call<TestStreamT, double>();
}

void TestGather() {
#if SIMD_TARGET_VALUE == SIMD_AVX2
  // No u8,u16.
  Call<TestGatherT<int32_t, 2>, uint32_t>();
  Call<TestGatherT<int64_t, 3>, uint64_t>();
  // No i8,i16.
  Call<TestGatherT<int32_t, 2>, int32_t>();
  Call<TestGatherT<int64_t, 3>, int64_t>();

  Call<TestFloatGatherT<int32_t, 2>, float>();
  Call<TestFloatGatherT<int64_t, 3>, double>();
#endif
}

void TestMemory() {
  ForeachLaneType<TestLoadStore>();
  ForeachLaneType<TestLoadDup128>();
  TestStream();
  TestGather();
}

}  // namespace memory

namespace convert {

struct TestCastT {
  template <typename T, class D>
  void operator()(T, D d) const {
    Test<uint8_t, T>();
    Test<uint16_t, T>();
    Test<uint32_t, T>();
    Test<uint64_t, T>();
    Test<int8_t, T>();
    Test<int16_t, T>();
    Test<int32_t, T>();
    Test<int64_t, T>();
    Test<float, T>();
    Test<double, T>();
  }

  template <typename FromT, typename ToT>
  void Test() const {
    const Full<FromT> df;
    const Full<ToT> dt;
    const auto vf = iota(df, 1);
    const auto vt = cast_to(dt, vf);
    static_assert(sizeof(vf) == sizeof(vt), "Cast must return same size");
    // Must return the same bits
    SIMD_ALIGN FromT from_lanes[df.N];
    SIMD_ALIGN ToT to_lanes[dt.N];
    store(vf, df, from_lanes);
    store(vt, dt, to_lanes);
    ASSERT_EQ(true, BytesEqual(from_lanes, to_lanes, sizeof(vf)));
  }
};

void TestCast() {
#if SIMD_TARGET_VALUE == SIMD_NONE
  // Promotion is undefined => only test same-sized types.
  TestCastT().Test<uint8_t, uint8_t>();
  TestCastT().Test<int8_t, int8_t>();
  TestCastT().Test<uint8_t, int8_t>();
  TestCastT().Test<int8_t, uint8_t>();

  TestCastT().Test<uint16_t, uint16_t>();
  TestCastT().Test<int16_t, int16_t>();
  TestCastT().Test<uint16_t, int16_t>();
  TestCastT().Test<int16_t, uint16_t>();

  TestCastT().Test<uint32_t, uint32_t>();
  TestCastT().Test<int32_t, int32_t>();
  TestCastT().Test<uint32_t, int32_t>();
  TestCastT().Test<int32_t, uint32_t>();
  TestCastT().Test<uint32_t, float>();
  TestCastT().Test<int32_t, float>();
  TestCastT().Test<float, float>();
  TestCastT().Test<float, uint32_t>();
  TestCastT().Test<float, int32_t>();

  TestCastT().Test<uint64_t, uint64_t>();
  TestCastT().Test<int64_t, int64_t>();
  TestCastT().Test<uint64_t, int64_t>();
  TestCastT().Test<int64_t, uint64_t>();
  TestCastT().Test<uint64_t, double>();
  TestCastT().Test<int64_t, double>();
  TestCastT().Test<double, double>();
  TestCastT().Test<double, uint64_t>();
  TestCastT().Test<double, int64_t>();
#else
  ForeachUnsignedLaneType<TestCastT>();
  ForeachSignedLaneType<TestCastT>();
#endif

  // Float <-> u/i32
  TestCastT().Test<uint32_t, float>();
  TestCastT().Test<int32_t, float>();
  TestCastT().Test<float, uint32_t>();
  TestCastT().Test<float, int32_t>();
}

template <typename FromT, typename ToT>
void TestPromoteT() {
  constexpr size_t N = Full<ToT>::N;
  const Part<FromT, N> from_d;
  const Full<ToT> to_d;

  const auto from = iota(from_d, 1);
  const auto from_n1 = set1(from_d, FromT(-1));
  const auto from_min = set1(from_d, LimitsMin<FromT>());
  const auto from_max = set1(from_d, LimitsMax<FromT>());
  const auto to = iota(to_d, 1);
  const auto to_n1 = set1(to_d, ToT(FromT(-1)));
  const auto to_min = set1(to_d, ToT(LimitsMin<FromT>()));
  const auto to_max = set1(to_d, ToT(LimitsMax<FromT>()));
  ASSERT_VEC_EQ(to_d, to, convert_to(to_d, from));
  ASSERT_VEC_EQ(to_d, to_n1, convert_to(to_d, from_n1));
  ASSERT_VEC_EQ(to_d, to_min, convert_to(to_d, from_min));
  ASSERT_VEC_EQ(to_d, to_max, convert_to(to_d, from_max));
}

template <typename FromT, typename ToT>
void TestDemoteT() {
  constexpr size_t N = Full<FromT>::N;
  const Full<FromT> from_d;
  const Part<ToT, N> to_d;

  const auto from = iota(from_d, 1);
  const auto from_n1 = set1(from_d, FromT(ToT(-1)));
  const auto from_min = set1(from_d, FromT(LimitsMin<ToT>()));
  const auto from_max = set1(from_d, FromT(LimitsMax<ToT>()));
  const auto to = iota(to_d, 1);
  const auto to_n1 = set1(to_d, ToT(-1));
  const auto to_min = set1(to_d, LimitsMin<ToT>());
  const auto to_max = set1(to_d, LimitsMax<ToT>());
  ASSERT_VEC_EQ(to_d, to, convert_to(to_d, from));
  ASSERT_VEC_EQ(to_d, to_n1, convert_to(to_d, from_n1));
  ASSERT_VEC_EQ(to_d, to_min, convert_to(to_d, from_min));
  ASSERT_VEC_EQ(to_d, to_max, convert_to(to_d, from_max));
}

template <typename FromT, typename ToT>
void TestDupPromoteT() {
  constexpr size_t N = Full<ToT>::N;
  const Part<FromT, N> from_d;
  const Full<ToT> to_d;

  const auto from = iota(from_d, 1);
  const auto from_n1 = set1(from_d, FromT(-1));
  const auto from_min = set1(from_d, LimitsMin<FromT>());
  const auto from_max = set1(from_d, LimitsMax<FromT>());
  const auto to = iota(to_d, 1);
  const auto to_n1 = set1(to_d, ToT(FromT(-1)));
  const auto to_min = set1(to_d, ToT(LimitsMin<FromT>()));
  const auto to_max = set1(to_d, ToT(LimitsMax<FromT>()));
  ASSERT_VEC_EQ(to_d, to, convert_to(to_d, from));
  ASSERT_VEC_EQ(to_d, to_n1, convert_to(to_d, from_n1));
  ASSERT_VEC_EQ(to_d, to_min, convert_to(to_d, from_min));
  ASSERT_VEC_EQ(to_d, to_max, convert_to(to_d, from_max));
}

void TestConvert() {
  TestCast();

  const Full<uint8_t> d8;
  const Full<uint32_t> d32;
  SIMD_ALIGN uint8_t lanes8[d8.N];
  store(iota(d8, 0), d8, lanes8);
  ASSERT_VEC_EQ(d32, iota(d32, 0), u32_from_u8(load_dup128(d8, lanes8)));
  store(iota(d8, 0x7F), d8, lanes8);
  ASSERT_VEC_EQ(d32, iota(d32, 0x7F), u32_from_u8(load_dup128(d8, lanes8)));
  const Part<uint8_t, d32.N> p8;
  ASSERT_VEC_EQ(p8, iota(p8, 0), u8_from_u32(iota(d32, 0)));
  ASSERT_VEC_EQ(p8, iota(p8, 0x7F), u8_from_u32(iota(d32, 0x7F)));

  // Promote: no u64,i64
  TestPromoteT<uint8_t, int16_t>();
  TestPromoteT<uint8_t, int32_t>();
  TestPromoteT<uint16_t, int32_t>();
  TestPromoteT<int8_t, int16_t>();
  TestPromoteT<int8_t, int32_t>();
  TestPromoteT<int16_t, int32_t>();
  TestPromoteT<uint32_t, uint64_t>();
  TestPromoteT<int32_t, int64_t>();

  // Demote
  TestDemoteT<int16_t, int8_t>();
  TestDemoteT<int32_t, int8_t>();
  TestDemoteT<int32_t, int16_t>();
  TestDemoteT<int16_t, uint8_t>();
  TestDemoteT<int32_t, uint8_t>();
  TestDemoteT<int32_t, uint16_t>();

  TestDupPromoteT<uint8_t, uint32_t>();
}

}  // namespace convert

namespace swizzle {

struct TestShiftBytesT {
  template <typename T, class D>
  void operator()(T, D d) const {
#if SIMD_TARGET_VALUE != SIMD_NONE
    const Full<uint8_t> d8;

    // Zero remains zero
    const auto v0 = setzero(d);
    ASSERT_VEC_EQ(d, v0, shift_left_bytes<1>(v0));
    ASSERT_VEC_EQ(d, v0, shift_right_bytes<1>(v0));

    // Zero after shifting out the high/low byte
    SIMD_ALIGN uint8_t bytes[d8.N] = {0};
    bytes[d8.N - 1] = 0x7F;
    const auto vhi = cast_to(d, load(d8, bytes));
    bytes[d8.N - 1] = 0;
    bytes[0] = 0x7F;
    const auto vlo = cast_to(d, load(d8, bytes));
    ASSERT_EQ(true, ext::all_zero(shift_left_bytes<1>(vhi)));
    ASSERT_EQ(true, ext::all_zero(shift_right_bytes<1>(vlo)));

    SIMD_ALIGN T in[d.N];
    const uint8_t* in_bytes = reinterpret_cast<const uint8_t*>(in);
    const auto v = cast_to(d, iota(d8, 1));
    store(v, d, in);

    // Shifting by one lane is the same as shifting #bytes
    ASSERT_VEC_EQ(d, shift_left_lanes<1>(v), shift_left_bytes<sizeof(T)>(v));
    ASSERT_VEC_EQ(d, shift_right_lanes<1>(v), shift_right_bytes<sizeof(T)>(v));
    // Two lanes
    ASSERT_VEC_EQ(d, shift_left_lanes<2>(v),
                  shift_left_bytes<2 * sizeof(T)>(v));
    ASSERT_VEC_EQ(d, shift_right_lanes<2>(v),
                  shift_right_bytes<2 * sizeof(T)>(v));

    SIMD_ALIGN T shifted[d.N];
    const uint8_t* shifted_bytes = reinterpret_cast<const uint8_t*>(shifted);

    const size_t kBlockSize = SIMD_MIN(d8.N, 16);
    store(shift_left_bytes<1>(v), d, shifted);
    for (size_t block = 0; block < d8.N; block += kBlockSize) {
      ASSERT_EQ(uint8_t(0), shifted_bytes[block]);
      ASSERT_EQ(true, BytesEqual(in_bytes + block, shifted_bytes + block + 1,
                                 kBlockSize - 1));
    }

    store(shift_right_bytes<1>(v), d, shifted);
    for (size_t block = 0; block < d8.N; block += kBlockSize) {
      ASSERT_EQ(uint8_t(0), shifted_bytes[block + kBlockSize - 1]);
      ASSERT_EQ(true, BytesEqual(in_bytes + block + 1, shifted_bytes + block,
                                 kBlockSize - 1));
    }
#endif
  }
};

void TestShiftBytes() {
  ForeachUnsignedLaneType<TestShiftBytesT>();
  ForeachSignedLaneType<TestShiftBytesT>();
  // No float.
}

template <typename T, int kLane>
struct TestBroadcastR {
  void operator()() const {
    const Full<T> d;
    SIMD_ALIGN T in_lanes[d.N] = {0};
    constexpr size_t kVecN = Full<T>::N;
    constexpr size_t kBlockN = SIMD_MIN(kVecN * sizeof(T), 16) / sizeof(T);
    // Need to set within each 128-bit block
    for (size_t block = 0; block < d.N; block += kBlockN) {
      in_lanes[block + kLane] = block + 1;
    }
    const auto in = load(d, in_lanes);
    SIMD_ALIGN T out_lanes[d.N];
    store(broadcast<kLane>(in), d, out_lanes);
    for (size_t block = 0; block < d.N; block += kBlockN) {
      for (size_t i = 0; i < kBlockN; ++i) {
        ASSERT_EQ(T(block + 1), out_lanes[block + i]);
      }
    }

    TestBroadcastR<T, kLane - 1>()();
  }
};

template <typename T>
struct TestBroadcastR<T, -1> {
  void operator()() const {}
};

template <typename T>
void TestBroadcastT() {
  constexpr size_t kVecN = Full<T>::N;
  TestBroadcastR<T, SIMD_MIN(kVecN, 16 / sizeof(T)) - 1>()();
}

void TestBroadcast() {
  // No u8.
  TestBroadcastT<uint16_t>();
  TestBroadcastT<uint32_t>();
  TestBroadcastT<uint64_t>();
  // No i8.
  TestBroadcastT<int16_t>();
  TestBroadcastT<int64_t>();
  TestBroadcastT<float>();
  TestBroadcastT<double>();
}

#if SIMD_TARGET_VALUE != SIMD_NONE

struct TestPermuteT {
  template <typename T, class D>
  void operator()(T, D d) const {
#if SIMD_TARGET_VALUE == SIMD_AVX2
    // Test one specific permutation with repeated and cross-block indices.
    SIMD_ALIGN int32_t idx[d.N] = {1, 7, 2, 2, 4, 1, 3, 6};
    const auto v = iota(d, 1);
    SIMD_ALIGN T expected_lanes[d.N];
    for (size_t i = 0; i < d.N; ++i) {
      expected_lanes[i] = idx[i] + 1;  // == v[idx[i]]
    }

    const auto opaque = set_table_indices(d, idx);
    const auto actual = table_lookup_lanes(v, opaque);
    ASSERT_VEC_EQ(d, expected_lanes, actual);
#else
    // Non-AVX2: test all possible permutations.
    SIMD_ALIGN int32_t idx[d.N];
    const auto v = iota(d, 1);
    SIMD_ALIGN T expected_lanes[d.N];

    for (int i0 = 0; i0 < d.N; ++i0) {
      idx[0] = i0;
      for (int i1 = 0; i1 < d.N; ++i1) {
        idx[1] = i1;
        for (int i2 = 0; i2 < d.N; ++i2) {
          idx[2] = i2;
          for (int i3 = 0; i3 < d.N; ++i3) {
            idx[3] = i3;

            for (size_t i = 0; i < d.N; ++i) {
              expected_lanes[i] = idx[i] + 1;  // == v[idx[i]]
            }

            const auto opaque = set_table_indices(d, idx);
            const auto actual = table_lookup_lanes(v, opaque);
            ASSERT_VEC_EQ(d, expected_lanes, actual);
          }
        }
      }
    }
#endif
  }
};

#endif

void TestPermute() {
#if SIMD_TARGET_VALUE != SIMD_NONE
  // Only uif32.
  Call<TestPermuteT, uint32_t>();
  Call<TestPermuteT, int32_t>();
  Call<TestPermuteT, float>();
#endif
}

struct TestInterleave {
  template <typename T, class D>
  void operator()(T, D d) const {
// Not supported by scalar.h: zip(f32, f32) would need to return f32x2.
#if SIMD_TARGET_VALUE != SIMD_NONE
    SIMD_ALIGN T even_lanes[d.N];
    SIMD_ALIGN T odd_lanes[d.N];
    for (size_t i = 0; i < d.N; ++i) {
      even_lanes[i] = 2 * i + 0;
      odd_lanes[i] = 2 * i + 1;
    }
    const auto even = load(d, even_lanes);
    const auto odd = load(d, odd_lanes);

    SIMD_ALIGN T lo_lanes[d.N];
    SIMD_ALIGN T hi_lanes[d.N];
    store(interleave_lo(even, odd), d, lo_lanes);
    store(interleave_hi(even, odd), d, hi_lanes);

    constexpr size_t kBlockN = 16 / sizeof(T);
    for (size_t i = 0; i < d.N; ++i) {
      const size_t block = i / kBlockN;
      const size_t lo = (i % kBlockN) + block * 2 * kBlockN;
      ASSERT_EQ(T(lo), lo_lanes[i]);
      ASSERT_EQ(T(lo + kBlockN), hi_lanes[i]);
    }
#endif
  }
};

template <typename T, typename WideT>
struct TestZipT {
  void operator()() const {
    const Full<T> d;
    const Full<WideT> dw;
    SIMD_ALIGN T even_lanes[d.N];
    SIMD_ALIGN T odd_lanes[d.N];
    for (size_t i = 0; i < d.N; ++i) {
      even_lanes[i] = 2 * i + 0;
      odd_lanes[i] = 2 * i + 1;
    }
    const auto even = load(d, even_lanes);
    const auto odd = load(d, odd_lanes);

    SIMD_ALIGN WideT lo_lanes[dw.N];
    SIMD_ALIGN WideT hi_lanes[dw.N];
    store(zip_lo(even, odd), dw, lo_lanes);
    store(zip_hi(even, odd), dw, hi_lanes);

    constexpr size_t kBlockN = 16 / sizeof(WideT);
    for (size_t i = 0; i < dw.N; ++i) {
      const size_t block = i / kBlockN;
      const size_t lo = (i % kBlockN) + block * 2 * kBlockN;
      const size_t bits = sizeof(T) * 8;
      const size_t expected_lo = ((lo + 1) << bits) + lo;
      const size_t expected_hi = ((lo + kBlockN + 1) << bits) + lo + kBlockN;
      ASSERT_EQ(T(expected_lo), lo_lanes[i]);
      ASSERT_EQ(T(expected_hi), hi_lanes[i]);
    }
  }
};

void TestZip() {
  TestZipT<uint8_t, uint16_t>();
  TestZipT<uint16_t, uint32_t>();
  TestZipT<uint32_t, uint64_t>();
  // No 64-bit nor float.
  TestZipT<int8_t, int16_t>();
  TestZipT<int16_t, int32_t>();
  TestZipT<int32_t, int64_t>();
}

struct TestShuffleT {
  template <typename T, class D>
  void operator()(T, D d) const {
// Not supported by scalar.h (its vector size is always less than 16 bytes)
#if SIMD_TARGET_VALUE != SIMD_NONE
    RandomState rng = {1234};
    const Full<uint8_t> d8;
    constexpr size_t N8 = Full<uint8_t>::N;
    SIMD_ALIGN uint8_t in_bytes[N8];
    for (size_t i = 0; i < N8; ++i) {
      in_bytes[i] = Random32(&rng) & 0xFF;
    }
    const auto in = load(d8, in_bytes);
    SIMD_ALIGN const uint8_t index_bytes[32] = {
        // Same index as source, multiple outputs from same input,
        // unused input (9), ascending/descending and nonconsecutive neighbors.
        0,  2,  1, 2, 15, 12, 13, 14, 6,  7,  8,  5,  4, 3, 10, 11,
        11, 10, 3, 4, 5,  8,  7,  6,  14, 13, 12, 15, 2, 1, 2,  0};
    const auto indices = load(d8, index_bytes);
    SIMD_ALIGN T out_lanes[d.N];
    store(table_lookup_bytes(cast_to(d, in), indices), d, out_lanes);
    const uint8_t* out_bytes = reinterpret_cast<const uint8_t*>(out_lanes);

    for (size_t block = 0; block < N8; block += 16) {
      for (size_t i = 0; i < 16; ++i) {
        const uint8_t expected = in_bytes[block + index_bytes[block + i]];
        ASSERT_EQ(expected, out_bytes[block + i]);
      }
    }
#endif
  }
};

void TestShuffle() {
  ForeachUnsignedLaneType<TestShuffleT>();
  ForeachSignedLaneType<TestShuffleT>();
  // No float.
}

template <typename T, class D, int kBytes>
struct TestExtractR {
  void operator()() const {
#if SIMD_TARGET_VALUE != SIMD_NONE
    const D d;
    const Full<uint8_t> d8;
    const auto lo = cast_to(d, iota(d8, 1));
    const auto hi = cast_to(d, iota(d8, 1 + d8.N));

    SIMD_ALIGN T lanes[D::N];
    store(combine_shift_right_bytes<kBytes>(hi, lo), d, lanes);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(lanes);

    const size_t kBlockSize = 16;
    for (size_t i = 0; i < d8.N; ++i) {
      const size_t block = i / kBlockSize;
      const size_t lane = i % kBlockSize;
      const size_t first_lo = block * kBlockSize;
      const size_t idx = lane + kBytes;
      const size_t offset = (idx < kBlockSize) ? 0 : d8.N - kBlockSize;
      const bool at_end = idx >= 2 * kBlockSize;
      const uint8_t expected = at_end ? 0 : (first_lo + idx + 1 + offset);
      ASSERT_EQ(expected, bytes[i]);
    }

    TestExtractR<T, D, kBytes - 1>()();
#endif
  }
};

template <typename T, class D>
struct TestExtractR<T, D, 0> {
  void operator()() const {}
};

struct TestExtractT {
  template <typename T, class D>
  void operator()(T, D d) const {
    TestExtractR<T, D, 15>()();
  }
};

void TestExtract() {
  ForeachUnsignedLaneType<TestExtractT>();
  ForeachSignedLaneType<TestExtractT>();
  // No float.
}

#if SIMD_TARGET_VALUE != SIMD_NONE

template <class D, class V>
void VerifyLanes32(D d, V v, const int i3, const int i2, const int i1,
                   const int i0) {
  using T = typename D::T;
  SIMD_ALIGN T lanes[d.N];
  store(v, d, lanes);
  constexpr size_t kBlockN = 16 / sizeof(T);
  for (size_t block = 0; block < d.N; block += kBlockN) {
    ASSERT_EQ(T(block + i3), lanes[block + 3]);
    ASSERT_EQ(T(block + i2), lanes[block + 2]);
    ASSERT_EQ(T(block + i1), lanes[block + 1]);
    ASSERT_EQ(T(block + i0), lanes[block + 0]);
  }
}

template <class D, class V>
void VerifyLanes64(D d, V v, const int i1, const int i0) {
  using T = typename D::T;
  SIMD_ALIGN T lanes[d.N];
  store(v, d, lanes);
  constexpr size_t kBlockN = 16 / sizeof(T);
  for (size_t block = 0; block < d.N; block += kBlockN) {
    ASSERT_EQ(T(block + i1), lanes[block + 1]);
    ASSERT_EQ(T(block + i0), lanes[block + 0]);
  }
}

struct TestSpecialShuffle32 {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v = iota(d, 0);
    VerifyLanes32(d, shuffle_1032(v), 1, 0, 3, 2);
    VerifyLanes32(d, shuffle_0321(v), 0, 3, 2, 1);
    VerifyLanes32(d, shuffle_2103(v), 2, 1, 0, 3);
    VerifyLanes32(d, shuffle_0123(v), 0, 1, 2, 3);
  }
};

struct TestSpecialShuffle64 {
  template <typename T, class D>
  void operator()(T, D d) const {
    const auto v = iota(d, 0);
    VerifyLanes64(d, shuffle_01(v), 0, 1);
  }
};

#endif

void TestSpecialShuffles() {
#if SIMD_TARGET_VALUE != SIMD_NONE
  Call<TestSpecialShuffle32, int32_t>();
  Call<TestSpecialShuffle64, int64_t>();
  Call<TestSpecialShuffle32, float>();
  Call<TestSpecialShuffle64, double>();
#endif
}

struct TestConcatHalves {
  template <typename T, class D>
  void operator()(T, D d) const {
#if SIMD_TARGET_VALUE != SIMD_NONE
    // Construct inputs such that interleaved halves == iota.
    const auto expected = iota(d, 1);

    SIMD_ALIGN T lo[d.N];
    SIMD_ALIGN T hi[d.N];
    size_t i;
    for (i = 0; i < d.N / 2; ++i) {
      lo[i] = 1 + i;
      hi[i] = lo[i] + d.N / 2;
    }
    for (; i < d.N; ++i) {
      lo[i] = hi[i] = 0;
    }
    ASSERT_VEC_EQ(d, expected, concat_lo_lo(load(d, hi), load(d, lo)));

    // Same for high blocks.
    for (i = 0; i < d.N / 2; ++i) {
      lo[i] = hi[i] = 0;
    }
    for (; i < d.N; ++i) {
      lo[i] = 1 + i - d.N / 2;
      hi[i] = lo[i] + d.N / 2;
    }
    ASSERT_VEC_EQ(d, expected, concat_hi_hi(load(d, hi), load(d, lo)));
#endif
  }
};

struct TestConcatLoHi {
  template <typename T, class D>
  void operator()(T, D d) const {
#if SIMD_TARGET_VALUE != SIMD_NONE
    // Middle part of iota(1) == iota(1 + d.N / 2).
    const auto lo = iota(d, 1);
    const auto hi = iota(d, 1 + d.N);
    ASSERT_VEC_EQ(d, iota(d, 1 + d.N / 2), concat_lo_hi(hi, lo));
#endif
  }
};

struct TestConcatHiLo {
  template <typename T, class D>
  void operator()(T, D d) const {
#if SIMD_TARGET_VALUE != SIMD_NONE
    const auto lo = iota(d, 1);
    const auto hi = iota(d, 1 + d.N);
    T expected[d.N];
    size_t i = 0;
    for (; i < d.N / 2; ++i) {
      expected[i] = 1 + i;
    }
    for (; i < d.N; ++i) {
      expected[i] = 1 + i + d.N;
    }
    ASSERT_VEC_EQ(d, expected, concat_hi_lo(hi, lo));
#endif
  }
};

struct TestOddEven {
  template <typename T, class D>
  void operator()(T, D d) const {
#if SIMD_TARGET_VALUE != SIMD_NONE
    const auto even = iota(d, 1);
    const auto odd = iota(d, 1 + d.N);
    T expected[d.N];
    for (size_t i = 0; i < d.N; ++i) {
      expected[i] = 1 + i + ((i & 1) ? d.N : 0);
    }
    ASSERT_VEC_EQ(d, expected, odd_even(odd, even));
#endif
  }
};

void TestSwizzle() {
  TestShiftBytes();
  TestBroadcast();
  ForeachLaneType<TestInterleave>();
  TestPermute();
  TestZip();
  TestShuffle();
  TestExtract();
  TestSpecialShuffles();
  ForeachLaneType<TestConcatHalves>();
  ForeachLaneType<TestConcatLoHi>();
  ForeachLaneType<TestConcatHiLo>();
  ForeachLaneType<TestOddEven>();
}

}  // namespace swizzle

void RunTests() {
  examples::TestExamples();
  basic::TestBasic();
  arithmetic::TestArithmetic();
  compare::TestCompare();
  logical::TestLogical();
  memory::TestMemory();
  convert::TestConvert();
  swizzle::TestSwizzle();
}

}  // namespace
}  // namespace SIMD_NAMESPACE

// Instantiate for the current target.
template <>
void SimdTest::operator()<SIMD_TARGET>(NotifyFailure notify_failure) {
  SIMD_NAMESPACE::g_notify_failure = notify_failure;
  SIMD_NAMESPACE::RunTests();
  targets |= SIMD_TARGET::value;
}

}  // namespace pik
