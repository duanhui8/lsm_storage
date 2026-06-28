/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_storage.h */

#ifndef OCEANBASE_LOGSERVICE_LOG_STORAGE_INTERFACE_
#define OCEANBASE_LOGSERVICE_LOG_STORAGE_INTERFACE_
#include <cstdint>
#include "log_define.h"

namespace oceanbase {
namespace palf {

class ReadBuf {
public:
  ReadBuf() : buf_(nullptr), buf_len_(0) {}
  ReadBuf(char *buf, int64_t len) : buf_(buf), buf_len_(len) {}
  char *buf_;
  int64_t buf_len_;
};

class ILogStorage {
public:
  ILogStorage() = default;
  virtual ~ILogStorage() = default;

  virtual int append(const LSN &lsn, const char *buf, int64_t buf_len) = 0;
  virtual int read(const LSN &lsn, int64_t in_read_size,
                   ReadBuf &read_buf, int64_t &out_read_size) = 0;
  virtual int truncate(const LSN &lsn) = 0;
  virtual int truncate_prefix_blocks(const LSN &lsn) = 0;
  virtual const LSN get_begin_lsn() const = 0;
  virtual int get_block_id_range(block_id_t &min_block_id, block_id_t &max_block_id) const = 0;
};

}  // namespace palf
}  // namespace oceanbase
#endif
