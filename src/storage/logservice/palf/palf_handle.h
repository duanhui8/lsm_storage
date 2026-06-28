/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/palf_handle.h */

#ifndef OCEANBASE_LOGSERVICE_PALF_HANDLE_
#define OCEANBASE_LOGSERVICE_PALF_HANDLE_
#include "palf_options.h"
#include "lsn.h"
#include "log_storage_interface.h"

namespace oceanbase {
namespace palf {

class IPalfHandleImpl;

/**
 * PalfHandle — RAII wrapper around IPalfHandleImpl.
 * Simplified from OB 4.4.2 palf_handle.h:60-614.
 */
class PalfHandle {
public:
  PalfHandle();
  ~PalfHandle();

  int init(IPalfHandleImpl *impl);
  void reset();

  // === Log operations ===
  int append(const PalfAppendOptions &opts, const void *buf, int64_t buf_len,
             int64_t ref_scn, LSN &lsn, int64_t &scn);
  int read(const LSN &lsn, int64_t read_size, ReadBuf &read_buf, int64_t &out_size);
  int seek(const LSN &lsn);

  // === State ===
  int get_end_lsn(LSN &lsn) const;
  int get_max_lsn(LSN &lsn) const;
  int get_begin_lsn(LSN &lsn) const;
  int64_t get_palf_id() const;

private:
  IPalfHandleImpl *impl_;
};

}  // namespace palf
}  // namespace oceanbase
#endif
