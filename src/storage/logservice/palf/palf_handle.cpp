/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/palf_handle.h */

#include "palf_handle.h"
#include "palf_handle_impl.h"

namespace oceanbase {
namespace palf {

PalfHandle::PalfHandle() : impl_(nullptr) {}
PalfHandle::~PalfHandle() { reset(); }

int PalfHandle::init(IPalfHandleImpl *impl)
{
  impl_ = impl;
  return (impl_ != nullptr) ? 0 : -1;
}

void PalfHandle::reset() { impl_ = nullptr; }

int PalfHandle::append(const PalfAppendOptions &opts, const void *buf,
                        int64_t buf_len, int64_t ref_scn, LSN &lsn, int64_t &scn)
{
  if (nullptr == impl_) return -1;
  return impl_->append(opts, buf, buf_len, ref_scn, lsn, scn);
}

int PalfHandle::read(const LSN &lsn, int64_t read_size, ReadBuf &read_buf, int64_t &out_size)
{
  if (nullptr == impl_) return -1;
  return impl_->read(lsn, read_size, read_buf, out_size);
}

int PalfHandle::seek(const LSN &lsn)
{
  if (nullptr == impl_) return -1;
  return impl_->seek(lsn);
}

int PalfHandle::get_end_lsn(LSN &lsn) const
{
  if (nullptr == impl_) return -1;
  return impl_->get_end_lsn(lsn);
}

int PalfHandle::get_max_lsn(LSN &lsn) const
{
  if (nullptr == impl_) return -1;
  return impl_->get_max_lsn(lsn);
}

int PalfHandle::get_begin_lsn(LSN &lsn) const
{
  if (nullptr == impl_) return -1;
  return impl_->get_begin_lsn(lsn);
}

int64_t PalfHandle::get_palf_id() const
{
  if (nullptr == impl_) return INVALID_PALF_ID;
  int64_t id; impl_->get_palf_id(id);
  return id;
}

}  // namespace palf
}  // namespace oceanbase
