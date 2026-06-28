/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_entry.h
           /opt/oceanbase4.4.2/src/logservice/palf/log_entry_header.h */

#ifndef OCEANBASE_LOGSERVICE_LOG_ENTRY_
#define OCEANBASE_LOGSERVICE_LOG_ENTRY_
#include <cstdint>
#include <cstring>
#include "lsn.h"

namespace oceanbase {
namespace palf {

// ==================== LogEntryHeader ========================
// Each log entry inside a LogGroupEntry has this header.
// magic = 0x4C48 ("LH")
struct LogEntryHeader {
  static const uint16_t MAGIC = 0x4C48;

  uint16_t magic_;
  uint16_t version_;
  uint32_t data_len_;
  uint32_t data_checksum_;   // CRC32 over payload

  LogEntryHeader();
  void reset();
  bool is_valid() const;

  int serialize(char *buf, int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, int64_t data_len, int64_t &pos);
  static int64_t get_serialize_size();
};

// ==================== LogGroupEntryHeader ========================
// Group commit header: one per atomic write unit.
// magic = 0x4752 ("GR")
struct LogGroupEntryHeader {
  static const uint16_t MAGIC = 0x4752;

  uint16_t magic_;
  uint16_t version_;
  uint32_t group_entry_size_;   // total size including this header
  int64_t  log_id_;
  int64_t  log_proposal_id_;
  LSN      prev_lsn_;
  LSN      committed_end_lsn_;  // committed end LSN (single-node: set to current LSN)
  uint32_t data_checksum_;      // CRC32 over entire group entry body (after header)

  LogGroupEntryHeader();
  void reset();
  bool is_valid() const;

  int serialize(char *buf, int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, int64_t data_len, int64_t &pos);
  static int64_t get_serialize_size();
};

// ==================== LogEntry ========================
// A single log entry: header + payload (used within a LogGroupEntry).
class LogEntry {
public:
  LogEntry();
  ~LogEntry();

  int init(const char *data, int64_t data_len);
  void reset();

  const LogEntryHeader &get_header() const { return header_; }
  const char *get_buf() const { return buf_; }
  int64_t get_buf_len() const { return buf_len_; }

  int serialize(char *buf, int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;

private:
  LogEntryHeader header_;
  char *buf_;
  int64_t buf_len_;
};

}  // namespace palf
}  // namespace oceanbase
#endif
