/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "storage/ob_i_store.h"

namespace oceanbase {
namespace storage {

uint64_t ObStoreRowkey::murmurhash(uint64_t seed) const
{
  const char *ptr = get_data();
  if (ptr == nullptr || length_ == 0) return seed;

  const uint64_t m = 0xc6a4a7935bd1e995ULL;
  const int      r = 47;
  uint64_t       h = seed ^ (length_ * m);

  const uint64_t *data = reinterpret_cast<const uint64_t *>(ptr);
  const uint64_t *end  = data + (length_ / 8);

  while (data != end) {
    uint64_t k = *data++;
    k *= m;
    k ^= k >> r;
    k *= m;
    h ^= k;
    h *= m;
  }

  const unsigned char *data2 = reinterpret_cast<const unsigned char *>(data);
  switch (length_ & 7) {
    case 7: h ^= uint64_t(data2[6]) << 48; [[fallthrough]];
    case 6: h ^= uint64_t(data2[5]) << 40; [[fallthrough]];
    case 5: h ^= uint64_t(data2[4]) << 32; [[fallthrough]];
    case 4: h ^= uint64_t(data2[3]) << 24; [[fallthrough]];
    case 3: h ^= uint64_t(data2[2]) << 16; [[fallthrough]];
    case 2: h ^= uint64_t(data2[1]) << 8;  [[fallthrough]];
    case 1: h ^= uint64_t(data2[0]);
            h *= m;
  }

  h ^= h >> r;
  h *= m;
  h ^= h >> r;
  return h;
}

}  // namespace storage
}  // namespace oceanbase
