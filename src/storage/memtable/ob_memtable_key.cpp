/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include <cstring>

#include "storage/memtable/ob_memtable_key.h"

namespace oceanbase {
namespace memtable {

uint64_t ObMemtableKey::murmurhash_(uint64_t seed) const
{
  if (key_data_ == nullptr || key_len_ == 0) return seed;

  const uint64_t m = 0xc6a4a7935bd1e995ULL;
  const int      r = 47;
  uint64_t       h = seed ^ (key_len_ * m);

  const uint64_t *data = reinterpret_cast<const uint64_t *>(key_data_);
  const uint64_t *end  = data + (key_len_ / 8);

  while (data != end) {
    uint64_t k = *data++;
    k *= m;
    k ^= k >> r;
    k *= m;
    h ^= k;
    h *= m;
  }

  const unsigned char *data2 = reinterpret_cast<const unsigned char *>(data);
  switch (key_len_ & 7) {
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

int ObMemtableKey::dup(ObMemtableKey *&new_key) const
{
  if (key_len_ == 0 || key_data_ == nullptr) {
    new_key = nullptr;
    return 0;
  }
  char *buf = new char[sizeof(ObMemtableKey) + key_len_];
  if (OB_ISNULL(buf)) {
    return -1;
  }
  char *key_buf = buf + sizeof(ObMemtableKey);
  MEMCPY(key_buf, key_data_, key_len_);
  new_key = new (buf) ObMemtableKey(key_buf, key_len_);
  new_key->hash_val_ = hash_val_;
  return 0;
}

}  // namespace memtable
}  // namespace oceanbase
