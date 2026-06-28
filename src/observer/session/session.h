/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include "common/types.h"
#include "common/lang/string.h"
#include <cstdint>

#include "common/sys/rc.h"
// Minimal Trx stub — mystorage handles its own MVCC
class Trx {
public:
  Trx() = default;
  virtual ~Trx() = default;
  virtual RC start_if_need() { return RC::SUCCESS; }
  virtual RC commit() { return RC::SUCCESS; }
  virtual RC rollback() { return RC::SUCCESS; }
};

class SessionEvent;

class Session
{
public:
  static Session &default_session();

public:
  Session() = default;
  ~Session();

  const char *get_current_db_name() const { return current_db_name_.c_str(); }
  uint64_t    get_current_database_id() const { return current_database_id_; }

  void set_current_db(const string &dbname);

  void set_trx_multi_operation_mode(bool multi_operation_mode);
  bool is_trx_multi_operation_mode() const;

  Trx *current_trx();
  void set_current_trx(Trx *trx);

  // stubs for removed features
  bool get_execution_mode() const { return false; }
  bool used_chunk_mode() const { return false; }
  bool sql_debug_on() const { return false; }
  void set_current_request(void *) {}
  static void set_current_session(Session *) {}
  void set_sql_debug(bool) {}
  void set_execution_mode(ExecutionMode) {}
  void set_hash_join(bool) {}
  void set_use_cascade(bool) {}
  void destroy_trx() { current_trx_ = nullptr; }
  bool use_cascade() const { return false; }
  void set_used_chunk_mode(bool) {}

private:
  string   current_db_name_;
  uint64_t current_database_id_ = 0;
  Trx     *current_trx_ = nullptr;
  bool     trx_multi_operation_mode_ = false;
};
