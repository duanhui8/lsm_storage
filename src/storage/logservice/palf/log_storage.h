/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_storage.h */

#ifndef OCEANBASE_LOGSERVICE_LOG_STORAGE_
#define OCEANBASE_LOGSERVICE_LOG_STORAGE_
#include <string>
#include "log_storage_interface.h"
#include "log_define.h"
#include "lsn.h"
#include "log_entry.h"

namespace oceanbase {
namespace palf {

/**
 * LogStorage — block-based log file management.
 * Simplified from OB 4.4.2 log_storage.h:174-384.
 *
 * Manages a series of block files on disk (named by block_id: 0, 1, 2, ...).
 * Each block is PALF_BLOCK_SIZE bytes (64MB minus header space).
 * Supports append, read, and truncate operations.
 */
class LogStorage : public ILogStorage {
public:
  LogStorage();
  virtual ~LogStorage();

  int init(const char *log_dir, int64_t block_size = PALF_BLOCK_SIZE);
  void destroy();

  // === ILogStorage interface ===
  int append(const LSN &lsn, const char *buf, int64_t buf_len) override;
  int read(const LSN &lsn, int64_t in_read_size,
            ReadBuf &read_buf, int64_t &out_read_size) override;
  int truncate(const LSN &lsn) override;
  int truncate_prefix_blocks(const LSN &lsn) override;

  const LSN get_begin_lsn() const override;
  int get_block_id_range(block_id_t &min_block_id, block_id_t &max_block_id) const override;

  // === Additional operations ===
  int read_group_entry_header(const LSN &lsn, LogGroupEntryHeader &header);

  int64_t get_block_size() const { return block_size_; }
  const std::string &get_log_dir() const { return log_dir_; }

private:
  int open_block_(block_id_t block_id);
  int close_current_block_();
  int create_next_block_();
  int read_block_(block_id_t block_id, offset_t offset,
                  char *buf, int64_t buf_len, int64_t &out_read_size);

  std::string block_file_path_(block_id_t block_id) const;

  std::string  log_dir_;
  int64_t      block_size_;
  FileDesc     cur_fd_;
  block_id_t   cur_block_id_;
  block_id_t   max_block_id_;
  block_id_t   min_block_id_;
  bool         is_inited_;
};

}  // namespace palf
}  // namespace oceanbase
#endif
