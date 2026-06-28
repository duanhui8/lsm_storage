/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/storage/slog/ob_storage_logger.h */

#include "storage/slog/ob_storage_logger.h"
#include "common/log/log.h"
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace oceanbase {
namespace storage {

ObStorageLogger::ObStorageLogger() = default;
ObStorageLogger::~ObStorageLogger() { destroy(); }

int ObStorageLogger::init(const char *log_dir) {
  if (is_inited_) return 0;
  slog_dir_ = log_dir;
  std::string cmd = "mkdir -p " + slog_dir_;
  ::system(cmd.c_str());
  slog_file_path_ = slog_dir_ + "/slog_0";
  fd_ = ::open(slog_file_path_.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
  if (fd_ < 0) return -1;
  is_inited_ = true;
  LOG_INFO("ObStorageLogger inited: %s", slog_file_path_.c_str());
  return 0;
}

void ObStorageLogger::destroy() {
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
  is_inited_ = false;
}

int ObStorageLogger::write_log(const ObStorageLogParam &param) {
  if (!is_inited_ || fd_ < 0) return -1;

  int32_t header_size = ObStorageLogEntry::get_header_size();
  int64_t entry_size = header_size + param.data_len_;
  char *buf = static_cast<char *>(std::malloc(entry_size));
  auto *entry = reinterpret_cast<ObStorageLogEntry *>(buf);
  entry->magic_ = ObStorageLogEntry::MAGIC;
  entry->total_size_ = static_cast<uint32_t>(entry_size);
  entry->tenant_id_ = param.tenant_id_;
  entry->log_type_ = param.log_type_;
  entry->data_len_ = static_cast<int32_t>(param.data_len_);
  if (param.data_len_ > 0 && param.data_ != nullptr) {
    std::memcpy(entry->data_, param.data_, param.data_len_);
  }

  ssize_t written = ::write(fd_, buf, entry_size);
  ::fsync(fd_);
  std::free(buf);
  if (written != entry_size) { return -1; }

  LOG_INFO("SLOG write: type=%d, size=%ld", param.log_type_, entry_size);
  return 0;
}

int ObStorageLogger::replay(void *ctx, int (*callback)(void *, const ObStorageLogEntry *)) {
  int replay_fd = ::open(slog_file_path_.c_str(), O_RDONLY);
  if (replay_fd < 0) {
    LOG_INFO("No SLOG file to replay: %s", slog_file_path_.c_str());
    return 0;
  }

  int recovered = 0;
  while (true) {
    ObStorageLogEntry hdr;
    ssize_t n = ::read(replay_fd, &hdr, ObStorageLogEntry::get_header_size());
    if (n != ObStorageLogEntry::get_header_size()) break;
    if (hdr.magic_ != ObStorageLogEntry::MAGIC) { LOG_WARN("SLOG magic mismatch"); break; }

    int32_t data_len = hdr.data_len_;
    char *data = static_cast<char *>(std::malloc(data_len));
    n = ::read(replay_fd, data, data_len);
    if (n != data_len) { std::free(data); break; }

    // Build full entry for callback
    int32_t full_size = ObStorageLogEntry::get_header_size() + data_len;
    char *full = static_cast<char *>(std::malloc(full_size));
    std::memcpy(full, &hdr, ObStorageLogEntry::get_header_size());
    std::memcpy(full + ObStorageLogEntry::get_header_size(), data, data_len);
    auto *entry = reinterpret_cast<ObStorageLogEntry *>(full);

    if (callback != nullptr) {
      callback(ctx, entry);
    }
    recovered++;
    std::free(data);
    std::free(full);
  }
  ::close(replay_fd);
  LOG_INFO("SLOG replay done, recovered %d entries", recovered);
  return recovered;
}

}  // namespace storage
}  // namespace oceanbase
