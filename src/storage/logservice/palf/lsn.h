/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/lsn.h */

#ifndef OCEANBASE_LOGSERVICE_LSN_
#define OCEANBASE_LOGSERVICE_LSN_
#include "log_define.h"

namespace oceanbase {
namespace palf {

struct LSN {
  LSN() : val_(PALF_INITIAL_LSN_VAL) {}
  explicit LSN(const offset_t offset) : val_(offset) {}
  LSN(const LSN &lsn) : val_(lsn.val_) {}
  ~LSN() {}

  bool is_valid() const { return val_ < LOG_INVALID_LSN_VAL; }
  void reset() { val_ = PALF_INITIAL_LSN_VAL; }

  friend LSN operator+(const LSN &lsn, const offset_t len)
  { LSN ret; ret.val_ = lsn.val_ + len; return ret; }
  friend LSN operator-(const LSN &lsn, const offset_t len)
  { LSN ret; ret.val_ = lsn.val_ - len; return ret; }
  friend offset_t operator-(const LSN &lhs, const LSN &rhs)
  { return lhs.val_ - rhs.val_; }
  friend bool operator==(const uint64_t offset, const LSN &lsn)
  { return lsn.val_ == offset; }

  bool operator==(const LSN &lsn) const { return val_ == lsn.val_; }
  bool operator!=(const LSN &lsn) const { return val_ != lsn.val_; }
  bool operator<(const LSN &lsn) const { return val_ < lsn.val_; }
  bool operator>(const LSN &lsn) const { return val_ > lsn.val_; }
  bool operator>=(const LSN &lsn) const { return val_ >= lsn.val_; }
  bool operator<=(const LSN &lsn) const { return val_ <= lsn.val_; }
  LSN &operator=(const LSN &lsn) { val_ = lsn.val_; return *this; }

  offset_t val_;
};

struct LSNCompare final {
  bool operator()(const LSN &left, const LSN &right) { return left > right; }
};

inline block_id_t lsn_2_block(const LSN &lsn, const uint64_t block_size)
{ return lsn.val_ / block_size; }

inline offset_t lsn_2_offset(const LSN &lsn, const uint64_t block_size)
{ return lsn.val_ % block_size; }

}  // namespace palf
}  // namespace oceanbase

#endif
