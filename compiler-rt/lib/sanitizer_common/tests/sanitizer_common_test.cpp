//===-- sanitizer_common_test.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//
#include <algorithm>

#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_file.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_platform.h"

#include "sanitizer_pthread_wrappers.h"

#include "gtest/gtest.h"

namespace __sanitizer {

static bool IsSorted(const uptr *array, uptr n) {
  for (uptr i = 1; i < n; i++) {
    if (array[i] < array[i - 1]) return false;
  }
  return true;
}

TEST(SanitizerCommon, SortTest) {
  uptr array[100];
  uptr n = 100;
  // Already sorted.
  for (uptr i = 0; i < n; i++) {
    array[i] = i;
  }
  Sort(array, n);
  EXPECT_TRUE(IsSorted(array, n));
  // Reverse order.
  for (uptr i = 0; i < n; i++) {
    array[i] = n - 1 - i;
  }
  Sort(array, n);
  EXPECT_TRUE(IsSorted(array, n));
  // Mixed order.
  for (uptr i = 0; i < n; i++) {
    array[i] = (i % 2 == 0) ? i : n - 1 - i;
  }
  Sort(array, n);
  EXPECT_TRUE(IsSorted(array, n));
  // All equal.
  for (uptr i = 0; i < n; i++) {
    array[i] = 42;
  }
  Sort(array, n);
  EXPECT_TRUE(IsSorted(array, n));
  // All but one sorted.
  for (uptr i = 0; i < n - 1; i++) {
    array[i] = i;
  }
  array[n - 1] = 42;
  Sort(array, n);
  EXPECT_TRUE(IsSorted(array, n));
  // Minimal case - sort three elements.
  array[0] = 1;
  array[1] = 0;
  Sort(array, 2);
  EXPECT_TRUE(IsSorted(array, 2));
}

TEST(SanitizerCommon, MmapAlignedOrDieOnFatalError) {
  uptr PageSize = GetPageSizeCached();
  for (uptr size = 1; size <= 32; size *= 2) {
    for (uptr alignment = 1; alignment <= 32; alignment *= 2) {
      for (int iter = 0; iter < 100; iter++) {
        uptr res = (uptr)MmapAlignedOrDieOnFatalError(
            size * PageSize, alignment * PageSize, "MmapAlignedOrDieTest");
        EXPECT_EQ(0U, res % (alignment * PageSize));
        internal_memset((void*)res, 1, size * PageSize);
        UnmapOrDie((void*)res, size * PageSize);
      }
    }
  }
}

TEST(SanitizerCommon, InternalMmapVectorRoundUpCapacity) {
  InternalMmapVector<uptr> v;
  v.reserve(1);
  CHECK_EQ(v.capacity(), GetPageSizeCached() / sizeof(uptr));
}

TEST(SanitizerCommon, InternalMmapVectorResize) {
  InternalMmapVector<uptr> v;
  CHECK_EQ(0U, v.size());
  CHECK_GE(v.capacity(), v.size());

  v.reserve(1000);
  CHECK_EQ(0U, v.size());
  CHECK_GE(v.capacity(), 1000U);

  v.resize(10000);
  CHECK_EQ(10000U, v.size());
  CHECK_GE(v.capacity(), v.size());
  uptr cap = v.capacity();

  v.resize(100);
  CHECK_EQ(100U, v.size());
  CHECK_EQ(v.capacity(), cap);

  v.reserve(10);
  CHECK_EQ(100U, v.size());
  CHECK_EQ(v.capacity(), cap);
}

TEST(SanitizerCommon, InternalMmapVector) {
  InternalMmapVector<uptr> vector;
  for (uptr i = 0; i < 100; i++) {
    EXPECT_EQ(i, vector.size());
    vector.push_back(i);
  }
  for (uptr i = 0; i < 100; i++) {
    EXPECT_EQ(i, vector[i]);
  }
  for (int i = 99; i >= 0; i--) {
    EXPECT_EQ((uptr)i, vector.back());
    vector.pop_back();
    EXPECT_EQ((uptr)i, vector.size());
  }
  InternalMmapVector<uptr> empty_vector;
  CHECK_EQ(empty_vector.capacity(), 0U);
  CHECK_EQ(0U, empty_vector.size());
}

TEST(SanitizerCommon, InternalMmapVectorEq) {
  InternalMmapVector<uptr> vector1;
  InternalMmapVector<uptr> vector2;
  for (uptr i = 0; i < 100; i++) {
    vector1.push_back(i);
    vector2.push_back(i);
  }
  EXPECT_TRUE(vector1 == vector2);
  EXPECT_FALSE(vector1 != vector2);

  vector1.push_back(1);
  EXPECT_FALSE(vector1 == vector2);
  EXPECT_TRUE(vector1 != vector2);

  vector2.push_back(1);
  EXPECT_TRUE(vector1 == vector2);
  EXPECT_FALSE(vector1 != vector2);

  vector1[55] = 1;
  EXPECT_FALSE(vector1 == vector2);
  EXPECT_TRUE(vector1 != vector2);
}

TEST(SanitizerCommon, InternalMmapVectorSwap) {
  InternalMmapVector<uptr> vector1;
  InternalMmapVector<uptr> vector2;
  InternalMmapVector<uptr> vector3;
  InternalMmapVector<uptr> vector4;
  for (uptr i = 0; i < 100; i++) {
    vector1.push_back(i);
    vector2.push_back(i);
    vector3.push_back(-i);
    vector4.push_back(-i);
  }
  EXPECT_NE(vector2, vector3);
  EXPECT_NE(vector1, vector4);
  vector1.swap(vector3);
  EXPECT_EQ(vector2, vector3);
  EXPECT_EQ(vector1, vector4);
}

TEST(SanitizerCommon, InternalMmapVectorErase) {
  InternalMmapVector<uptr> v;
  std::vector<uptr> r;
  for (uptr i = 0; i < 10; i++) {
    v.push_back(i);
    r.push_back(i);
  }

  v.erase(&v[9]);
  r.erase(r.begin() + 9);
  EXPECT_EQ(r.size(), v.size());
  for (uptr i = 0; i < r.size(); i++) EXPECT_EQ(r[i], v[i]);

  v.erase(&v[3]);
  r.erase(r.begin() + 3);
  EXPECT_EQ(r.size(), v.size());
  for (uptr i = 0; i < r.size(); i++) EXPECT_EQ(r[i], v[i]);

  v.erase(&v[0]);
  r.erase(r.begin());
  EXPECT_EQ(r.size(), v.size());
  for (uptr i = 0; i < r.size(); i++) EXPECT_EQ(r[i], v[i]);
}

void TestThreadInfo(bool main) {
  uptr stk_addr = 0;
  uptr stk_size = 0;
  uptr tls_addr = 0;
  uptr tls_size = 0;
  GetThreadStackAndTls(main, &stk_addr, &stk_size, &tls_addr, &tls_size);

  int stack_var;
  EXPECT_NE(stk_addr, (uptr)0);
  EXPECT_NE(stk_size, (uptr)0);
  EXPECT_GT((uptr)&stack_var, stk_addr);
  EXPECT_LT((uptr)&stack_var, stk_addr + stk_size);

#if SANITIZER_LINUX && defined(__x86_64__)
  static __thread int thread_var;
  EXPECT_NE(tls_addr, (uptr)0);
  EXPECT_NE(tls_size, (uptr)0);
  EXPECT_GT((uptr)&thread_var, tls_addr);
  EXPECT_LT((uptr)&thread_var, tls_addr + tls_size);

  // Ensure that tls and stack do not intersect.
  uptr tls_end = tls_addr + tls_size;
  EXPECT_TRUE(tls_addr < stk_addr || tls_addr >= stk_addr + stk_size);
  EXPECT_TRUE(tls_end  < stk_addr || tls_end  >=  stk_addr + stk_size);
  EXPECT_TRUE((tls_addr < stk_addr) == (tls_end  < stk_addr));
#endif
}

static void *WorkerThread(void *arg) {
  TestThreadInfo(false);
  return 0;
}

TEST(SanitizerCommon, ThreadStackTlsMain) {
  InitTlsSize();
  TestThreadInfo(true);
}

TEST(SanitizerCommon, ThreadStackTlsWorker) {
  InitTlsSize();
  pthread_t t;
  PTHREAD_CREATE(&t, 0, WorkerThread, 0);
  PTHREAD_JOIN(t, 0);
}

bool UptrLess(uptr a, uptr b) {
  return a < b;
}

TEST(SanitizerCommon, InternalLowerBound) {
  static const uptr kSize = 5;
  int arr[kSize];
  arr[0] = 1;
  arr[1] = 3;
  arr[2] = 5;
  arr[3] = 7;
  arr[4] = 11;

  EXPECT_EQ(0u, InternalLowerBound(arr, 0, kSize, 0, UptrLess));
  EXPECT_EQ(0u, InternalLowerBound(arr, 0, kSize, 1, UptrLess));
  EXPECT_EQ(1u, InternalLowerBound(arr, 0, kSize, 2, UptrLess));
  EXPECT_EQ(1u, InternalLowerBound(arr, 0, kSize, 3, UptrLess));
  EXPECT_EQ(2u, InternalLowerBound(arr, 0, kSize, 4, UptrLess));
  EXPECT_EQ(2u, InternalLowerBound(arr, 0, kSize, 5, UptrLess));
  EXPECT_EQ(3u, InternalLowerBound(arr, 0, kSize, 6, UptrLess));
  EXPECT_EQ(3u, InternalLowerBound(arr, 0, kSize, 7, UptrLess));
  EXPECT_EQ(4u, InternalLowerBound(arr, 0, kSize, 8, UptrLess));
  EXPECT_EQ(4u, InternalLowerBound(arr, 0, kSize, 9, UptrLess));
  EXPECT_EQ(4u, InternalLowerBound(arr, 0, kSize, 10, UptrLess));
  EXPECT_EQ(4u, InternalLowerBound(arr, 0, kSize, 11, UptrLess));
  EXPECT_EQ(5u, InternalLowerBound(arr, 0, kSize, 12, UptrLess));
}

TEST(SanitizerCommon, InternalLowerBoundVsStdLowerBound) {
  std::vector<int> data;
  auto create_item = [] (size_t i, size_t j) {
    auto v = i * 10000 + j;
    return ((v << 6) + (v >> 6) + 0x9e3779b9) % 100;
  };
  for (size_t i = 0; i < 1000; ++i) {
    data.resize(i);
    for (size_t j = 0; j < i; ++j) {
      data[j] = create_item(i, j);
    }

    std::sort(data.begin(), data.end());

    for (size_t j = 0; j < i; ++j) {
      int val = create_item(i, j);
      for (auto to_find : {val - 1, val, val + 1}) {
        uptr expected =
            std::lower_bound(data.begin(), data.end(), to_find) - data.begin();
        EXPECT_EQ(expected, InternalLowerBound(data.data(), 0, data.size(),
                                               to_find, std::less<int>()));
      }
    }
  }
}

#if SANITIZER_LINUX && !SANITIZER_ANDROID
TEST(SanitizerCommon, FindPathToBinary) {
  char *true_path = FindPathToBinary("true");
  EXPECT_NE((char*)0, internal_strstr(true_path, "/bin/true"));
  InternalFree(true_path);
  EXPECT_EQ(0, FindPathToBinary("unexisting_binary.ergjeorj"));
}
#elif SANITIZER_WINDOWS
TEST(SanitizerCommon, FindPathToBinary) {
  // ntdll.dll should be on PATH in all supported test environments on all
  // supported Windows versions.
  char *ntdll_path = FindPathToBinary("ntdll.dll");
  EXPECT_NE((char*)0, internal_strstr(ntdll_path, "ntdll.dll"));
  InternalFree(ntdll_path);
  EXPECT_EQ(0, FindPathToBinary("unexisting_binary.ergjeorj"));
}
#endif

TEST(SanitizerCommon, StripPathPrefix) {
  EXPECT_EQ(0, StripPathPrefix(0, "prefix"));
  EXPECT_STREQ("foo", StripPathPrefix("foo", 0));
  EXPECT_STREQ("dir/file.cc",
               StripPathPrefix("/usr/lib/dir/file.cc", "/usr/lib/"));
  EXPECT_STREQ("/file.cc", StripPathPrefix("/usr/myroot/file.cc", "/myroot"));
  EXPECT_STREQ("file.h", StripPathPrefix("/usr/lib/./file.h", "/usr/lib/"));
}

TEST(SanitizerCommon, RemoveANSIEscapeSequencesFromString) {
  RemoveANSIEscapeSequencesFromString(nullptr);
  const char *buffs[22] = {
    "Default",                                "Default",
    "\033[95mLight magenta",                  "Light magenta",
    "\033[30mBlack\033[32mGreen\033[90mGray", "BlackGreenGray",
    "\033[106mLight cyan \033[107mWhite ",    "Light cyan White ",
    "\033[31mHello\033[0m World",             "Hello World",
    "\033[38;5;82mHello \033[38;5;198mWorld", "Hello World",
    "123[653456789012",                       "123[653456789012",
    "Normal \033[5mBlink \033[25mNormal",     "Normal Blink Normal",
    "\033[106m\033[107m",                     "",
    "",                                       "",
    " ",                                      " ",
  };

  for (size_t i = 0; i < ARRAY_SIZE(buffs); i+=2) {
    char *buffer_copy = internal_strdup(buffs[i]);
    RemoveANSIEscapeSequencesFromString(buffer_copy);
    EXPECT_STREQ(buffer_copy, buffs[i+1]);
    InternalFree(buffer_copy);
  }
}

TEST(SanitizerCommon, InternalScopedString) {
  InternalScopedString str(10);
  EXPECT_EQ(0U, str.length());
  EXPECT_STREQ("", str.data());

  str.append("foo");
  EXPECT_EQ(3U, str.length());
  EXPECT_STREQ("foo", str.data());

  int x = 1234;
  str.append("%d", x);
  EXPECT_EQ(7U, str.length());
  EXPECT_STREQ("foo1234", str.data());

  str.append("%d", x);
  EXPECT_EQ(9U, str.length());
  EXPECT_STREQ("foo123412", str.data());

  str.clear();
  EXPECT_EQ(0U, str.length());
  EXPECT_STREQ("", str.data());

  str.append("0123456789");
  EXPECT_EQ(9U, str.length());
  EXPECT_STREQ("012345678", str.data());
}

#if SANITIZER_LINUX || SANITIZER_FREEBSD || \
  SANITIZER_MAC || SANITIZER_IOS
TEST(SanitizerCommon, GetRandom) {
  u8 buffer_1[32], buffer_2[32];
  for (bool blocking : { false, true }) {
    EXPECT_FALSE(GetRandom(nullptr, 32, blocking));
    EXPECT_FALSE(GetRandom(buffer_1, 0, blocking));
    EXPECT_FALSE(GetRandom(buffer_1, 512, blocking));
    EXPECT_EQ(ARRAY_SIZE(buffer_1), ARRAY_SIZE(buffer_2));
    for (uptr size = 4; size <= ARRAY_SIZE(buffer_1); size += 4) {
      for (uptr i = 0; i < 100; i++) {
        EXPECT_TRUE(GetRandom(buffer_1, size, blocking));
        EXPECT_TRUE(GetRandom(buffer_2, size, blocking));
        EXPECT_NE(internal_memcmp(buffer_1, buffer_2, size), 0);
      }
    }
  }
}
#endif

TEST(SanitizerCommon, ReservedAddressRangeInit) {
  uptr init_size = 0xffff;
  ReservedAddressRange address_range;
  uptr res = address_range.Init(init_size);
  CHECK_NE(res, (void*)-1);
  UnmapOrDie((void*)res, init_size);
  // Should be able to map into the same space now.
  ReservedAddressRange address_range2;
  uptr res2 = address_range2.Init(init_size, nullptr, res);
  CHECK_EQ(res, res2);

  // TODO(flowerhack): Once this is switched to the "real" implementation
  // (rather than passing through to MmapNoAccess*), enforce and test "no
  // double initializations allowed"
}

TEST(SanitizerCommon, ReservedAddressRangeMap) {
  constexpr uptr init_size = 0xffff;
  ReservedAddressRange address_range;
  uptr res = address_range.Init(init_size);
  CHECK_NE(res, (void*) -1);

  // Valid mappings should succeed.
  CHECK_EQ(res, address_range.Map(res, init_size));

  // Valid mappings should be readable.
  unsigned char buffer[init_size];
  memcpy(buffer, reinterpret_cast<void *>(res), init_size);

  // TODO(flowerhack): Once this is switched to the "real" implementation, make
  // sure you can only mmap into offsets in the Init range.
}

TEST(SanitizerCommon, ReservedAddressRangeUnmap) {
  uptr PageSize = GetPageSizeCached();
  uptr init_size = PageSize * 8;
  ReservedAddressRange address_range;
  uptr base_addr = address_range.Init(init_size);
  CHECK_NE(base_addr, (void*)-1);
  CHECK_EQ(base_addr, address_range.Map(base_addr, init_size));

  // Unmapping the entire range should succeed.
  address_range.Unmap(base_addr, init_size);

  // Map a new range.
  base_addr = address_range.Init(init_size);
  CHECK_EQ(base_addr, address_range.Map(base_addr, init_size));

  // Windows doesn't allow partial unmappings.
  #if !SANITIZER_WINDOWS

  // Unmapping at the beginning should succeed.
  address_range.Unmap(base_addr, PageSize);

  // Unmapping at the end should succeed.
  uptr new_start = reinterpret_cast<uptr>(address_range.base()) +
                   address_range.size() - PageSize;
  address_range.Unmap(new_start, PageSize);

  #endif

  // Unmapping in the middle of the ReservedAddressRange should fail.
  EXPECT_DEATH(address_range.Unmap(base_addr + (PageSize * 2), PageSize), ".*");
}

// Windows has no working ReadBinaryName.
#if !SANITIZER_WINDOWS
TEST(SanitizerCommon, ReadBinaryNameCached) {
  char buf[256];
  EXPECT_NE((uptr)0, ReadBinaryNameCached(buf, sizeof(buf)));
}
#endif

}  // namespace __sanitizer
