/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_meta_entry.h */

#include "log_meta_entry.h"

namespace oceanbase {
namespace palf {

LogMetaEntry::LogMetaEntry() { meta_.reset(); }
LogMetaEntry::~LogMetaEntry() {}

int LogMetaEntry::init(const LogMeta &meta)
{
  meta_ = meta;
  header_.meta_size_ = static_cast<uint32_t>(meta.get_serialize_size());
  return 0;
}

int LogMetaEntry::serialize(char *buf, int64_t buf_len, int64_t &pos) const
{
  int64_t total = get_serialize_size();
  if (buf_len - pos < total) return -1;
  // Write header (simple POD)
  uint32_t *pu32 = reinterpret_cast<uint32_t *>(buf + pos);
  *pu32++ = header_.magic_; *pu32++ = header_.meta_size_; *pu32++ = header_.checksum_;
  pos = sizeof(uint32_t) * 3;
  // Write meta body (member-wise)
  const LogConfigMeta &cm = meta_.config_meta_;
  int64_t *pi64 = reinterpret_cast<int64_t *>(buf + pos);
  *pi64++ = cm.proposal_id_; *pi64++ = cm.prev_lsn_.val_;
  *pi64++ = cm.prev_log_proposal_id_; *pi64++ = cm.prev_mode_pid_;
  *pi64++ = cm.config_version_.config_version_; *pi64++ = cm.config_version_.config_seq_;
  int32_t *pi32 = reinterpret_cast<int32_t *>(pi64);
  *pi32++ = cm.member_count_;
  pos = total;
  return 0;
}

int LogMetaEntry::deserialize(const char *buf, int64_t data_len, int64_t &pos, LogMeta &meta)
{
  if (data_len - pos < static_cast<int64_t>(sizeof(LogMetaEntryHeader))) return -1;
  const uint32_t *pu32 = reinterpret_cast<const uint32_t *>(buf + pos);
  header_.magic_ = *pu32++; header_.meta_size_ = *pu32++; header_.checksum_ = *pu32++;
  pos = sizeof(uint32_t) * 3;
  if (!header_.is_valid()) return -1;
  if (data_len - pos < static_cast<int64_t>(header_.meta_size_)) return -1;
  // Read meta body
  const int64_t *pi64 = reinterpret_cast<const int64_t *>(buf + pos);
  meta.config_meta_.proposal_id_ = *pi64++;
  meta.config_meta_.prev_lsn_.val_ = *pi64++;
  meta.config_meta_.prev_log_proposal_id_ = *pi64++;
  meta.config_meta_.prev_mode_pid_ = *pi64++;
  meta.config_meta_.config_version_.config_version_ = *pi64++;
  meta.config_meta_.config_version_.config_seq_ = *pi64++;
  const int32_t *pi32 = reinterpret_cast<const int32_t *>(pi64);
  meta.config_meta_.member_count_ = *pi32;
  pos = data_len; // skip rest
  return 0;
}

int64_t LogMetaEntry::get_serialize_size() const
{
  return sizeof(LogMetaEntryHeader) + header_.meta_size_;
}

}  // namespace palf
}  // namespace oceanbase
