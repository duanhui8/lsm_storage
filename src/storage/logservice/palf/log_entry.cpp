/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_entry.h
           /opt/oceanbase4.4.2/src/logservice/palf/log_entry_header.h */

#include "log_entry.h"
#include <cstdlib>

namespace oceanbase {
namespace palf {

// ==================== LogEntryHeader ========================

LogEntryHeader::LogEntryHeader()
    : magic_(MAGIC), version_(1), data_len_(0), data_checksum_(0) {}

void LogEntryHeader::reset()
{
  magic_ = MAGIC; version_ = 1; data_len_ = 0; data_checksum_ = 0;
}

bool LogEntryHeader::is_valid() const { return magic_ == MAGIC; }

int LogEntryHeader::serialize(char *buf, int64_t buf_len, int64_t &pos) const
{
  int64_t sz = get_serialize_size();
  if (buf_len - pos < sz) return -1;
  std::memcpy(buf + pos, this, sz);
  pos += sz;
  return 0;
}

int LogEntryHeader::deserialize(const char *buf, int64_t data_len, int64_t &pos)
{
  int64_t sz = get_serialize_size();
  if (data_len - pos < sz) return -1;
  std::memcpy(this, buf + pos, sz);
  pos += sz;
  return 0;
}

int64_t LogEntryHeader::get_serialize_size() { return sizeof(LogEntryHeader); }

// ==================== LogGroupEntryHeader ========================

LogGroupEntryHeader::LogGroupEntryHeader()
    : magic_(MAGIC), version_(1), group_entry_size_(0),
      log_id_(0), log_proposal_id_(1), prev_lsn_(),
      committed_end_lsn_(), data_checksum_(0) {}

void LogGroupEntryHeader::reset()
{
  magic_ = MAGIC; version_ = 1; group_entry_size_ = 0;
  log_id_ = 0; log_proposal_id_ = 1; prev_lsn_.reset();
  committed_end_lsn_.reset(); data_checksum_ = 0;
}

bool LogGroupEntryHeader::is_valid() const { return magic_ == MAGIC; }

int LogGroupEntryHeader::serialize(char *buf, int64_t buf_len, int64_t &pos) const
{
  int64_t sz = get_serialize_size();
  if (buf_len - pos < sz) return -1;
  // Use field-by-field copy to avoid -Wclass-memaccess with non-trivial LSN
  uint16_t *pu16 = reinterpret_cast<uint16_t *>(buf + pos);
  *pu16++ = magic_; *pu16++ = version_;
  uint32_t *pu32 = reinterpret_cast<uint32_t *>(pu16);
  *pu32++ = group_entry_size_;
  int64_t *pi64 = reinterpret_cast<int64_t *>(pu32);
  *pi64++ = log_id_; *pi64++ = log_proposal_id_;
  uint64_t *pu = reinterpret_cast<uint64_t *>(pi64);
  *pu++ = prev_lsn_.val_; *pu++ = committed_end_lsn_.val_;
  *pu32 = data_checksum_;
  pos += sz;
  return 0;
}

int LogGroupEntryHeader::deserialize(const char *buf, int64_t data_len, int64_t &pos)
{
  int64_t sz = get_serialize_size();
  if (data_len - pos < sz) return -1;
  const uint16_t *pu16 = reinterpret_cast<const uint16_t *>(buf + pos);
  magic_ = *pu16++; version_ = *pu16++;
  const uint32_t *pu32 = reinterpret_cast<const uint32_t *>(pu16);
  group_entry_size_ = *pu32++;
  const int64_t *pi64 = reinterpret_cast<const int64_t *>(pu32);
  log_id_ = *pi64++; log_proposal_id_ = *pi64++;
  const uint64_t *pu = reinterpret_cast<const uint64_t *>(pi64);
  prev_lsn_.val_ = *pu++; committed_end_lsn_.val_ = *pu++;
  data_checksum_ = *(reinterpret_cast<const uint32_t *>(pu));
  pos += sz;
  return 0;
}

int64_t LogGroupEntryHeader::get_serialize_size() { return sizeof(LogGroupEntryHeader); }

// ==================== LogEntry ========================

LogEntry::LogEntry() : buf_(nullptr), buf_len_(0) {}

LogEntry::~LogEntry() { reset(); }

int LogEntry::init(const char *data, int64_t data_len)
{
  reset();
  buf_ = static_cast<char *>(std::malloc(data_len));
  if (nullptr == buf_) return -1;
  std::memcpy(buf_, data, data_len);
  buf_len_ = data_len;
  header_.data_len_ = static_cast<uint32_t>(data_len);
  return 0;
}

void LogEntry::reset()
{
  if (buf_ != nullptr) { std::free(buf_); buf_ = nullptr; }
  buf_len_ = 0;
  header_.reset();
}

int LogEntry::serialize(char *buf, int64_t buf_len, int64_t &pos) const
{
  int ret = header_.serialize(buf, buf_len, pos);
  if (ret != 0) return ret;
  if (buf_len - pos < buf_len_) return -1;
  std::memcpy(buf + pos, buf_, buf_len_);
  pos += buf_len_;
  return 0;
}

int LogEntry::deserialize(const char *buf, int64_t data_len, int64_t &pos)
{
  int ret = header_.deserialize(buf, data_len, pos);
  if (ret != 0) return ret;
  int64_t payload_len = header_.data_len_;
  if (data_len - pos < payload_len) return -1;
  return init(buf + pos, payload_len);
}

int64_t LogEntry::get_serialize_size() const
{
  return LogEntryHeader::get_serialize_size() + buf_len_;
}

}  // namespace palf
}  // namespace oceanbase
