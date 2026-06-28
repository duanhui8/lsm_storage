/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "storage/memtable/mvcc/ob_query_engine.h"

namespace oceanbase {
namespace memtable {

int ObQueryEngine::scan(const ObMemtableKey *start_key, bool start_exclude,
                         const ObMemtableKey *end_key,   bool end_exclude,
                         ObIQueryEngineIterator *&ret_iter)
{
  uint64_t start_h = start_key ? start_key->hash() : 0;
  uint64_t end_h   = end_key   ? end_key->hash()   : UINT64_MAX;

  auto *iter = new ObQueryEngineBtreeIterator(btree_, start_h, start_exclude,
                                               end_h, end_exclude, false);
  ret_iter = iter;
  return 0;
}

int ObQueryEngine::scan_range(const ObMemtableKey *start_key, bool start_exclude,
                               const ObMemtableKey *end_key,   bool end_exclude,
                               std::vector<std::pair<ObMemtableKey, ObMvccRow *>> &results)
{
  uint64_t start_h = start_key ? start_key->hash() : 0;
  uint64_t end_h   = end_key   ? end_key->hash()   : UINT64_MAX;

  auto it = btree_.lower_bound(start_h);
  if (start_exclude && it != btree_.end() && it->first == start_h) {
    ++it;
  }

  while (it != btree_.end()) {
    if (it->first > end_h) break;
    if (end_exclude && it->first == end_h) break;
    // We don't store the key separately — simplified
    if (it->second != nullptr && !it->second->is_empty()) {
      results.push_back({ObMemtableKey(), it->second});
    }
    ++it;
  }
  return 0;
}

void ObQueryEngine::revert_iter(ObIQueryEngineIterator *iter)
{
  delete iter;
}

int ObQueryEngineBtreeIterator::next_internal_()
{
  if (!is_init_) {
    curr_     = btree_.lower_bound(start_hash_);
    is_init_  = true;
  } else {
    if (curr_ != btree_.end()) ++curr_;
  }

  while (curr_ != btree_.end()) {
    if (curr_->first > end_hash_) {
      curr_ = btree_.end();
      return -1;  // OB_ITER_END
    }
    if (end_exclude_ && curr_->first == end_hash_) {
      curr_ = btree_.end();
      return -1;
    }
    // Skip empty rows
    if (curr_->second != nullptr && !curr_->second->is_empty()) {
      return 0;
    }
    ++curr_;
  }
  return -1;  // OB_ITER_END
}

}  // namespace memtable
}  // namespace oceanbase
