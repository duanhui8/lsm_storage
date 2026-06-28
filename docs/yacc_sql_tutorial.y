/**
 * ==========================================================================
 * 【核心学习文件】yacc_sql.y — SQL 语法规则定义（bison 语法文件）
 * ==========================================================================
 *
 * ★ 这个文件是整个 SQL 解析的核心！
 *
 * bison 文件的三个部分（用 %% 分隔）：
 *
 *   %{ C代码块 %}   — 声明、头文件、辅助函数（原样拷贝到生成的 .cpp 文件）
 *   %token/%type/%left — bison 声明：Token定义、类型绑定、优先级
 *   %%              — 分隔符
 *   语法规则        — BNF 范式定义每种 SQL 语句的结构
 *   %%              — 分隔符
 *   用户代码        — parse 入口函数
 *
 * ★ flex（词法）和 bison（语法）怎么协作：
 *
 *   用户输入: "SELECT * FROM t1 WHERE id > 10"
 *     ↓
 *   flex (lex_sql.l) 把字符串切成 Token：
 *     SELECT → token:SELECT   * → token:'*' (StarExpr)
 *     FROM   → token:FROM     t1 → token:ID("t1")
 *     WHERE  → token:WHERE    id → token:ID("id")
 *     >      → token:GT       10 → token:NUMBER(10)
 *     ↓
 *   bison (yacc_sql.y) 按语法规则组合 Token → AST：
 *     select_stmt → SELECT expression_list FROM rel_list where group_by
 *     → ParsedSqlNode { flag=SCF_SELECT, selection={...} }
 *
 * ★ 学习建议：
 *   1. 先看 tokens 声明（第80-120行），了解 MiniOB 支持哪些关键字
 *   2. 看 select_stmt 规则（第486行），理解 SELECT 语法结构
 *   3. 看 expression 规则，理解算术表达式怎么处理优先级
 *   4. 尝试加一个新语句：比如 SHOW DATABASES
 *      需要：加 token → 加语句类型 → 加语法规则 → 加 Stmt 类
 *
 * 💡 提问：为什么 MiniOB 自己写 .y 文件而不是用 ANTLR 或其它 parser generator？
 *    （提示：对比 bison 和 ANTLR 的依赖、生成代码大小、调试难度）
 * ==========================================================================
 */

%{

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/log/log.h"
#include "common/lang/string.h"
#include "sql/parser/parse_defs.h"
#include "sql/parser/yacc_sql.hpp"
#include "sql/parser/lex_sql.h"
#include "sql/expr/expression.h"

using namespace std;

/**
 * ★ 从 SQL 字符串中提取 Token 的名称（用于调试和 AST 节点命名）
 * @param sql_string 原始 SQL
 * @param llocp 位置信息（bison 自动追踪）
 */
string token_name(const char *sql_string, YYLTYPE *llocp)
{
  return string(sql_string + llocp->first_column,
                llocp->last_column - llocp->first_column + 1);
}

/**
 * ★ 语法错误回调
 * 当 SQL 语法不符合任何规则时，bison 调用此函数。
 * 生成一个 SCF_ERROR 节点，包含错误消息和位置信息。
 *
 * 💡 提问：这里的错误消息是 bison 自动生成的，比如 "syntax error, unexpected X"。
 *    如何改进错误提示，让用户知道"这个位置应该写什么"？
 */
int yyerror(YYLTYPE *llocp, const char *sql_string, ParsedSqlResult *sql_result,
            yyscan_t scanner, const char *msg)
{
  unique_ptr<ParsedSqlNode> error_sql_node = make_unique<ParsedSqlNode>(SCF_ERROR);
  error_sql_node->error.error_msg = msg;
  error_sql_node->error.line = llocp->first_line;
  error_sql_node->error.column = llocp->first_column;
  sql_result->add_sql_node(std::move(error_sql_node));
  return 0;
}

ArithmeticExpr *create_arithmetic_expression(ArithmeticExpr::Type type,
                                             Expression *left,
                                             Expression *right,
                                             const char *sql_string,
                                             YYLTYPE *llocp)
{
  ArithmeticExpr *expr = new ArithmeticExpr(type, left, right);
  expr->set_name(token_name(sql_string, llocp));
  return expr;
}

UnboundAggregateExpr *create_aggregate_expression(const char *aggregate_name,
                                           Expression *child,
                                           const char *sql_string,
                                           YYLTYPE *llocp)
{
  UnboundAggregateExpr *expr = new UnboundAggregateExpr(aggregate_name, child);
  expr->set_name(token_name(sql_string, llocp));
  return expr;
}

%}

/* ★ bison 配置选项 */
%define api.pure full        /* 纯解析器（可重入，不使用全局变量） */
%define parse.error verbose  /* 详细的错误消息 */
%locations                   /* 启用位置追踪（@$ @1 @2 等） */
%lex-param { yyscan_t scanner }  /* 传递给词法分析器的额外参数 */
%parse-param { const char * sql_string }     /* 传递给 yyparse 的参数 */
%parse-param { ParsedSqlResult * sql_result }
%parse-param { void * scanner }

// ==================== Token 声明 ====================
// ★ 每个 SQL 关键字、标点、运算符都需要声明为 Token
// 这些 Token 在 lex_sql.l 中被识别和返回

%token  SEMICOLON
        BY
        CREATE
        DROP
        GROUP
        TABLE
        TABLES
        INDEX
        CALC
        SELECT
        DESC
        SHOW
        SYNC
        INSERT
        DELETE
        UPDATE
        LBRACE
        RBRACE
        COMMA
        TRX_BEGIN
        TRX_COMMIT
        TRX_ROLLBACK
        INT_T
        STRING_T
        FLOAT_T
        VECTOR_T
        HELP
        EXIT
        DOT
        INTO
        VALUES
        FROM
        WHERE
        AND
        SET
        ON
        LOAD
        DATA
        INFILE
        EXPLAIN
        STORAGE
        FORMAT
        PRIMARY
        KEY
        ANALYZE
        FIELDS
        TERMINATED
        ENCLOSED
        EQ
        LT
        GT
        LE
        GE
        NE

/**
 * ★ %union: 定义语法符号的值类型（C 联合体）
 *
 * 每个终结符和非终结符都可以携带一个值，值的类型由 %type 指定。
 * 在语法动作中通过 $$（结果）、$1、$2...（子元素）引用。
 *
 * ★ 限制：不能有非 POD 类型（如 string, unique_ptr 等），
 *    所以指针类型都手动管理（new/delete），并配合 %destructor 自动清理。
 */
%union {
  ParsedSqlNode *                            sql_node;
  ConditionSqlNode *                         condition;
  Value *                                    value;
  enum CompOp                                comp;
  RelAttrSqlNode *                           rel_attr;
  vector<AttrInfoSqlNode> *                  attr_infos;
  AttrInfoSqlNode *                          attr_info;
  Expression *                               expression;
  vector<unique_ptr<Expression>> *           expression_list;
  vector<Value> *                            value_list;
  vector<ConditionSqlNode> *                 condition_list;
  vector<RelAttrSqlNode> *                   rel_attr_list;
  vector<string> *                           relation_list;
  vector<string> *                           key_list;
  char *                                     cstring;
  int                                        number;
  float                                      floats;
}

/**
 * ★ %destructor: 错误恢复时的析构函数
 * 当语法解析中途失败时，bison 会自动调用这些析构函数来释放已分配的内存，
 * 防止内存泄漏。
 */
%destructor { delete $$; } <condition>
%destructor { delete $$; } <value>
%destructor { delete $$; } <rel_attr>
%destructor { delete $$; } <attr_infos>
%destructor { delete $$; } <expression>
%destructor { delete $$; } <expression_list>
%destructor { delete $$; } <value_list>
%destructor { delete $$; } <condition_list>
%destructor { delete $$; } <relation_list>
%destructor { delete $$; } <key_list>

/* ★ 终结符的类型绑定：把 Token 绑定到 union 的成员类型 */
%token <number> NUMBER
%token <floats> FLOAT
%token <cstring> ID
%token <cstring> SSS  /* 单引号字符串（SQL string） */

/* ★ 非终结符的类型绑定：指定每个语法规则返回值的类型 */
%type <number>              type
%type <condition>           condition
%type <value>               value
%type <number>              number
%type <cstring>             relation
%type <comp>                comp_op
%type <rel_attr>            rel_attr
%type <attr_infos>          attr_def_list
%type <attr_info>           attr_def
%type <value_list>          value_list
%type <condition_list>      where
%type <condition_list>      condition_list
%type <cstring>             storage_format
%type <key_list>            primary_key
%type <key_list>            attr_list
%type <relation_list>       rel_list
%type <expression>          expression
%type <expression>          aggregate_expression
%type <expression_list>     expression_list
%type <expression_list>     group_by
%type <cstring>             fields_terminated_by
%type <cstring>             enclosed_by
%type <sql_node>            calc_stmt
%type <sql_node>            select_stmt
%type <sql_node>            insert_stmt
%type <sql_node>            update_stmt
%type <sql_node>            delete_stmt
%type <sql_node>            create_table_stmt
%type <sql_node>            drop_table_stmt
%type <sql_node>            analyze_table_stmt
%type <sql_node>            show_tables_stmt
%type <sql_node>            desc_table_stmt
%type <sql_node>            create_index_stmt
%type <sql_node>            drop_index_stmt
%type <sql_node>            sync_stmt
%type <sql_node>            begin_stmt
%type <sql_node>            commit_stmt
%type <sql_node>            rollback_stmt
%type <sql_node>            load_data_stmt
%type <sql_node>            explain_stmt
%type <sql_node>            set_variable_stmt
%type <sql_node>            help_stmt
%type <sql_node>            exit_stmt
%type <sql_node>            command_wrapper
%type <sql_node>            commands

/**
 * ★ 运算符优先级（从低到高）
 *
 * %left 表示左结合（a+b+c = (a+b)+c）
 * %right 表示右结合
 * 越靠下优先级越高
 *
 * 这解决了 "1+2*3" 的歧义：乘法优先级高 → 1+(2*3)
 * 也解决了 "1-2-3" 的歧义：左结合 → (1-2)-3
 *
 * 💡 提问：为什么没有定义比较运算符（>, <, =）的优先级？
 *    （提示：看 condition 规则中 cond_op 是怎么处理的，比较运算符和 WHERE 子句绑定）
 */
%left '+' '-'
%left '*' '/'
%right UMINUS  /* 一元负号（-x），伪 token，仅用于优先级 */
%%

/**
 * ==================================================================
 * ★ 语法规则部分 — 这是 BNF 范式的 SQL 语法定义
 * ==================================================================
 *
 * BNF 语法：
 *   rule_name:
 *       alternative1   { C 代码动作 }
 *     | alternative2   { C 代码动作 }
 *     ;
 *
 * 在动作代码中：
 *   $$ → 本规则的返回值
 *   $1 → 第一个符号的值（从左到右）
 *   $2 → 第二个符号的值
 *   ... 依此类推
 *
 * ★ MiniOB 支持的完整 SQL 语句列表（以 command_wrapper 为准）：
 *   select / insert / update / delete — DML
 *   create_table / drop_table — DDL
 *   create_index / drop_index — DDL
 *   show_tables / desc_table / analyze_table — 信息查询
 *   begin / commit / rollback — 事务控制
 *   load_data / explain / set / calc / help / exit / sync — 工具
 * ==================================================================
 */

/* ★ 解析入口：一条 SQL 命令（可选分号结尾） */
commands: command_wrapper opt_semicolon
  {
    unique_ptr<ParsedSqlNode> sql_node = unique_ptr<ParsedSqlNode>($1);
    sql_result->add_sql_node(std::move(sql_node));  /* 将解析结果加入列表 */
  }
  ;

/* ★ 所有支持的 SQL 语句类型 */
command_wrapper:
    calc_stmt
  | select_stmt
  | insert_stmt
  | update_stmt
  | delete_stmt
  | create_table_stmt
  | drop_table_stmt
  | analyze_table_stmt
  | show_tables_stmt
  | desc_table_stmt
  | create_index_stmt
  | drop_index_stmt
  | sync_stmt
  | begin_stmt
  | commit_stmt
  | rollback_stmt
  | load_data_stmt
  | explain_stmt
  | set_variable_stmt
  | help_stmt
  | exit_stmt
    ;

exit_stmt:
    EXIT {
      (void)yynerrs;  /* 消除未使用变量的编译警告 */
      $$ = new ParsedSqlNode(SCF_EXIT);
    };

help_stmt:
    HELP {
      $$ = new ParsedSqlNode(SCF_HELP);
    };

sync_stmt:
    SYNC {
      $$ = new ParsedSqlNode(SCF_SYNC);
    };

/* ... (中间省略部分语法规则，详见完整源码) ... */

/**
 * ==================================================================
 * ★★★ SELECT 语句的语法规则 ★★★
 * ==================================================================
 *
 * 语法：
 *   SELECT <列列表> FROM <表列表> [WHERE <条件>] [GROUP BY <表达式列表>]
 *
 * 例：SELECT id, name FROM t1 WHERE age > 20 GROUP BY city
 *
 * 解析过程：
 *   SELECT → 识别关键字
 *   expression_list → 解析 "id, name" → vector<Expression*>
 *   FROM → 识别关键字
 *   rel_list → 解析 "t1" → vector<string>（表名列表）
 *   where → 解析 "WHERE age > 20" → vector<ConditionSqlNode>
 *   group_by → 解析 "GROUP BY city" → vector<Expression*>
 *
 * ★ 每个子成分用 swap 把数据"移动"到 ParsedSqlNode 中，
 *   避免拷贝 large object。原来的 vector 通过 delete 释放。
 *
 * 💡 提问：如果 SELECT 后面写了 *，它是怎么处理的？
 *   看 expression 规则中的 '*' → StarExpr 分支
 */
select_stmt:
    SELECT expression_list FROM rel_list where group_by
    {
      $$ = new ParsedSqlNode(SCF_SELECT);
      if ($2 != nullptr) {
        $$->selection.expressions.swap(*$2);   /* ★ 列列表 */
        delete $2;
      }

      if ($4 != nullptr) {
        $$->selection.relations.swap(*$4);     /* ★ 表列表 */
        delete $4;
      }

      if ($5 != nullptr) {
        $$->selection.conditions.swap(*$5);    /* ★ WHERE 条件 */
        delete $5;
      }

      if ($6 != nullptr) {
        $$->selection.group_by.swap(*$6);      /* ★ GROUP BY */
        delete $6;
      }
    }
    ;

/**
 * ==================================================================
 * ★ INSERT 语句
 * ==================================================================
 *
 * 语法：INSERT INTO <表名> VALUES (<值列表>)
 * 例：INSERT INTO t1 VALUES (1, 'hello', 3.14)
 *
 * ★ 设计限制：不支持指定列名（如 INSERT INTO t1(id, name) VALUES(1, 'x')），
 *   必须按表定义的列顺序提供所有值。
 */
insert_stmt:
    INSERT INTO ID VALUES LBRACE value_list RBRACE
    {
      $$ = new ParsedSqlNode(SCF_INSERT);
      $$->insertion.relation_name = $3;
      $$->insertion.values.swap(*$6);
      delete $6;
    }
    ;

/**
 * ★ value_list: 逗号分隔的值列表（递归定义）
 *
 * 递归模式：value | value_list COMMA value
 * 这种方式产生"左递归"，bison 处理左递归比右递归更高效。
 *
 * 💡 提问：为什么 bison 推荐左递归而不是右递归？
 *   （提示：右递归会不断增加栈深度，左递归可以即时归约）
 */
value_list:
    value
    {
      $$ = new vector<Value>;
      $$->emplace_back(*$1);
      delete $1;
    }
    | value_list COMMA value {
      $$ = $1;
      $$->emplace_back(*$3);
      delete $3;
    }
    ;

value:
    NUMBER {
      $$ = new Value((int)$1);
      @$ = @1;
    }
    |FLOAT {
      $$ = new Value((float)$1);
      @$ = @1;
    }
    |SSS {
      char *tmp = common::substr($1,1,strlen($1)-2);  /* 去掉首尾的单引号 */
      $$ = new Value(tmp);
      free(tmp);
    }
    ;

/* ★ DELETE 语句：DELETE FROM <表名> [WHERE <条件>] */
delete_stmt:
    DELETE FROM ID where
    {
      $$ = new ParsedSqlNode(SCF_DELETE);
      $$->deletion.relation_name = $3;
      if ($4 != nullptr) {
        $$->deletion.conditions.swap(*$4);
        delete $4;
      }
    }
    ;

/* ★ UPDATE 语句：UPDATE <表名> SET <列名> = <值> [WHERE <条件>]
 * 限制：只支持 SET 单个列 */
update_stmt:
    UPDATE ID SET ID EQ value where
    {
      $$ = new ParsedSqlNode(SCF_UPDATE);
      $$->update.relation_name = $2;
      $$->update.attribute_name = $4;
      $$->update.value = *$6;
      if ($7 != nullptr) {
        $$->update.conditions.swap(*$7);
        delete $7;
      }
    }
    ;

/**
 * ==================================================================
 * ★ CREATE TABLE 语句（DDL 核心）
 * ==================================================================
 *
 * 语法：CREATE TABLE <表名> (<列定义列表>) [STORAGE FORMAT = <格式>]
 *
 * 例：CREATE TABLE t1 (id int, name char(20), age int)
 *
 * ★ 列定义 attr_def: 列名 + 类型
 *   支持 INT_T / STRING_T / FLOAT_T / VECTOR_T
 *
 * ★ STORAGE FORMAT: 可选的存储格式（如 "row" / "column" 等）
 * ==================================================================
 */
create_table_stmt:
    CREATE TABLE ID LBRACE attr_def attr_def_list RBRACE storage_format
    {
      $$ = new ParsedSqlNode(SCF_CREATE_TABLE);
      CreateTableSqlNode &create_table = $$->create_table;
      create_table.relation_name = $3;
      free($3);

      vector<AttrInfoSqlNode> *src_attrs = $6;

      if (src_attrs != nullptr) {
        create_table.attr_infos.swap(*src_attrs);
        delete src_attrs;
      }
      create_table.attr_infos.emplace_back(*$5);
      reverse(create_table.attr_infos.begin(), create_table.attr_infos.end());
      delete $5;
      if ($8 != nullptr) {
        create_table.storage_format = $8;
      }
    }
    ;

/**
 * ==================================================================
 * ★ expression（表达式）的语法规则
 * ==================================================================
 *
 * 表达式是 SQL 中最复杂的递归结构之一，支持：
 *   - 算术运算：+, -, *, /
 *   - 括号嵌套：(expression)
 *   - 一元负号：-x（%prec UMINUS 指定优先级）
 *   - 通配符：*（StarExpr，用于 SELECT *）
 *   - 字面量：NUMBER / FLOAT / STRING
 *   - 列引用：rel_attr（列名或 表名.列名）
 *   - 聚合函数：aggregate_expression（如 COUNT(x), SUM(x)）
 *
 * ★ 优先级由 %left 声明控制：
 *   '*' '/' 优先级 > '+' '-' 优先级
 *   所以 "1+2*3" 解析为 1+(2*3)，而不是 (1+2)*3
 *
 * 💡 提问：如果用户写 "SELECT * FROM t1 WHERE x + y > 10"，
 *   这里的 x+y 是怎么处理的？看 condition 规则中的 comp_op
 * ==================================================================
 */
expression:
    expression '+' expression {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::ADD, $1, $3, sql_string, &@$);
    }
    | expression '-' expression {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::SUB, $1, $3, sql_string, &@$);
    }
    | expression '*' expression {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::MUL, $1, $3, sql_string, &@$);
    }
    | expression '/' expression {
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::DIV, $1, $3, sql_string, &@$);
    }
    | LBRACE expression RBRACE {  /* 括号：提升优先级 */
      $$ = $2;
      $$->set_name(token_name(sql_string, &@$));
    }
    | '-' expression %prec UMINUS {  /* ★ 一元负号，%prec 指定使用 UMINUS 的优先级 */
      $$ = create_arithmetic_expression(ArithmeticExpr::Type::NEGATIVE, $2, nullptr, sql_string, &@$);
    }
    | '*' {
      $$ = new StarExpr();  /* ★ SELECT * 的通配符 */
    }
    | value {
      $$ = new ValueExpr(*$1);
      $$->set_name(token_name(sql_string, &@$));
      delete $1;
    }
    | rel_attr {  /* 列引用（列名 或 表名.列名） */
      RelAttrSqlNode *node = $1;
      $$ = new UnboundFieldExpr(node->relation_name, node->attribute_name);
      /* ★ "Unbound" — 此时还没绑定到具体的表和列，resolve 阶段才绑定 */
      $$->set_name(token_name(sql_string, &@$));
      delete $1;
    }
    | aggregate_expression {
      $$ = $1;
    }
    ;

/* ★ 聚合函数：ID(expression)，如 COUNT(*), SUM(x), AVG(y) */
aggregate_expression:
    ID LBRACE expression RBRACE {
      $$ = create_aggregate_expression($1, $3, sql_string, &@$);
    }
    ;

/* ★ 列引用：列名 或 表名.列名 */
rel_attr:
    ID {
      $$ = new RelAttrSqlNode;
      $$->attribute_name = $1;
    }
    | ID DOT ID {
      $$ = new RelAttrSqlNode;
      $$->relation_name  = $1;
      $$->attribute_name = $3;
    }
    ;

/**
 * ★ WHERE 子句
 *
 * WHERE <条件列表>
 * 条件列表支持 AND 连接多个条件（不支持 OR）
 *
 * 条件有三种形式：
 *   - 列 比较运算符 值    (如 id > 10)
 *   - 值 比较运算符 值    (如 10 > 5)
 *   - 列 比较运算符 列    (如 t1.id = t2.id, 用于 JOIN)
 *
 * 💡 提问：为什么 WHERE 条件用 vector 而不是树结构？
 *   如果条件是 (a>10 AND b<20) OR c=30，这个语法能正确解析吗？
 *   （提示：看 AND 有没有处理 OR 的分支）
 */
condition:
    rel_attr comp_op value
    {
      $$ = new ConditionSqlNode;
      $$->left_is_attr = 1;
      $$->left_attr = *$1;
      $$->right_is_attr = 0;
      $$->right_value = *$3;
      $$->comp = $2;
      delete $1;
      delete $3;
    }
    | value comp_op value
    {
      $$ = new ConditionSqlNode;
      $$->left_is_attr = 0;
      $$->left_value = *$1;
      $$->right_is_attr = 0;
      $$->right_value = *$3;
      $$->comp = $2;
      delete $1;
      delete $3;
    }
    | rel_attr comp_op rel_attr
    {
      $$ = new ConditionSqlNode;
      $$->left_is_attr = 1;
      $$->left_attr = *$1;
      $$->right_is_attr = 1;
      $$->right_attr = *$3;
      $$->comp = $2;
      delete $1;
      delete $3;
    }
    ;

/* ★ 比较运算符 */
comp_op:
      EQ { $$ = EQUAL_TO; }
    | LT { $$ = LESS_THAN; }
    | GT { $$ = GREAT_THAN; }
    | LE { $$ = LESS_EQUAL; }
    | GE { $$ = GREAT_EQUAL; }
    | NE { $$ = NOT_EQUAL; }
    ;

group_by:
    /* empty */
    {
      $$ = nullptr;
    }
    | GROUP BY expression_list
    {
      $$ = $3;
    }
    ;

explain_stmt:
    EXPLAIN command_wrapper
    {
      $$ = new ParsedSqlNode(SCF_EXPLAIN);
      $$->explain.sql_node = unique_ptr<ParsedSqlNode>($2);
    }
    ;

opt_semicolon: /*empty*/
    | SEMICOLON
    ;

%%
/* ==================================================================
 * ★ 用户代码部分
 * ==================================================================
 *
 * sql_parse() 是外部调用的入口：
 *   1. 初始化 flex 扫描器（yylex_init_extra）
 *   2. 设置输入字符串（scan_string）
 *   3. 调用 bison 解析器（yyparse）
 *   4. 清理资源
 *
 * ★ 使用流程：
 *   ParseStage::handle_request() → sql_parse(sql, &parsed_sql_result)
 *   内部调用 yylex (flex 生成) + yyparse (bison 生成)
 * ==================================================================
 */

extern void scan_string(const char *str, yyscan_t scanner);

int sql_parse(const char *s, ParsedSqlResult *sql_result) {
  yyscan_t scanner;
  std::vector<char *> allocated_strings;
  yylex_init_extra(static_cast<void*>(&allocated_strings), &scanner);
  scan_string(s, scanner);
  int result = yyparse(s, sql_result, scanner);

  for (char *ptr : allocated_strings) {
    free(ptr);
  }
  allocated_strings.clear();

  yylex_destroy(scanner);
  return result;
}
