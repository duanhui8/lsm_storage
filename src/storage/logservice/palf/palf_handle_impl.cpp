/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/palf_handle_impl.h */

#include "palf_handle_impl.h"
#include "log_entry.h"
#include "log_io_worker.h"
#include <cstring>
#include <cstdio>

namespace oceanbase {
namespace palf {

PalfHandleImpl::PalfHandleImpl()
    : palf_id_(INVALID_PALF_ID), epoch_(0), io_worker_(nullptr),
      log_id_(FIRST_VALID_LOG_ID), proposal_id_(1), role_(1), is_inited_(false) {}

PalfHandleImpl::~PalfHandleImpl() {}

int PalfHandleImpl::init(int64_t palf_id, const char *log_dir, LogIOWorker *io_worker)
{
  if (is_inited_) return -1;
  palf_id_ = palf_id;
  log_dir_ = log_dir;
  io_worker_ = io_worker;
  epoch_ = 1;

  int ret = log_storage_.init(log_dir, PALF_BLOCK_SIZE);
  if (ret != 0) { return ret; }

  // Scan for existing data (recovery path)
  is_inited_ = true;

  // Check if there's existing data and scan to find end_lsn
  LSN scan_lsn = log_storage_.get_begin_lsn();
  char buf[4096];
  LSN last_valid_lsn = scan_lsn;
  while (true) {
    ReadBuf rbuf(buf, sizeof(buf));
    int64_t out_size = 0;
    ret = log_storage_.read(scan_lsn, sizeof(buf), rbuf, out_size);
    if (ret != 0 || out_size < static_cast<int64_t>(LogGroupEntryHeader::get_serialize_size())) break;

    LogGroupEntryHeader hdr;
    int64_t pos = 0;
    ret = hdr.deserialize(buf, out_size, pos);
    if (ret != 0 || !hdr.is_valid()) break;

    last_valid_lsn = hdr.committed_end_lsn_;
    if (last_valid_lsn.val_ <= scan_lsn.val_) break;
    scan_lsn = last_valid_lsn;
  }
  end_lsn_ = last_valid_lsn;
  max_lsn_ = last_valid_lsn;
  log_id_ = (last_valid_lsn.val_ > 0) ? 2 : FIRST_VALID_LOG_ID;

  return 0;
}

int PalfHandleImpl::load(int64_t palf_id, const char *log_dir, LogIOWorker *io_worker)
{
  // Load existing log from disk
  int ret = init(palf_id, log_dir, io_worker);
  if (ret != 0) return ret;

  // Scan log to find end_lsn_
  block_id_t min_id, max_id;
  log_storage_.get_block_id_range(min_id, max_id);

  if (min_id == max_id && min_id == 0 && log_storage_.get_begin_lsn().val_ == 0) {
    // Truly no data: start fresh
    end_lsn_ = LSN(0);
    max_lsn_ = LSN(0);
    return 0;
  }

  // Scan to find the actual end_lsn by reading group entries
  LSN scan_lsn = log_storage_.get_begin_lsn();
  char buf[4096];
  while (true) {
    ReadBuf rbuf(buf, sizeof(buf));
    int64_t out_size = 0;
    int ret = log_storage_.read(scan_lsn, sizeof(buf), rbuf, out_size);
    if (ret != 0 || out_size < LogGroupEntryHeader::get_serialize_size()) break;

    LogGroupEntryHeader hdr;
    int64_t pos = 0;
    ret = hdr.deserialize(buf, out_size, pos);
    if (ret != 0 || !hdr.is_valid()) break;

    scan_lsn = hdr.committed_end_lsn_;
    if (scan_lsn.val_ <= 0) break;
  }
  end_lsn_ = scan_lsn;
  max_lsn_ = scan_lsn;
  return 0;
}

int PalfHandleImpl::do_append_(const char *buf, int64_t buf_len, LSN &lsn)
{
  // 1. Allocate LSN
  lsn = max_lsn_;

  // 2. Build LogGroupEntryHeader
  LogGroupEntryHeader group_header;
  group_header.log_id_ = log_id_++;
  group_header.log_proposal_id_ = proposal_id_;
  group_header.prev_lsn_ = (end_lsn_.val_ > 0) ? end_lsn_ : LSN(0);
  group_header.committed_end_lsn_ = LSN(lsn.val_ + buf_len +
      LogGroupEntryHeader::get_serialize_size());
  group_header.group_entry_size_ = static_cast<uint32_t>(
      LogGroupEntryHeader::get_serialize_size() + buf_len);

  // 3. Write [LogGroupEntryHeader][payload] to log storage
  int64_t total_size = group_header.group_entry_size_;
  char *write_buf = static_cast<char *>(std::malloc(total_size));
  if (nullptr == write_buf) return -1;

  int64_t pos = 0;
  group_header.serialize(write_buf, total_size, pos);
  std::memcpy(write_buf + pos, buf, buf_len);

  int ret = log_storage_.append(lsn, write_buf, total_size);
  std::free(write_buf);
  if (ret != 0) return ret;

  // 4. Update state
  end_lsn_ = LSN(lsn.val_ + total_size);
  max_lsn_ = end_lsn_;

  return 0;
}

int PalfHandleImpl::append(const PalfAppendOptions &, const void *buf,
                            int64_t buf_len, int64_t, LSN &lsn, int64_t &scn)
{
  if (!is_inited_) return -1;
  std::lock_guard<std::mutex> lock(mutex_);
  int ret = do_append_(static_cast<const char *>(buf), buf_len, lsn);
  if (ret == 0) scn = log_id_ - 1; // Use log_id as simplified SCN
  return ret;
}

int PalfHandleImpl::read(const LSN &lsn, int64_t read_size,
                          ReadBuf &read_buf, int64_t &out_size)
{
  if (!is_inited_) return -1;
  // Try cache first
  if (log_cache_.get(lsn, read_size, read_buf.buf_, out_size) == 0) return 0;
  // Read from disk
  return log_storage_.read(lsn, read_size, read_buf, out_size);
}

int PalfHandleImpl::seek(const LSN &lsn)
{
  if (!is_inited_) return -1;
  // Reset the state to begin replay from lsn
  end_lsn_ = lsn;
  return 0;
}

int PalfHandleImpl::get_end_lsn(LSN &lsn) const
{
  lsn = end_lsn_;
  return 0;
}

int PalfHandleImpl::get_max_lsn(LSN &lsn) const
{
  lsn = max_lsn_;
  return 0;
}

int PalfHandleImpl::get_begin_lsn(LSN &lsn) const
{
  lsn = log_storage_.get_begin_lsn();
  return 0;
}

int PalfHandleImpl::get_palf_id(int64_t &palf_id) const
{
  palf_id = palf_id_;
  return 0;
}

int PalfHandleImpl::get_role(int64_t &role) const
{
  role = role_;
  return 0;
}

}  // namespace palf
}  // namespace oceanbase
