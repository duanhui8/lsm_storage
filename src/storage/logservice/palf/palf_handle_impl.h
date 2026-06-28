/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/palf_handle_impl.h */

#ifndef OCEANBASE_LOGSERVICE_PALF_HANDLE_IMPL_
#define OCEANBASE_LOGSERVICE_PALF_HANDLE_IMPL_
#include "palf_handle.h"
#include "log_storage.h"
#include "log_storage_interface.h"
#include "log_meta.h"
#include "log_cache.h"
#include "log_define.h"
#include "lsn.h"
#include <mutex>
#include <atomic>

namespace oceanbase {
namespace palf {

class LogIOWorker;

/**
 * IPalfHandleImpl — abstract interface for PalfHandleImpl.
 * Simplified from OB 4.4.2 palf_handle_impl.h:219-655.
 */
class IPalfHandleImpl {
public:
  IPalfHandleImpl() = default;
  virtual ~IPalfHandleImpl() = default;

  virtual int append(const PalfAppendOptions &opts, const void *buf, int64_t buf_len,
                     int64_t ref_scn, LSN &lsn, int64_t &scn) = 0;
  virtual int read(const LSN &lsn, int64_t read_size, ReadBuf &read_buf, int64_t &out_size) = 0;
  virtual int seek(const LSN &lsn) = 0;
  virtual int get_end_lsn(LSN &lsn) const = 0;
  virtual int get_max_lsn(LSN &lsn) const = 0;
  virtual int get_begin_lsn(LSN &lsn) const = 0;
  virtual int get_palf_id(int64_t &palf_id) const = 0;
  virtual int get_role(int64_t &role) const = 0;
  virtual int64_t get_epoch() const = 0;
};

/**
 * PalfHandleImpl — per-LS log stream instance.
 * Simplified from OB 4.4.2 palf_handle_impl.h:658-1367.
 *
 * Each tablet has one PalfHandleImpl, managing:
 *  - LogStorage (disk I/O)
 *  - LogCache (read cache)
 *  - LSN allocation (monotonically increasing)
 */
class PalfHandleImpl : public IPalfHandleImpl {
public:
  PalfHandleImpl();
  ~PalfHandleImpl();

  int init(int64_t palf_id, const char *log_dir, LogIOWorker *io_worker);

  int load(int64_t palf_id, const char *log_dir, LogIOWorker *io_worker);

  // === IPalfHandleImpl interface ===
  int append(const PalfAppendOptions &opts, const void *buf, int64_t buf_len,
             int64_t ref_scn, LSN &lsn, int64_t &scn) override;
  int read(const LSN &lsn, int64_t read_size, ReadBuf &read_buf, int64_t &out_size) override;
  int seek(const LSN &lsn) override;

  int get_end_lsn(LSN &lsn) const override;
  int get_max_lsn(LSN &lsn) const override;
  int get_begin_lsn(LSN &lsn) const override;
  int get_palf_id(int64_t &palf_id) const override;
  int get_role(int64_t &role) const override;
  int64_t get_epoch() const override { return epoch_; }

  int64_t get_my_palf_id() const { return palf_id_; }

private:
  int do_append_(const char *buf, int64_t buf_len, LSN &lsn);

  int64_t         palf_id_;
  int64_t         epoch_;
  std::string     log_dir_;
  LogStorage      log_storage_;
  LogMeta         log_meta_;
  LogCache        log_cache_;
  LogIOWorker    *io_worker_;
  std::mutex      mutex_;
  LSN             end_lsn_;        // tail of committed log
  LSN             max_lsn_;        // highest LSN allocated
  int64_t         log_id_;         // monotonically increasing log id
  int64_t         proposal_id_;    // leader proposal id
  int64_t         role_;           // 0=follower, 1=leader
  std::atomic<bool> is_inited_;
};

}  // namespace palf
}  // namespace oceanbase
#endif
