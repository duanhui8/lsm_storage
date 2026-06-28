/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_meta_entry.h */

#ifndef OCEANBASE_LOGSERVICE_LOG_META_ENTRY_
#define OCEANBASE_LOGSERVICE_LOG_META_ENTRY_
#include "log_meta.h"

namespace oceanbase {
namespace palf {

/**
 * LogMetaEntry — serialized meta on disk.
 * Header + meta payload, written atomically.
 */
struct LogMetaEntryHeader {
  static const uint32_t MAGIC = 0x4D48; // "MH"
  uint32_t magic_;
  uint32_t meta_size_;   // size of body
  uint32_t checksum_;    // CRC32

  LogMetaEntryHeader() : magic_(MAGIC), meta_size_(0), checksum_(0) {}
  bool is_valid() const { return magic_ == MAGIC; }
};

class LogMetaEntry {
public:
  LogMetaEntry();
  ~LogMetaEntry();

  int init(const LogMeta &meta);
  int serialize(char *buf, int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, int64_t data_len, int64_t &pos, LogMeta &meta);
  int64_t get_serialize_size() const;

private:
  LogMetaEntryHeader header_;
  LogMeta meta_;
};

}  // namespace palf
}  // namespace oceanbase
#endif
