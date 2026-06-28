/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

/**
 * ==========================================================================
 * ★★★ ob_memtable.h — 内存表（MemTable） ★★★
 * ==========================================================================
 *
 * ★ 在 LSM 架构中的位置：
 *
 *   MemTable 是 LSM-Tree 的"写入口"——所有写入首先到达 MemTable。
 *
 *   写入路径：
 *     ObLsmImpl::put(key, value, seq)
 *          │
 *          ├──→ ObLsmWal::put(seq, key, value)   [先写 WAL，保证持久性]
 *          │
 *          └──→ ObMemTable::put(seq, key, value)  [再写 MemTable]
 *                    │
 *                    ▼
 *              ObSkipList::insert(encoded_entry)
 *
 *   冻结路径（MemTable 写满时）：
 *     ObMemTable (active) → Immutable MemTable → SSTable (on disk)
 *          ↑                                              ↑
 *     接受新写入                                    Compaction 合并
 *
 * ★ 设计思想：写缓冲 + 批量刷盘
 *
 *   为什么需要 MemTable 而不直接写 SSTable？
 *   1. 磁盘 IO 的最小单元是 Block（4KB），单条记录通常几十字节——
 *      直接写磁盘会严重浪费 IO 带宽
 *   2. MemTable 在内存中累积多条写入，冻结后一次性批量生成 SSTable——
 *      将随机小写入转化为顺序大写入
 *   3. MemTable 提供读-写并发隔离——写入只操作 MemTable（内存），
 *      不影响已持久化的 SSTable（磁盘），读操作看到的是快照
 *
 * ★ Entry 编码格式（关键数据结构）：
 *
 *   每个 entry 在 SkipList 中存储为一个连续的字节序列：
 *
 *   ┌──────────────┬──────────┬───────────┬──────────────┬────────────┐
 *   │ key_size     │  key     │   seq     │  value_size  │   value    │
 *   │ (size_t,8B)  │ (变长)   │ (uint64)  │ (size_t,8B)  │  (变长)    │
 *   └──────────────┴──────────┴───────────┴──────────────┴────────────┘
 *
 *   注意：key_size 记录的是 internal_key 的大小（= user_key + 8B seq），
 *   但实际上 key + seq 是分开存储的（key 部分后面又存了一个 seq）。
 *   这个设计有点冗余——是 LevelDB 的原始设计，保留它是为了兼容性。
 *
 * ★ KeyComparator 的工作方式：
 *   比较时需要从 entry 字节序列中提取 internal_key（length-prefixed string），
 *   然后交给 ObInternalKeyComparator 比较（先比 user_key 字典序，相同再比 seq 降序）。
 *
 * ★ 内存管理：
 *   所有 entry 内存从 Arena 分配——MemTable 生命周期结束时统一释放。
 *
 * 💡 提问：为什么 MemTable 使用 shared_ptr + enable_shared_from_this？
 *   （提示：MemTable 在冻结后变成 Immutable MemTable，此时：
 *          1. 后台 Compaction 线程还在读它（生成 SSTable）
 *          2. 前台查询线程也可能在读它（Iterator 可能横跨新旧 MemTable）
 *          3. 需要"谁用谁持有引用"的安全模型——最后一个用完的人释放它）
 */

#pragma once

#include "common/sys/rc.h"
#include "common/lang/string.h"
#include "common/lang/string_view.h"
#include "common/lang/memory.h"
#include "oblsm/memtable/ob_skiplist.h"
#include "oblsm/util/ob_comparator.h"
#include "oblsm/util/ob_arena.h"
#include "oblsm/include/ob_lsm_iterator.h"

namespace oceanbase {

class ObMemTable : public enable_shared_from_this<ObMemTable>
{
public:
  ObMemTable() : comparator_(), table_(comparator_){};
  ~ObMemTable() = default;

  shared_ptr<ObMemTable> get_shared_ptr() { return shared_from_this(); }

  void put(uint64_t seq, const string_view &key, const string_view &value);

  /**
   * ★ appro_memory_usage — 近似内存用量
   *
   * 返回 Arena 追踪的内存使用量。为什么是"近似"？
   * 因为 SkipList 节点本身的开销（指针数组、malloc header）没算进去。
   * 但对于判断"MemTable 是否写满"来说，这个近似值足够准确。
   */
  size_t appro_memory_usage() const { return arena_.memory_usage(); }

  ObLsmIterator *new_iterator();

private:
  friend class ObMemTableIterator;

  /**
   * ★ KeyComparator — MemTable 内部的 Key 比较器
   *
   * 从 entry 的字节序列中提取 internal_key（length-prefixed string），
   * 然后委托给 ObInternalKeyComparator。
   *
   * ★ 为什么需要这个包装？
   *   SkipList 模板参数需要 Key 类型和 Comparator 类型。
   *   这里 Key = const char*（指向 entry 字节序列的指针），
   *   Comparator 需要能从 char* 中解析出真正的 key 来比较。
   *   所以 KeyComparator 就是做这个"解析 + 比较"的适配器。
   */
  struct KeyComparator
  {
    const ObInternalKeyComparator comparator;
    explicit KeyComparator() {}
    int operator()(const char *a, const char *b) const;
  };

  // ★ Table 是 SkipList 的别名。键是 const char*（entry 数据指针），
  //   比较器是 KeyComparator。
  //
  //   TODO 注释提到：未来可能换成其他数据结构（如 Hash Table）。
  //   这就是用 typedef 的原因——将来换数据结构只需要改这一行。
  typedef ObSkipList<const char *, KeyComparator> Table;

  KeyComparator comparator_;
  Table         table_;   // ★ 底层跳表
  ObArena       arena_;   // ★ 内存分配器
};

/**
 * ★ ObMemTableIterator — MemTable 的迭代器
 *
 * 包装 SkipList::Iterator，把 entry 字节序列解析为 key/value。
 *
 * key() 和 value() 的实现是核心：
 *   - key()：从 entry 字节序列中提取 length-prefixed 的 internal_key
 *   - value()：先获取 internal_key，跳过它的字节，再读取 length-prefixed 的 value
 *
 * 两个函数都是零拷贝的——返回 string_view 指向 entry 内部数据。
 */
class ObMemTableIterator : public ObLsmIterator
{
public:
  explicit ObMemTableIterator(shared_ptr<ObMemTable> mem, ObMemTable::Table *table) : mem_(mem), iter_(table) {}

  ObMemTableIterator(const ObMemTableIterator &)            = delete;
  ObMemTableIterator &operator=(const ObMemTableIterator &) = delete;
  ~ObMemTableIterator() override = default;

  void        seek(const string_view &k) override;
  void        seek_to_first() override { iter_.seek_to_first(); }
  void        seek_to_last() override { iter_.seek_to_last(); }
  bool        valid() const override { return iter_.valid(); }
  void        next() override { iter_.next(); }
  string_view key() const override;
  string_view value() const override;

private:
  shared_ptr<ObMemTable>      mem_;   // ★ 持有 MemTable 的 shared_ptr，保证生命周期
  ObMemTable::Table::Iterator iter_;  // ★ 底层 SkipList 迭代器
  string                      tmp_;   // ★ seek 用的临时 string
};

}  // namespace oceanbase
