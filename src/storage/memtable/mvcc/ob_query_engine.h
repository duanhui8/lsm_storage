/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/mvcc/ob_query_engine.h */

#pragma once

#include <map>
#include <vector>
#include <functional>

#include "storage/memtable/ob_memtable_key.h"
#include "storage/memtable/mvcc/ob_mt_hash.h"
#include "storage/memtable/mvcc/ob_mvcc_row.h"

namespace oceanbase {
namespace memtable {

/**
 * ObIQueryEngineIterator — iterator interface for the memtable.
 * From: /opt/oceanbase4.4.2/src/storage/memtable/mvcc/ob_query_engine.h:38-58
 */
class ObIQueryEngineIterator
{
public:
  ObIQueryEngineIterator()          = default;
  virtual ~ObIQueryEngineIterator() = default;

  /** Iterate to the next non-empty ObMvccRow. */
  virtual int next() = 0;

  /** Get the key at current position. */
  virtual const ObMemtableKey *get_key() const = 0;

  /** Get the ObMvccRow at current position. */
  virtual ObMvccRow *get_value() const = 0;

  /** Whether this is a reverse scan. */
  virtual bool is_reverse_scan() const = 0;

  /** Reset the iterator. */
  virtual void reset() = 0;
};

/**
 * ObQueryEngine — dual-structure index: hash table + btree.
 * Hash table for point queries, btree for range scans.
 * Simplified from OB 4.4.2: using std::map for btree instead of ObKeyBtree.
 *
 * From: /opt/oceanbase4.4.2/src/storage/memtable/mvcc/ob_query_engine.h:64-200
 */
class ObQueryEngine
{
public:
  typedef ObMtHash KeyHash;
  typedef std::function<int(bool exists, ObStoreRowkeyWrapper &key, ObMvccRow *&row)> ObMvccRowCreator;

  ObQueryEngine()  = default;
  ~ObQueryEngine() = default;

  // ---- Insert operations (from OB 4.4.2) ----

  /** Insert key → value into both hash table and btree. */
  int set(const ObMemtableKey *key, ObMvccRow *value)
  {
    if (OB_ISNULL(key) || OB_ISNULL(value)) {
      return -1;
    }
    ObStoreRowkeyWrapper key_wrapper(key->get_rowkey());
    int ret = keyhash_.set(key_wrapper, value);
    if (ret == -1) {
      // Already exists in hash — return existing
      return -1;  // OB_ENTRY_EXIST
    }
    // Also insert into btree for range scan support
    btree_set_(key, value);
    value->set_btree_indexed();
    value->set_hash_indexed();
    return 0;
  }

  /** Ensure the key-value pair exists in the btree (for range scan correctness). */
  int ensure(const ObMemtableKey *key, ObMvccRow *value)
  {
    if (OB_ISNULL(key) || OB_ISNULL(value)) {
      return -1;
    }
    if (value->is_btree_indexed()) {
      return 0;  // Already in btree
    }
    btree_set_(key, value);
    value->set_btree_indexed();
    return 0;
  }

  // ---- Lookup operations ----

  /** Point lookup via hash table. */
  int get(const ObMemtableKey *parameter_key,
          ObMvccRow           *&row,
          ObMemtableKey       *returned_key)
  {
    if (OB_ISNULL(parameter_key)) {
      return -1;
    }
    ObStoreRowkeyWrapper key_wrapper(parameter_key->get_rowkey());
    return keyhash_.get(key_wrapper, row);
  }

  /** Get or create via btree (used by ObMvccEngineWithoutHashIndex). */
  int create_btree_kv(const ObMemtableKey       *key,
                      const ObMvccRowCreator     &row_creator,
                      ObMvccRow                 *&row)
  {
    if (OB_ISNULL(key)) {
      return -1;
    }
    // Check btree first
    auto iter = btree_.find(key->hash());
    if (iter != btree_.end()) {
      row = iter->second;
      return 0;
    }
    // Create new
    ObStoreRowkeyWrapper key_wrapper(key->get_rowkey());
    int ret = row_creator(false, key_wrapper, row);
    if (ret == 0 && row != nullptr) {
      btree_set_(key, row);
      row->set_btree_indexed();
    }
    return ret;
  }

  // ---- Scan operations ----

  /** Range scan via btree. Returns an iterator. */
  int scan(const ObMemtableKey *start_key, bool start_exclude,
           const ObMemtableKey *end_key,   bool end_exclude,
           ObIQueryEngineIterator *&ret_iter);

  /** Simple iterator: collect all keys in range into a vector. */
  int scan_range(const ObMemtableKey *start_key, bool start_exclude,
                 const ObMemtableKey *end_key,   bool end_exclude,
                 std::vector<std::pair<ObMemtableKey, ObMvccRow *>> &results);

  void revert_iter(ObIQueryEngineIterator *iter);

  // Direct all-values scan (bypasses iterator)
  const std::map<uint64_t, ObMvccRow *> &get_btree() const { return btree_; }

  // ---- Accessors ----
  KeyHash &get_keyhash() { return keyhash_; }
  size_t   size() const { return btree_.size(); }

private:
  void btree_set_(const ObMemtableKey *key, ObMvccRow *value)
  {
    btree_[key->hash()] = value;
  }

  // Provide a raw key pointer for btree lookup
  struct ObMemtableKeyRef
  {
    const char *data;
    int64_t len;
    uint64_t hash;
  };

  KeyHash                              keyhash_;  // hash table for point queries
  std::map<uint64_t, ObMvccRow *>      btree_;    // btree for range scans (simplified from ObKeyBtree)
};

// Btree iterator implementation using the sorted map
class ObQueryEngineBtreeIterator : public ObIQueryEngineIterator
{
public:
  ObQueryEngineBtreeIterator(std::map<uint64_t, ObMvccRow *> &btree,
                              uint64_t start_hash, bool start_exclude,
                              uint64_t end_hash,   bool end_exclude,
                              bool reverse)
      : btree_(btree), start_hash_(start_hash), end_hash_(end_hash),
        start_exclude_(start_exclude), end_exclude_(end_exclude),
        reverse_(reverse), curr_(), is_init_(false) {}

  int next() override
  {
    return next_internal_();
  }

  const ObMemtableKey *get_key() const override { return nullptr; /* simplified — no key stored */ }
  ObMvccRow *get_value() const override
  {
    if (curr_ != btree_.end()) return curr_->second;
    return nullptr;
  }
  bool is_reverse_scan() const override { return reverse_; }
  void reset() override { is_init_ = false; }

private:
  int next_internal_();

  std::map<uint64_t, ObMvccRow *> &btree_;
  uint64_t start_hash_, end_hash_;
  bool start_exclude_, end_exclude_, reverse_;
  std::map<uint64_t, ObMvccRow *>::const_iterator curr_;
  bool is_init_;
};

}  // namespace memtable
}  // namespace oceanbase
