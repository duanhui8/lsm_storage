/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_storage.cpp */

#include "log_storage.h"
#include "log_entry.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <sstream>

namespace oceanbase {
namespace palf {

LogStorage::LogStorage()
    : block_size_(PALF_BLOCK_SIZE), cur_fd_(-1), cur_block_id_(LOG_INVALID_BLOCK_ID),
      max_block_id_(LOG_INVALID_BLOCK_ID), min_block_id_(LOG_INVALID_BLOCK_ID), is_inited_(false) {}

LogStorage::~LogStorage() { destroy(); }

int LogStorage::init(const char *log_dir, int64_t block_size)
{
  if (is_inited_) return -1;
  log_dir_ = log_dir;
  block_size_ = block_size;
  // Ensure dir exists
  std::string cmd = "mkdir -p " + log_dir_;
  ::system(cmd.c_str());

  // Scan existing blocks
  min_block_id_ = LOG_INVALID_BLOCK_ID;
  max_block_id_ = LOG_INVALID_BLOCK_ID;
  for (block_id_t id = 0; ; id++) {
    std::string path = block_file_path_(id);
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
      if (min_block_id_ == LOG_INVALID_BLOCK_ID) min_block_id_ = id;
      max_block_id_ = id;
    } else {
      break; // No more blocks
    }
  }
  // If no blocks exist, set max to -1 so create_next_block_ starts at 0
  if (min_block_id_ == LOG_INVALID_BLOCK_ID) {
    min_block_id_ = 0;
    max_block_id_ = static_cast<block_id_t>(-1);
    int ret = create_next_block_();
    if (ret != 0) return ret;
    // Open the first block
    ret = open_block_(0);
    if (ret != 0) return ret;
  }
  is_inited_ = true;
  return 0;
}

void LogStorage::destroy()
{
  close_current_block_();
  is_inited_ = false;
}

std::string LogStorage::block_file_path_(block_id_t block_id) const
{
  std::ostringstream oss;
  oss << log_dir_ << "/" << block_id;
  return oss.str();
}

int LogStorage::open_block_(block_id_t block_id)
{
  close_current_block_();
  std::string path = block_file_path_(block_id);
  cur_fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, FILE_OPEN_MODE);
  if (cur_fd_ < 0) return -1;
  cur_block_id_ = block_id;
  return 0;
}

int LogStorage::close_current_block_()
{
  if (cur_fd_ >= 0) {
    ::fsync(cur_fd_);
    ::close(cur_fd_);
    cur_fd_ = -1;
  }
  cur_block_id_ = LOG_INVALID_BLOCK_ID;
  return 0;
}

int LogStorage::create_next_block_()
{
  block_id_t next_id = max_block_id_ + 1;
  std::string path = block_file_path_(next_id);
  int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, FILE_OPEN_MODE);
  if (fd < 0) return -1;
  // Pre-allocate block file
  if (::ftruncate(fd, PALF_PHY_BLOCK_SIZE) != 0) {
    ::close(fd);
    return -1;
  }
  ::close(fd);
  max_block_id_ = next_id;
  return 0;
}

int LogStorage::append(const LSN &lsn, const char *buf, int64_t buf_len)
{
  if (!is_inited_) return -1;
  if (buf_len <= 0) return 0;

  // Determine target block and offset
  block_id_t target_block = lsn_2_block(lsn, block_size_);
  offset_t   target_offset = lsn_2_offset(lsn, block_size_);

  // Ensure block file is open
  if (cur_block_id_ != target_block) {
    int ret = open_block_(target_block);
    if (ret != 0) return ret;
  }

  // Seek and write
  if (::lseek(cur_fd_, target_offset + MAX_INFO_BLOCK_SIZE, SEEK_SET) < 0) return -1;
  ssize_t written = ::write(cur_fd_, buf, buf_len);
  if (written < 0) return -1;
  if (written != buf_len) return -1;

  // Update max_block_id if needed
  if (target_block > max_block_id_) max_block_id_ = target_block;

  return 0;
}

int LogStorage::read(const LSN &lsn, int64_t in_read_size,
                     ReadBuf &read_buf, int64_t &out_read_size)
{
  if (!is_inited_) return -1;
  out_read_size = 0;

  block_id_t target_block = lsn_2_block(lsn, block_size_);
  offset_t   target_offset = lsn_2_offset(lsn, block_size_);

  std::string path = block_file_path_(target_block);
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) return -1;

  int64_t to_read = in_read_size;
  if (to_read > read_buf.buf_len_) to_read = read_buf.buf_len_;
  if (target_offset + to_read > static_cast<uint64_t>(block_size_)) to_read = block_size_ - target_offset;

  if (::lseek(fd, target_offset + MAX_INFO_BLOCK_SIZE, SEEK_SET) < 0) {
    ::close(fd); return -1;
  }
  ssize_t n = ::read(fd, read_buf.buf_, to_read);
  ::close(fd);
  if (n < 0) return -1;
  out_read_size = n;
  return 0;
}

int LogStorage::read_block_(block_id_t block_id, offset_t offset,
                            char *buf, int64_t buf_len, int64_t &out_read_size)
{
  std::string path = block_file_path_(block_id);
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) return -1;
  if (::lseek(fd, offset, SEEK_SET) < 0) { ::close(fd); return -1; }
  ssize_t n = ::read(fd, buf, buf_len);
  ::close(fd);
  if (n < 0) return -1;
  out_read_size = n;
  return 0;
}

int LogStorage::read_group_entry_header(const LSN &lsn, LogGroupEntryHeader &header)
{
  int64_t out_size = 0;
  ReadBuf read_buf(reinterpret_cast<char *>(&header), LogGroupEntryHeader::get_serialize_size());
  int ret = read(lsn, LogGroupEntryHeader::get_serialize_size(), read_buf, out_size);
  if (ret != 0) return ret;
  if (out_size < LogGroupEntryHeader::get_serialize_size()) return -1;
  return header.is_valid() ? 0 : -1;
}

int LogStorage::truncate(const LSN &lsn)
{
  // Truncate all data after the given LSN
  // For simplicity: truncate the block file
  block_id_t block = lsn_2_block(lsn, block_size_);
  offset_t   offset = lsn_2_offset(lsn, block_size_);

  std::string path = block_file_path_(block);
  int fd = ::open(path.c_str(), O_RDWR);
  if (fd < 0) return -1;
  if (::ftruncate(fd, offset + MAX_INFO_BLOCK_SIZE) != 0) {
    ::close(fd); return -1;
  }
  ::close(fd);

  // Remove files for higher block IDs
  for (block_id_t id = block + 1; id <= max_block_id_; id++) {
    ::unlink(block_file_path_(id).c_str());
  }
  max_block_id_ = block;
  return 0;
}

int LogStorage::truncate_prefix_blocks(const LSN &lsn)
{
  block_id_t block = lsn_2_block(lsn, block_size_);
  for (block_id_t id = min_block_id_; id < block; id++) {
    ::unlink(block_file_path_(id).c_str());
  }
  min_block_id_ = block;
  return 0;
}

const LSN LogStorage::get_begin_lsn() const
{
  return LSN(min_block_id_ * block_size_);
}

int LogStorage::get_block_id_range(block_id_t &min_id, block_id_t &max_id) const
{
  min_id = min_block_id_;
  max_id = max_block_id_;
  return 0;
}

}  // namespace palf
}  // namespace oceanbase
