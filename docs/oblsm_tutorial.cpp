/**
 * ==========================================================================
 * 【重点学习模块】oblsm/ — 完整独立的 LSM-Tree 实现
 * ==========================================================================
 *
 * ★ 为什么这是最有价值的学习模块？
 *
 *   oblsm 是一个完全独立的 LSM-Tree 存储引擎实现，约 2500 行 C++ 代码，
 *   包含了 LSM-Tree 的所有核心组件。它可以脱离 MiniOB 独立编译使用。
 *
 *   学习这个模块，你就能理解 LevelDB/RocksDB/Cassandra/HBase 等
 *   所有基于 LSM-Tree 的存储系统的工作原理。
 *
 * ==========================================================================
 * ★ LSM-Tree 核心架构图
 * ==========================================================================
 *
 *   写入路径 (Write Path):
 *   ┌──────────┐
 *   │  put(k,v) │
 *   └────┬─────┘
 *        ↓
 *   ┌──────────┐     ┌─────┐
 *   │  WAL 日志 │────→│ 磁盘 │  (先写日志，保证持久性)
 *   └────┬─────┘     └─────┘
 *        ↓
 *   ┌──────────────┐
 *   │  MemTable    │  (活跃内存表，SkipList 实现)
 *   │  (Active)    │
 *   └──────┬───────┘
 *          ↓ (满了/freeze)
 *   ┌──────────────┐
 *   │  Immutable   │  (冻结的 MemTable，只读，等待 flush)
 *   │  MemTable    │
 *   └──────┬───────┘
 *          ↓ (build_sstable / compaction)
 *   ┌──────────────────────────────────────┐
 *   │  SSTable (磁盘有序表)                │
 *   │  Level 0: [SST1] [SST2] [SST3]     │  (key 范围可能重叠)
 *   │  Level 1: [  SST4  ] [  SST5  ]    │  (key 范围不重叠)
 *   │  Level 2: [SST6][SST7][SST8][SST9] │  (更大，更多文件)
 *   └──────────────────────────────────────┘
 *
 *   读取路径 (Read Path):
 *   ┌──────────┐
 *   │  get(key) │
 *   └────┬─────┘
 *        ↓
 *   ┌──────────┐  找到? → 返回最新版本
 *   │ MemTable │  没找到 ↓
 *   └────┬─────┘
 *        ↓
 *   ┌────────────┐  找到? → 返回
 *   │ Immutable  │  没找到 ↓
 *   │ MemTables  │
 *   └────┬───────┘
 *        ↓
 *   ┌──────────┐  从 Level 0 到 Level N 逐层查找
 *   │ SSTable   │  Block 索引 → 二分查找 → 读取 Block → 二分查找 entry
 *   │ Level 0   │  配合 BloomFilter 快速排除不存在的 key
 *   │  ...      │
 *   │ Level N   │
 *   └──────────┘
 *
 * ==========================================================================
 * ★ 文件结构与职责
 * ==========================================================================
 *
 *   oblsm/
 *   ├── ob_lsm_impl.h/cpp      — 顶层引擎实现（put/get/compact）
 *   ├── ob_lsm_transaction.cpp — 事务支持（简单的 seq-based MVCC）
 *   ├── ob_manifest.h/cpp      — 元数据持久化（记录 SSTable 列表变更）
 *   ├── ob_user_iterator.cpp   — 用户层迭代器（合并多个数据源）
 *   │
 *   ├── memtable/
 *   │   ├── ob_memtable.h/cpp  — 内存表（SkipList）
 *   │   └── ob_skiplist.h      — ★ 跳表实现（核心数据结构）
 *   │
 *   ├── table/
 *   │   ├── ob_sstable.h/cpp   — SSTable（磁盘有序表）
 *   │   ├── ob_sstable_builder.cpp — SSTable 构建器
 *   │   ├── ob_block.h/cpp     — ★ Block 格式（数据块编解码）
 *   │   ├── ob_block_builder.cpp — Block 构建器
 *   │   └── ob_merger.cpp      — 归并迭代器（compaction 时合并多个源）
 *   │
 *   ├── compaction/
 *   │   ├── ob_compaction.h/cpp — Compaction 调度
 *   │   └── ob_compaction_picker.cpp — 选择哪些 SSTable 参与合并
 *   │
 *   ├── wal/
 *   │   └── ob_lsm_wal.h/cpp   — Write-Ahead Log
 *   │
 *   └── util/
 *       ├── ob_skiplist.h      — (或在此) SkipList 实现
 *       ├── ob_comparator.h    — Key 比较器（支持 user key + seq）
 *       ├── ob_arena.h/cpp     — 内存池
 *       ├── ob_bloomfilter.h   — 布隆过滤器
 *       ├── ob_lru_cache.h     — LRU 缓存
 *       ├── ob_file_reader.h   — 文件读取（随机读）
 *       └── ob_file_writer.h   — 文件写入（顺序写）
 *
 * ==========================================================================
 * ★ 关键数据结构
 * ==========================================================================
 */

// ---------------------------------------------------------------------------
// 1. InternalKey 格式
// ---------------------------------------------------------------------------
/**
 * ★ LSM 内部 Key 的编码规则：
 *
 *   InternalKey = user_key + (sequence_number << 8) + key_type
 *
 *   user_key:  用户提供的原始 key（如 "Alice"）
 *   seq:       序列号（单调递增，每次写入 +1）
 *   key_type:  Put(0x01) 或 Delete(0x02)（墓碑标记）
 *
 * ★ 为什么需要 seq？
 *   LSM 不直接修改/删除磁盘上的旧数据，而是写入新版本。
 *   同一个 user_key 可能有多条记录（不同 seq），seq 最大的最新。
 *   Delete 操作实际是写入一条 key_type=Delete 的记录（墓碑）。
 *
 * ★ 比较规则（ObInternalKeyComparator）：
 *   先比较 user_key，相同 user_key 时 seq 大的排在前面（降序）。
 *   这样 seek(key) 找到的第一个就是最新版本。
 *
 * 💡 提问：如果 user_key 相同但 seq 不同，所有版本都会持久化吗？
 *   Compaction 时旧版本会被丢弃（只保留最新版本或墓碑）。
 *   但 Compaction 之前，磁盘上可能有多个版本。
 */

// ---------------------------------------------------------------------------
// 2. MemTable — SkipList 内存表
// ---------------------------------------------------------------------------
/**
 * ★ ObMemTable 是最活跃的数据结构，所有写入首先到达这里。
 *
 * 数据结构：ObSkipList<const char *, KeyComparator>
 *
 * 跳表（SkipList）：
 *   一种多级链表，查找/插入/删除的时间复杂度 O(log N)，
 *   和平衡树相当，但实现简单、并发控制容易。
 *
 *   例：跳表结构（示意的多层链表）：
 *   Level 2:  head ────────────→ 50 ────────────→ NULL
 *   Level 1:  head ──→ 20 ──→ 30 ──→ 50 ──→ 70 → NULL
 *   Level 0:  head → 10 → 20 → 25 → 30 → 50 → 60 → 70 → 80 → NULL
 *
 *   查找 60：
 *     Level 2: head→50 (60>50, 继续) → NULL (回头, 降级)
 *     Level 1: 50→70 (60<70, 回头, 降级)
 *     Level 0: 50→60 ✓ 找到
 *
 * ★ 内存管理：ObArena
 *   所有 key/value 存储在 Arena 中（一块连续内存），随 MemTable 一起释放。
 *   不需要逐条 free，性能极高。
 *
 * 💡 提问：为什么 MemTable 用 SkipList 而不用红黑树？
 *   （提示：考虑并发写入、范围扫描的实现复杂度）
 *
 * 💡 提问：SkipList 的"随机层数"是怎么决定的？
 *   看 ob_skiplist.h 的实现，每层上升概率 1/2（类似抛硬币）。
 *   为什么是这个概率？
 */

// ---------------------------------------------------------------------------
// 3. SSTable — 磁盘有序表
// ---------------------------------------------------------------------------
/**
 * ★ SSTable = 数据块序列 + 元数据块 + 索引
 *
 * 文件格式（ascii 图来自 ob_sstable.h 源码注释）：
 *
 *      ┌─────────────────┐
 *      │    block 1      │◄──┐
 *      ├─────────────────┤   │
 *      │    block 2      │   │
 *      ├─────────────────┤   │ 每个 Block 存若干条 KV entry
 *      │      ..         │   │
 *      ├─────────────────┤   │
 *      │    block n      │◄┐ │
 *      ├─────────────────┤ │ │
 *   ┌─►│  meta size(n)   │ │ │  BlockMeta 数量
 *   │  ├─────────────────┤ │ │
 *   │  │block meta 1 size│ │ │  每个 BlockMeta 包含：
 *   │  ├─────────────────┤ │ │    - first_key（Block 中最小的 key）
 *   │  │  block meta 1   ┼─┼─┘    - last_key（Block 中最大的 key）
 *   │  ├─────────────────┤ │      - offset（Block 在文件中的偏移）
 *   │  │      ..         │ │      - size（Block 的大小）
 *   │  ├─────────────────┤ │
 *   │  │block meta n size│ │
 *   │  ├─────────────────┤ │
 *   │  │  block meta n   ┼─┘
 *   │  ├─────────────────┤
 *   └──┤                 │ ← 文件末尾：meta 起始位置的偏移
 *      └─────────────────┘
 *
 * ★ 查找过程：
 *   1. 读文件末尾，获取 meta 起始偏移 → 读 BlockMeta 列表
 *   2. 二分查找 BlockMeta（比较 first_key 和 last_key），定位目标 Block
 *   3. 读 Block 数据（通过 LRU Cache 缓存）
 *   4. 在 Block 内二分查找 key
 *
 * ★ 分层组织：
 *   sstables_ = vector<vector<shared_ptr<ObSSTable>>>
 *   sstables_[0] = Level 0 的所有 SSTable
 *   sstables_[1] = Level 1 的所有 SSTable
 *   ...
 *
 * 💡 提问：为什么 Level 0 的 SSTable 之间 key 范围可能重叠，
 *   但 Level 1+ 的 SSTable 之间 key 范围不重叠？
 *   （提示：想想每个 Level 的 SSTable 是怎么产生的）
 */

// ---------------------------------------------------------------------------
// 4. Block 格式
// ---------------------------------------------------------------------------
/**
 * ★ Block 是 SSTable 内部的数据单元，存储一批有序的 KV entry。
 *
 *  Block 格式（ascii 图来自 ob_block.h 源码注释）：
 *
 *      ┌─────────────────┐
 *      │    entry 1      │◄───┐
 *      ├─────────────────┤    │
 *      │    entry 2      │    │
 *      ├─────────────────┤    │  entry = key + value 的编码
 *      │      ..         │    │
 *      ├─────────────────┤    │
 *      │    entry n      │◄─┐ │
 *      ├─────────────────┤  │ │
 * ┌───►│  offset size(n) │  │ │  offset 数组的大小
 * │    ├─────────────────┤  │ │
 * │    │    offset 1     ├──┼─┘  每个 offset 指向一个 entry 的起始位置
 * │    ├─────────────────┤  │     通过 offset 可以实现 O(1) 随机访问
 * │    │      ..         │  │
 * │    ├─────────────────┤  │
 * │    │    offset n     ├──┘
 * │    ├─────────────────┤
 * └────┤  offset start   │ ← Block 末尾：offset 数组的起始位置
 *      └─────────────────┘
 *
 * ★ 二分查找过程：
 *   1. 读 Block 最后 4 字节 → 得到 offset 数组的起始位置
 *   2. 读 offset 数组的大小
 *   3. 在 offset 数组上二分查找 → 定位目标 entry
 *   4. 从 entry 中解析出 key 和 value
 *
 * ★ Block 大小：默认 4KB（对齐到操作系统页面大小）
 *
 * 💡 提问：为什么 offset 数组放在数据后面而不是前面？
 *   （提示：BlockBuilder 写入数据时可以不知道 offset 数组的最终大小，
 *          所有 entry 写完后才能确定 offset 数组的内容）
 */

// ---------------------------------------------------------------------------
// 5. WAL — Write-Ahead Log
// ---------------------------------------------------------------------------
/**
 * ★ WAL 是保证持久性的关键机制。
 *
 *   写入流程：
 *     put(key, value)
 *       → WAL::append(key, value, seq)  先写日志到磁盘
 *       → mem_table_->put(seq, key, value)  再写内存表
 *
 *   崩溃恢复：
 *     重启 → recover() → recover_from_wal()
 *       → 读取 WAL 文件 → 重放所有未 flush 的写入到 MemTable
 *       → MemTable 满 → flush 到 SSTable
 *
 * ★ 设计要点：
 *   - 每个 MemTable 有独立的 WAL 文件
 *   - MemTable flush 成 SSTable 后，对应的 WAL 可以删除
 *   - options_.force_sync_new_log 控制是否每次写入都 fsync（性能 vs 安全性）
 *
 * 💡 提问：如果 force_sync_new_log = false（不每次 fsync），
 *   崩溃时最多丢失多少数据？这是可接受的吗？
 *   （提示：OS page cache 的刷新间隔通常是几十秒）
 */

// ---------------------------------------------------------------------------
// 6. Compaction — 合并压缩
// ---------------------------------------------------------------------------
/**
 * ★ Compaction 是 LSM-Tree 的性能核心，解决"读放大"问题。
 *
 *   问题：随着写入增多，SSTable 越来越多。
 *         读取一个 key 可能要查 MemTable + Immutable MemTables + N 个 SSTable。
 *         这就是"读放大"（Read Amplification）。
 *
 *   解决：定期把多个小 SSTable 合并成大的有序 SSTable。
 *         合并过程中丢弃旧版本和墓碑。
 *
 * ★ MiniOB 的 Compaction 策略（Leveled Compaction）：
 *
 *   try_freeze_memtable():
 *     当 MemTable 大小超过阈值 → 冻结当前 MemTable → 创建新 MemTable
 *     → 通知后台线程做 compaction
 *
 *   background_compaction():
 *     build_sstable(imem): 把 Immutable MemTable 转成 SSTable（Level 0）
 *     do_compaction(): 选择 Level N 的 SSTable 和 Level N+1 有重叠的 SSTable
 *                      → 归并排序 → 生成新的 Level N+1 SSTable
 *
 *   try_major_compaction():
 *     把所有 Level 合并为一个 Level（全量合并，开销大但彻底）
 *
 * ★ 核心算法：归并排序（k-way merge）
 *   N 个 SSTable × M 个 iterator → MergeIterator
 *   每次 next() 从 N×M 个 iterator 中选 key 最小的 → 这就是归并排序的 merge 步
 *
 * 💡 提问：Compaction 会阻塞写入吗？
 *   MiniOB 的 compaction 在后台线程执行（executor_），
 *   写入只需要操作 MemTable（内存中的 SkipList），不阻塞。
 *   但如果 MemTable 冻结速度 > Compaction 速度，会发生什么？
 *   （提示：这就是 LevelDB/RocksDB 的 write stall 问题）
 *
 * 💡 提问：为什么 Compaction 不直接在 MemTable 上做，而是先转成 SSTable？
 *   （提示：考虑 MemTable 和 SSTable 的数据结构差异，
 *          SkipList 适合内存读写，不适合磁盘归并）
 */

// ---------------------------------------------------------------------------
// 7. Manifest — 元数据持久化
// ---------------------------------------------------------------------------
/**
 * ★ Manifest 记录 SSTable 集合的变更历史。
 *
 *   内容：每次 Compaction 后写一条记录：
 *     - 新增了哪些 SSTable
 *     - 删除了哪些 SSTable
 *
 *   用途：崩溃恢复时，通过 Manifest 重建 sstables_ 列表。
 *         - 先加载 snapshot（完整状态快照）
 *         - 再重放 snapshot 之后的增量记录
 *
 * ★ 为什么需要 Manifest？
 *   SSTable 文件存在于磁盘上，但"哪些文件属于当前数据库"是元数据。
 *   如果只在内存中维护，崩溃后就不知道了。
 *
 * 💡 提问：Manifest 文件不断追加，会不会无限增长？
 *   （提示：snapshot 机制会定期做完整快照，旧的增量记录可以截断）
 */

// ---------------------------------------------------------------------------
// 8. LRU Cache — Block 缓存
// ---------------------------------------------------------------------------
/**
 * ★ Block 缓存的作用：减少磁盘 I/O。
 *
 *   SSTable 的 BlockMetas 已经在内存中（初始化时加载），
 *   但 Block 数据（实际的 key-value entries）按需从磁盘读取。
 *   读过的 Block 放入 LRU Cache，下次读取同一 Block 直接命中。
 *
 * ★ LRU（Least Recently Used）驱逐策略：
 *   缓存满时，淘汰"最久没有被访问"的 Block。
 *   用双向链表 + HashMap 实现 O(1) 的插入/查找/删除。
 *
 * 💡 提问：LRU 和 LFU（Least Frequently Used）有什么区别？
 *   什么场景下 LFU 比 LRU 更好？
 *   （提示：考虑热点数据和周期性扫描的访问模式）
 */

// ---------------------------------------------------------------------------
// 9. BloomFilter — 布隆过滤器
// ---------------------------------------------------------------------------
/**
 * ★ BloomFilter 快速判断 key 是否"可能存在"于某个 SSTable 中。
 *
 *   原理：用 K 个哈希函数映射到一个位图。
 *     - 插入 key：把 K 个位置都设为 1
 *     - 查询 key：检查 K 个位置是否都为 1
 *       - 都为 1 → "可能存在"（需进一步查证）
 *       - 有 0 → "一定不存在"（可以跳过这个 SSTable！）
 *
 *   效果：显著减少不必要的 SSTable 读取。
 *   代价：少量假阳性（说"可能存在"但实际没有），占位图内存。
 *
 * ★ 参数：位图大小 M 和哈希函数个数 K 决定假阳性率。
 *   通常假阳性率 1%（10 bits per key）或更低。
 *
 * 💡 提问：BloomFilter 能不能删除元素？为什么？
 *   如果不能删除，LSM-Tree 场景下怎么处理 SSTable 被合并删除后的 BloomFilter？
 *   （提示：SSTable 被删除 → 整个 BloomFilter 跟着 SSTable 一起删了）
 */

// ==========================================================================
// ★ ObLsmImpl 主类 — 一切的总管
// ==========================================================================

/**
 * ★ put/get/remove 的核心实现：
 *
 * put(key, value):
 *   1. WAL::append(key, value, seq)  — 先写日志
 *   2. mem_table_->put(seq, key, value)  — 写内存
 *   3. seq_++  — 递增序列号
 *   4. try_freeze_memtable()  — 检查是否需要冻结
 *
 * get(key):
 *   1. 查 mem_table_（当前活跃 MemTable）
 *   2. 查 imem_tables_（已冻结但未 Compaction 的 MemTable）
 *   3. 查 sstables_（从 Level 0 到 Level N 逐层）
 *      → 每层创建一个 TableIterator → seek(key)
 *      → 找到就返回
 *
 * remove(key):
 *   写入一条 key_type=Delete 的记录（墓碑），同 put 流程。
 *   实际删除在 Compaction 时发生（墓碑和老数据一起被丢弃）。
 *
 * ★ 序列号管理：
 *   seq_ 是全局递增的原子变量，每次 put/remove 时递增。
 *   这个序列号是 MVCC 的基础 — 同一 key 的多个版本按 seq 排序。
 *
 * 💡 提问：如果 seq_ 溢出了（64位用完）怎么办？
 *   64 位无符号整数每秒 100 万次写入可以用 58 万年。
 *   真实数据库通常加 epoch 机制来重置。
 */

// ==========================================================================
// ★ 学习路径建议
// ==========================================================================
/**
 * 1. ob_memtable.h/cpp + ob_skiplist.h
 *    → 理解内存表的工作原理，跳表实现
 *
 * 2. ob_block.h/cpp + ob_block_builder.cpp
 *    → 理解 Block 编码格式，这是最底层的数据单元
 *
 * 3. ob_sstable.h/cpp + ob_sstable_builder.cpp
 *    → 理解 SSTable 文件格式，Block 如何组成 SSTable
 *
 * 4. ob_lsm_impl.h/cpp
 *    → 理解 put/get 的完整流程，try_freeze_memtable
 *
 * 5. ob_compaction.h/cpp + ob_compaction_picker.cpp + ob_merger.cpp
 *    → 理解 Compaction 的选择策略和归并算法
 *
 * 6. ob_lsm_wal.h/cpp + ob_manifest.h/cpp
 *    → 理解持久性和崩溃恢复
 *
 * 7. ob_bloomfilter.h + ob_lru_cache.h
 *    → 理解读优化手段
 *
 * 📝 动手实验建议：
 *   - 在 ob_lsm_impl.cpp 的 get() 方法中加日志，观察每次 get 查了几个 SSTable
 *   - 修改 MemTable 大小阈值（options_.memtable_size），观察 freeze 频率变化
 *   - 尝试把 SkipList 换成红黑树（C++ std::map），比较性能差异
 */
