#ifndef TABLE_H
#define TABLE_H

/*
 * table.h
 *
 * Author: Arthur Wei <arthur.wei@carbit.com.cn>
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    TABLE_NODE_TYPE_INVALID = -1, // 无效类型
    TABLE_NODE_TYPE_INT,     // 32 位整数类型
    TABLE_NODE_TYPE_DOUBLE,  // 双精度浮点类型
    TABLE_NODE_TYPE_STRING,  // 字符串类型（深拷贝）
    TABLE_NODE_TYPE_POINTER, // 指针类型（仅本地态，不参与序列化）
    TABLE_NODE_TYPE_TABLE    // 嵌套子表类型
} table_node_type_e;

typedef enum
{
    TABLE_SUCCESS = 0,
    TABLE_ERROR_NULL = -1,
    TABLE_ERROR_INVALID_PARAM = -2,
    TABLE_ERROR_MEMORY = -3,
    TABLE_ERROR_NO_MEMORY = TABLE_ERROR_MEMORY,
    TABLE_ERROR_KEY_EXISTS = -4,
    TABLE_ERROR_ALREADY_EXISTS = TABLE_ERROR_KEY_EXISTS,
    TABLE_ERROR_KEY_NOT_FOUND = -5,
    TABLE_ERROR_NOT_FOUND = TABLE_ERROR_KEY_NOT_FOUND,
    TABLE_ERROR_TYPE_MISMATCH = -6,
    TABLE_ERROR_HAS_NESTED = -7,
    TABLE_ERROR_NODE_NOT_IN_TABLE = -8,
    TABLE_ERROR_INVALID_TABLE_GRAPH = -9
} table_error_e;

typedef int64_t TB_ID;
typedef struct table table_t;
typedef struct table_node table_node_t;

// 便捷宏定义
#define TB_STR_ID(str) table_string_id(str)
#define TB_ID_STR(id) table_id_string(id)

// ==================== 基本操作接口 ====================

#define TABLE_ADD_INT(tb, key, value) table_add_int(tb, key, value)
#define TABLE_ADD_DOUBLE(tb, key, value) table_add_double(tb, key, value)
#define TABLE_ADD_STRING(tb, key, value) table_add_string(tb, key, value)
#define TABLE_ADD_POINTER(tb, key, value) table_add_pointer(tb, key, value)
#define TABLE_ADD_TABLE(tb, key, value) table_add_table(tb, key, value)

#define TABLE_GET_INT(tb, key) table_get_int(tb, key, 0)
#define TABLE_GET_DOUBLE(tb, key) table_get_double(tb, key, 0)
#define TABLE_GET_STRING(tb, key) table_get_string(tb, key, NULL)
#define TABLE_GET_POINTER(tb, key) table_get_pointer(tb, key, NULL)
#define TABLE_GET_TABLE(tb, key) table_get_table(tb, key, NULL)

#define TABLE_SET_INT(tb, key, value) table_set_int(tb, key, value)
#define TABLE_SET_DOUBLE(tb, key, value) table_set_double(tb, key, value)
#define TABLE_SET_STRING(tb, key, value) table_set_string(tb, key, value)
#define TABLE_SET_POINTER(tb, key, value) table_set_pointer(tb, key, value)
#define TABLE_SET_TABLE(tb, key, value) table_set_table(tb, key, value)

#define TABLE_NODE_GET_INT(node) table_node_get_int(node, 0)
#define TABLE_NODE_GET_DOUBLE(node) table_node_get_double(node, 0)
#define TABLE_NODE_GET_STRING(node) table_node_get_string(node, NULL)
#define TABLE_NODE_GET_POINTER(node) table_node_get_pointer(node, NULL)
#define TABLE_NODE_GET_TABLE(node) table_node_get_table(node, NULL)

#define TABLE_NODE_SET_INT(node, value) table_node_set_int(node, value)
#define TABLE_NODE_SET_DOUBLE(node, value) table_node_set_double(node, value)
#define TABLE_NODE_SET_STRING(node, value) table_node_set_string(node, value)
#define TABLE_NODE_SET_POINTER(node, value) table_node_set_pointer(node, value)
#define TABLE_NODE_SET_TABLE(node, value) table_node_set_table(node, value)

/* ================================================================================================== */
/**
 * @brief 创建一个新表
 * @return 新创建的table指针，失败返回 NULL
 */
table_t *table_create(void);

/**
 * @brief 复制表（深拷贝，包括所有嵌套子表）
 * @param tb 要复制的table指针
 * @return 新table指针（深拷贝），失败返回 NULL
 */
table_t *table_duplicate(table_t *tb);

/**
 * @brief 设置表名称
 * @param tb table指针
 * @param name 要设置的表名称
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL 或 TABLE_ERROR_MEMORY
 */
int32_t table_set_name(table_t *tb, const char *name);

/**
 * @brief 获取表名称
 * @param tb table指针
 * @return 表名称指针（无需释放），未设置返回 NULL
 */
const char *table_get_name(table_t *tb);

/**
 * @brief 获取表名称副本
 * @param tb table指针
 * @return 新分配的名称副本（需调用者 free），未设置或失败返回 NULL
 */
char *table_get_name_copy(table_t *tb);

/**
 * @brief 销毁表
 * @param tb 要销毁的table指针
 * @param recursive true：递归释放所有嵌套子表；false：如果存在嵌套子表则返回错误
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL 或 TABLE_ERROR_HAS_NESTED
 */
int32_t table_destroy(table_t *tb, bool recursive);

/**
 * @brief 向表中添加整数值
 * @param tb table指针
 * @param key 键
 * @param value 要添加的int值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_MEMORY 或 TABLE_ERROR_KEY_EXISTS
 */
int32_t table_add_int(table_t *tb, TB_ID key, int32_t value);

/**
 * @brief 向表中添加双精度值
 * @param tb table指针
 * @param key 键
 * @param value 要添加的double值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_MEMORY 或 TABLE_ERROR_KEY_EXISTS
 */
int32_t table_add_double(table_t *tb, TB_ID key, double value);

/**
 * @brief 向表中添加字符串值（深拷贝）
 * @param tb table指针
 * @param key 键
 * @param value 要添加的字符串值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_MEMORY 或 TABLE_ERROR_KEY_EXISTS
 */
int32_t table_add_string(table_t *tb, TB_ID key, const char *value);

/**
 * @brief 向表中添加指针值
 * @param tb table指针
 * @param key 键
 * @param value 要添加的指针值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_MEMORY 或 TABLE_ERROR_KEY_EXISTS
 */
int32_t table_add_pointer(table_t *tb, TB_ID key, void *value);

/**
 * @brief 向表中添加子表
 * @param tb table指针
 * @param key 键
 * @param value 要添加的table指针
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_MEMORY、TABLE_ERROR_KEY_EXISTS 或 TABLE_ERROR_INVALID_TABLE_GRAPH
 * @note 子表必须满足树结构约束：不能自引用、不能形成环、不能被多个父表共享
 */
int32_t table_add_table(table_t *tb, TB_ID key, table_t *value);

/**
 * @brief 设置int值（更新已存在的键）
 * @param tb table指针
 * @param key 键
 * @param value 要设置的int值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_KEY_NOT_FOUND 或 TABLE_ERROR_TYPE_MISMATCH
 */
int32_t table_set_int(table_t *tb, TB_ID key, int32_t value);

/**
 * @brief 设置double值（更新已存在的键）
 * @param tb table指针
 * @param key 键
 * @param value 要设置的double值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_KEY_NOT_FOUND 或 TABLE_ERROR_TYPE_MISMATCH
 */
int32_t table_set_double(table_t *tb, TB_ID key, double value);

/**
 * @brief 设置字符串值（更新已存在的键，深拷贝）
 * @param tb table指针
 * @param key 键
 * @param value 要设置的字符串值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_MEMORY、TABLE_ERROR_KEY_NOT_FOUND 或 TABLE_ERROR_TYPE_MISMATCH
 */
int32_t table_set_string(table_t *tb, TB_ID key, const char *value);

/**
 * @brief 设置指针值（更新已存在的键）
 * @param tb table指针
 * @param key 键
 * @param value 要设置的指针值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_KEY_NOT_FOUND 或 TABLE_ERROR_TYPE_MISMATCH
 */
int32_t table_set_pointer(table_t *tb, TB_ID key, void *value);

/**
 * @brief 设置子表值（更新已存在的键）
 * @param tb table指针
 * @param key 键
 * @param value 要设置的table指针
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_KEY_NOT_FOUND、TABLE_ERROR_TYPE_MISMATCH 或 TABLE_ERROR_INVALID_TABLE_GRAPH
 * @note 子表必须满足树结构约束：不能自引用、不能形成环、不能被多个父表共享
 */
int32_t table_set_table(table_t *tb, TB_ID key, table_t *value);

/**
 * @brief 通过点分隔路径添加整数值，缺失的中间子表会自动创建
 * @param tb table指针
 * @param path 点分隔路径，例如 "app_config.settings.width"
 * @param value 要添加的int值
 * @return TABLE_SUCCESS 表示成功；失败时返回对应的 TABLE_ERROR_* 错误码
 */
int32_t table_add_int_path(table_t *tb, const char *path, int32_t value);

/**
 * @brief 通过点分隔路径添加双精度值，缺失的中间子表会自动创建
 * @param tb table指针
 * @param path 点分隔路径，例如 "app_config.metrics.ratio"
 * @param value 要添加的double值
 * @return TABLE_SUCCESS 表示成功；失败时返回对应的 TABLE_ERROR_* 错误码
 */
int32_t table_add_double_path(table_t *tb, const char *path, double value);

/**
 * @brief 通过点分隔路径添加字符串值，缺失的中间子表会自动创建
 * @param tb table指针
 * @param path 点分隔路径，例如 "app_config.settings.theme"
 * @param value 要添加的字符串值
 * @return TABLE_SUCCESS 表示成功；失败时返回对应的 TABLE_ERROR_* 错误码
 */
int32_t table_add_string_path(table_t *tb, const char *path, const char *value);

/**
 * @brief 通过点分隔路径添加指针值，缺失的中间子表会自动创建
 * @param tb table指针
 * @param path 点分隔路径
 * @param value 要添加的指针值
 * @return TABLE_SUCCESS 表示成功；失败时返回对应的 TABLE_ERROR_* 错误码
 */
int32_t table_add_pointer_path(table_t *tb, const char *path, void *value);

/**
 * @brief 通过点分隔路径添加子表，缺失的中间子表会自动创建
 * @param tb table指针
 * @param path 点分隔路径
 * @param value 要添加的子table指针
 * @return TABLE_SUCCESS 表示成功；失败时返回对应的 TABLE_ERROR_* 错误码
 * @note 子表必须满足树结构约束：不能自引用、不能形成环、不能被多个父表共享
 */
int32_t table_add_table_path(table_t *tb, const char *path, table_t *value);

/**
 * @brief 获取int值
 * @param tb table指针
 * @param key 键
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际值，失败返回默认值
 */
int32_t table_get_int(table_t *tb, TB_ID key, int32_t default_value);

/**
 * @brief 获取double值
 * @param tb table指针
 * @param key 键
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际值，失败返回默认值
 */
double table_get_double(table_t *tb, TB_ID key, double default_value);

/**
 * @brief 获取字符串值
 * @param tb table指针
 * @param key 键
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际字符串，失败返回默认值
 * @note 返回内部字符串指针，不适合跨线程长期持有；如需稳定副本请使用table_get_string_copy
 */
const char *table_get_string(table_t *tb, TB_ID key, const char *default_value);

/**
 * @brief 获取字符串值副本
 * @param tb table指针
 * @param key 键
 * @return 成功返回新分配的字符串副本（需调用者free），失败返回NULL
 */
char *table_get_string_copy(table_t *tb, TB_ID key);

/**
 * @brief 获取指针值
 * @param tb table指针
 * @param key 键
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际指针，失败返回默认值
 */
void *table_get_pointer(table_t *tb, TB_ID key, void *default_value);

/**
 * @brief 获取子表值
 * @param tb table指针
 * @param key 键
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际子表，失败返回默认值
 * @note 返回内部子table指针，不适合跨线程长期持有
 */
table_t *table_get_table(table_t *tb, TB_ID key, table_t *default_value);

/**
 * @brief 获取子表副本
 * @param tb table指针
 * @param key 键
 * @return 成功返回深拷贝后的子表副本（需调用者 table_destroy），失败返回 NULL
 */
table_t *table_get_table_copy(table_t *tb, TB_ID key);

/**
 * @brief 检查点分隔路径是否存在
 * @param tb table指针
 * @param path 点分隔路径
 * @return true表示存在，false表示不存在或路径非法
 */
bool table_has_path(table_t *tb, const char *path);

/**
 * @brief 获取点分隔路径对应节点的类型
 * @param tb table指针
 * @param path 点分隔路径
 * @param default_type 获取失败时返回的默认类型
 * @return 成功返回实际类型，失败返回default_type
 */
table_node_type_e table_get_type_path(table_t *tb, const char *path, table_node_type_e default_type);

/**
 * @brief 通过点分隔路径设置整数值，缺失的中间子表会自动创建
 * @param tb table指针
 * @param path 点分隔路径，例如 "app_config.settings.width"
 * @param value 要设置的int值
 * @return TABLE_SUCCESS 表示成功；失败时返回对应的 TABLE_ERROR_* 错误码
 */
int32_t table_set_int_path(table_t *tb, const char *path, int32_t value);

/**
 * @brief 通过点分隔路径设置双精度值，缺失的中间子表会自动创建
 * @param tb table指针
 * @param path 点分隔路径
 * @param value 要设置的double值
 * @return TABLE_SUCCESS 表示成功；失败时返回对应的 TABLE_ERROR_* 错误码
 */
int32_t table_set_double_path(table_t *tb, const char *path, double value);

/**
 * @brief 通过点分隔路径设置字符串值，缺失的中间子表会自动创建
 * @param tb table指针
 * @param path 点分隔路径
 * @param value 要设置的字符串值
 * @return TABLE_SUCCESS 表示成功；失败时返回对应的 TABLE_ERROR_* 错误码
 */
int32_t table_set_string_path(table_t *tb, const char *path, const char *value);

/**
 * @brief 通过点分隔路径设置指针值，缺失的中间子表会自动创建
 * @param tb table指针
 * @param path 点分隔路径
 * @param value 要设置的指针值
 * @return TABLE_SUCCESS 表示成功；失败时返回对应的 TABLE_ERROR_* 错误码
 */
int32_t table_set_pointer_path(table_t *tb, const char *path, void *value);

/**
 * @brief 通过点分隔路径设置子表值，缺失的中间子表会自动创建
 * @param tb table指针
 * @param path 点分隔路径
 * @param value 要设置的子table指针
 * @return TABLE_SUCCESS 表示成功；失败时返回对应的 TABLE_ERROR_* 错误码
 * @note 子表必须满足树结构约束：不能自引用、不能形成环、不能被多个父表共享
 */
int32_t table_set_table_path(table_t *tb, const char *path, table_t *value);

/**
 * @brief 通过点分隔路径获取int值
 * @param tb table指针
 * @param path 点分隔路径，例如 "app_config.settings.width"
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际值，失败返回默认值
 */
int32_t table_get_int_path(table_t *tb, const char *path, int32_t default_value);

/**
 * @brief 通过点分隔路径获取double值
 * @param tb table指针
 * @param path 点分隔路径，例如 "app_config.settings.ratio"
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际值，失败返回默认值
 */
double table_get_double_path(table_t *tb, const char *path, double default_value);

/**
 * @brief 通过点分隔路径获取字符串值
 * @param tb table指针
 * @param path 点分隔路径，例如 "app_config.settings.theme"
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际字符串，失败返回默认值
 * @note 返回内部字符串指针，不适合跨线程长期持有；如需稳定副本请使用table_get_string_path_copy
 */
const char *table_get_string_path(table_t *tb, const char *path, const char *default_value);

/**
 * @brief 通过点分隔路径获取字符串值副本
 * @param tb table指针
 * @param path 点分隔路径，例如 "app_config.settings.theme"
 * @return 成功返回新分配的字符串副本（需调用者free），失败返回NULL
 */
char *table_get_string_path_copy(table_t *tb, const char *path);

/**
 * @brief 通过点分隔路径获取table值
 * @param tb table指针
 * @param path 点分隔路径，例如 "app_config.settings"
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际table，失败返回默认值
 * @note 返回内部table指针，不适合跨线程长期持有
 */
table_t *table_get_table_path(table_t *tb, const char *path, table_t *default_value);

/**
 * @brief 通过点分隔路径获取table值副本
 * @param tb table指针
 * @param path 点分隔路径，例如 "app_config.settings"
 * @return 成功返回深拷贝后的table副本（需调用者table_destroy），失败返回NULL
 */
table_t *table_get_table_path_copy(table_t *tb, const char *path);

/**
 * @brief 通过key_id删除table中的节点
 * @param tb table指针
 * @param key_id 要删除的节点的key_id
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL 或 TABLE_ERROR_KEY_NOT_FOUND
 */
int32_t table_delete_by_id(table_t *tb, TB_ID key_id);

/**
 * @brief 通过点分隔路径删除table中的节点
 * @param tb table指针
 * @param path 点分隔路径，例如 "app_config.settings.width"
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_INVALID_PARAM 或 TABLE_ERROR_KEY_NOT_FOUND
 */
int32_t table_delete_path(table_t *tb, const char *path);

/* ====================================================================================================================== */

/**
 * @brief 获取table中节点的数量
 * @param tb table指针
 * @return 当前table中存储的节点数量
 */
int32_t table_node_count(table_t *tb);

/**
 * @brief 通过下标获取节点（按插入顺序）
 * @param tb table指针
 * @param index 节点索引（0到count-1）
 * @return 节点指针，失败返回NULL（index越界）
 * @note 返回内部节点指针；节点级API属于低层接口，仅适合单线程、短生命周期、
 *       受控内部访问，不适合与delete/clear/destroy等并发修改场景混用
 */
table_node_t *table_index_node(table_t *tb, int32_t index);

/**
 * @brief 获取节点的数据类型
 * @param node 节点指针
 * @return 节点的数据类型
 * @note 节点级API属于低层接口，仅适合单线程、短生命周期、受控内部访问
 */
table_node_type_e table_node_type(table_node_t *node);

/**
 * @brief 获取节点的key_id
 * @param node 节点指针
 * @return 节点的key_id
 * @note 节点级API属于低层接口，仅适合单线程、短生命周期、受控内部访问
 */
TB_ID table_node_key(table_node_t *node);

/**
 * @brief 通过节点获取int值
 * @param node 节点指针
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际值，失败返回默认值
 * @note 节点级API属于低层接口，仅适合单线程、短生命周期、受控内部访问；
 *       不适合与delete/clear/destroy等并发修改场景混用
 */
int32_t table_node_get_int(table_node_t *node, int32_t default_value);

/**
 * @brief 通过节点获取double值
 * @param node 节点指针
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际值，失败返回默认值
 * @note 节点级API属于低层接口，仅适合单线程、短生命周期、受控内部访问；
 *       不适合与delete/clear/destroy等并发修改场景混用
 */
double table_node_get_double(table_node_t *node, double default_value);

/**
 * @brief 通过节点获取字符串值
 * @param node 节点指针
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际字符串，失败返回默认值
 * @note 节点级API属于低层接口，仅适合单线程、短生命周期、受控内部访问；
 *       不适合与delete/clear/destroy等并发修改场景混用
 */
const char *table_node_get_string(table_node_t *node, const char *default_value);

/**
 * @brief 通过节点获取指针值
 * @param node 节点指针
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际指针，失败返回默认值
 * @note 节点级API属于低层接口，仅适合单线程、短生命周期、受控内部访问；
 *       不适合与delete/clear/destroy等并发修改场景混用
 */
void *table_node_get_pointer(table_node_t *node, void *default_value);

/**
 * @brief 通过节点获取table值
 * @param node 节点指针
 * @param default_value 获取失败时返回的默认值
 * @return 成功返回实际table，失败返回默认值
 * @note 返回内部table指针；节点级API仅适合单线程、短生命周期、受控内部访问，
 *       不适合跨线程长期持有，也不适合与并发删除/销毁场景混用
 */
table_t *table_node_get_table(table_node_t *node, table_t *default_value);

/**
 * @brief 通过节点设置int值
 * @param node 节点指针
 * @param value 要设置的int值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL 或 TABLE_ERROR_TYPE_MISMATCH
 * @note 节点级API属于低层接口，仅适合单线程、短生命周期、受控内部访问
 */
int32_t table_node_set_int(table_node_t *node, int32_t value);

/**
 * @brief 通过节点设置double值
 * @param node 节点指针
 * @param value 要设置的double值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL 或 TABLE_ERROR_TYPE_MISMATCH
 * @note 节点级API属于低层接口，仅适合单线程、短生命周期、受控内部访问
 */
int32_t table_node_set_double(table_node_t *node, double value);

/**
 * @brief 通过节点设置字符串值（深拷贝）
 * @param node 节点指针
 * @param value 要设置的字符串值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_MEMORY 或 TABLE_ERROR_TYPE_MISMATCH
 * @note 节点级API属于低层接口，仅适合单线程、短生命周期、受控内部访问
 */
int32_t table_node_set_string(table_node_t *node, const char *value);

/**
 * @brief 通过节点设置指针值
 * @param node 节点指针
 * @param value 要设置的指针值
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL 或 TABLE_ERROR_TYPE_MISMATCH
 * @note 节点级API属于低层接口，仅适合单线程、短生命周期、受控内部访问
 */
int32_t table_node_set_pointer(table_node_t *node, void *value);

/**
 * @brief 通过节点设置table值
 * @param node 节点指针
 * @param value 要设置的table指针
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_TYPE_MISMATCH 或 TABLE_ERROR_INVALID_TABLE_GRAPH
 * @note 子table必须满足树结构约束：不能自引用、不能形成环、不能被多个父table共享
 * @note 节点级API属于低层接口，仅适合单线程、短生命周期、受控内部访问
 */
int32_t table_node_set_table(table_node_t *node, table_t *value);

/**
 * @brief 删除table中的指定节点
 * @param tb table指针
 * @param node 要删除的节点指针
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_INVALID_PARAM 或 TABLE_ERROR_NODE_NOT_IN_TABLE
 */
int32_t table_node_delete(table_t *tb, table_node_t *node);

/**
 * @brief 获取节点所属的table
 * @param node 节点指针
 * @return 节点所属的table指针，失败返回NULL
 * @note 返回内部table指针；节点级API仅适合单线程、短生命周期、受控内部访问
 */
table_t *table_node_get_parent(table_node_t *node);

/**
 * @brief 通过key_id获取table中的节点
 * @param tb table指针
 * @param key_id 要获取的节点的key_id
 * @return 节点指针，失败返回NULL（tb为NULL或key不存在）
 * @note 返回内部节点指针；节点级API属于低层接口，仅适合单线程、短生命周期、
 *       受控内部访问，不适合与delete/clear/destroy等并发修改场景混用
 */
table_node_t *table_get_node(table_t *tb, TB_ID key_id);

// ==================== 调试接口 ====================

/**
 * @brief 打印table的内部结构（用于调试）
 * @param tb table指针
 */
void table_dump(table_t *tb);

// ==================== 文件操作接口 ====================

/**
 * @brief 将table转换为字符串格式
 * @param tb table指针
 * @return 字符串指针（需调用者释放），失败返回NULL
 */
char *table_to_string(table_t *tb);

/**
 * @brief 从文件加载table
 * @param file 文件路径
 * @return 加载的table指针，失败返回NULL
 */
table_t *table_load_from_file(const char *file);

/**
 * @brief 将table保存到文件（覆盖模式）
 * @param tb table指针
 * @param file 文件路径
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_INVALID_PARAM 或通用负值错误码
 */
int32_t table_save_to_file(table_t *tb, const char *file);

/**
 * @brief 检查table中的节点是否存在
 * @param tb table指针
 * @param key_id 要检查的键ID
 * @return true表示存在，false表示不存在
 */
bool table_has_key(table_t *tb, TB_ID key_id);

/**
 * @brief 判断table是否为空
 * @param tb table指针
 * @return true表示为空，false表示不为空
 */
bool table_is_empty(table_t *tb);

/**
 * @brief 清空table中的所有节点（不销毁table本身）
 * @param tb table指针
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL
 */
int32_t table_clear(table_t *tb);

/**
 * @brief 遍历table中所有节点
 * @param tb table指针
 * @param callback 回调函数，对每个节点调用；回调执行时不持有table锁。
 *                 遍历期间如有其他线程对同一table执行delete/clear/destroy等写操作，行为未定义。
 * @param user_data 用户自定义数据，传递给回调函数
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL 或 TABLE_ERROR_MEMORY
 */
typedef int (*table_node_callback)(table_node_t *node, void *user_data);
int32_t table_for_each(table_t *tb, table_node_callback callback, void *user_data);

// ==================== ID生成接口 ====================

/**
 * @brief 将字符串转换为唯一ID（FNV-1a哈希值）
 * @param str 字符串指针
 * @return 字符串的唯一ID
 */
TB_ID table_string_to_id(const char *str);

/**
 * @brief 通过ID获取字符串（如果字符串已被注册）
 * @param id 字符串ID
 * @return 字符串指针（如果ID未注册返回NULL，返回值生命周期受字符串池影响）
 */
const char *table_id_string(TB_ID id);

/**
 * @brief 通过ID获取字符串副本
 * @param id 字符串ID
 * @return 新分配的字符串副本（需调用者free），如果ID未注册返回NULL
 */
char *table_id_string_copy(TB_ID id);

/**
 * @brief 注册字符串到字符串池，以便ID可以还原为字符串
 * @param str 要注册的字符串
 * @return 字符串的唯一ID；如果参数非法或检测到哈希碰撞则返回-1
 */
TB_ID table_string_id(const char *str);

/**
 * @brief 主动注册字符串到字符串池并返回唯一ID
 * @param str 要注册的字符串
 * @return 字符串的唯一ID；如果参数非法或检测到哈希碰撞则返回-1
 */
TB_ID table_register_string(const char *str);

// ==================== 二进制序列化接口 ====================

/**
 * @brief 将table序列化为二进制数据
 * @param tb table指针
 * @param data 输出参数，指向分配的二进制数据指针（需调用者free）
 * @param size 输出参数，返回二进制数据的大小
 * @return TABLE_SUCCESS 表示成功；失败时返回 TABLE_ERROR_NULL、TABLE_ERROR_INVALID_PARAM 或 TABLE_ERROR_MEMORY
 * @note POINTER类型的节点不会被序列化，它们只保留本地运行时语义
 */
int32_t table_serialize(table_t *tb, void **data, int32_t *size);

/**
 * @brief 从二进制数据反序列化为table
 * @param data 二进制数据指针
 * @param size 二进制数据大小
 * @return 反序列化的table指针，失败返回NULL
 */
table_t *table_deserialize(const void *data, int32_t size);

#ifdef STRING_POOL_TEST_HOOKS
/**
 * @brief 测试钩子：让下一次内部字符串复制失败
 */
void table_fail_next_strdup_for_test(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TABLE_H */
