/**
 * @file message.h
 * @author Arthur Wei <arthur.wei@carbit.com.cn>
 */

#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "table.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 远程方法回调函数类型
 *
 * @param remote_name 发起调用的远端对象名称
 * @param method_id 方法 ID，必须由 TB_STR_ID(...) 生成
 * @param data_in 输入载荷
 * @param data_out 输出载荷，由回调填充
 */
typedef void (*remote_method_t)(const char *remote_name, int64_t method_id, table_t *data_in, table_t *data_out);

/**
 * @brief 远端消息回调函数类型
 *
 * @param remote_name 消息发布方名称
 * @param msg_id 消息 ID，必须由 TB_STR_ID(...) 生成
 * @param data 输入载荷
 * @param arg 用户上下文
 * @return 成功返回 0，失败返回非 0
 */
typedef int32_t (*remote_message_t)(const char *remote_name, int64_t msg_id, table_t *data, void *arg);

/**
 * @brief 远程消息订阅选项
 */
typedef enum
{
    MESSAGE_SUB_FLAG_NONE = 0,                         /**< 不请求订阅成功后的缓存补发 */
    MESSAGE_SUB_FLAG_SNAPSHOT_ON_SUBSCRIBE = 1u << 0,  /**< 订阅成功后请求发布者补发最新缓存 */
} message_sub_flag_e;

/**
 * @brief libmessage 公共日志等级定义
 */
typedef enum
{
    MESSAGE_LOG_LEVEL_DEBUG = 0,   /**< 输出 DEBUG/INFO/WARN/ERROR/FATAL */
    MESSAGE_LOG_LEVEL_INFO,        /**< 输出 INFO/WARN/ERROR/FATAL */
    MESSAGE_LOG_LEVEL_WARN,        /**< 输出 WARN/ERROR/FATAL */
    MESSAGE_LOG_LEVEL_ERROR,       /**< 输出 ERROR/FATAL */
    MESSAGE_LOG_LEVEL_FATAL,       /**< 仅输出 FATAL */
    MESSAGE_LOG_LEVEL_OFF,         /**< 关闭全部日志 */
} message_log_level_e;

/**
 * @brief 所有 method_id/msg_id 必须统一通过 TB_STR_ID("module.action") 生成
 */

/**
 * @brief 创建消息对象
 * @param name 消息对象名称，必须为非空字符串，且长度不能超过31字节
 * @param method 远程方法回调函数
 * @param ctx 用户上下文指针
 * @return void* 成功返回消息对象指针，失败返回NULL
 *
 * @note 如果创建过程中任何一步失败，会释放已分配的资源并返回NULL
 */
void *message_object_create(const char *name, remote_method_t method, void *ctx);

/**
 * @brief 销毁消息对象
 * @param obj 要销毁的消息对象指针
 * @return 成功返回0，失败返回-1
 * @note 一旦开始销毁，该对象不得再用于发送消息、发起RPC、广播或成员检查
 * @note 当后台线程停止或成员自移除失败时，接口会返回-1，但仍会继续释放本地资源
 */
int32_t message_object_destroy(void *obj);

/**
 * @brief 设置当前进程内 libmessage 的全局日志等级
 * @param level 目标日志等级，参见 @ref message_log_level_e
 * @return 成功返回0，失败返回-1
 * @note 默认日志等级为 MESSAGE_LOG_LEVEL_FATAL
 * @note 该设置对当前进程内所有 libmessage 日志点生效，包括已有对象和后续创建对象
 */
int32_t message_set_log_level(message_log_level_e level);

/**
 * @brief 远程方法调用
 * @param obj 消息对象指针
 * @param remote_name 远程对象名称
 * @param method_id 方法ID，必须通过TB_STR_ID(...)生成
 * @param data 输入数据(table格式)
 * @param out 输出数据(table格式)指针
 * @param wait_reply 是否等待回复
 * @return int32_t 成功返回0，失败返回-1
 *
 * @note 该函数用于向指定远程对象发起方法调用，可选择是否等待返回结果
 *       wait_reply=true时，会等待远程对象返回结果，并将结果存储在out中，timeout为500ms
 *       同步RPC不提供单独的过载回复语义，超时仅表示在超时窗口内未收到回复
 *       调用方不能仅凭超时判断远端是否未执行、执行中或已执行但回复未达
 *       remote_method_t 回调中禁止使用 RPC 调用，包括同步 RPC 和异步 RPC
 *       out中的内容需要由调用者自行释放
 *       若序列化后的payload超过接收缓冲上限，会在发送前直接返回失败
 * 
 * @note 内部会拷贝一份data，携带的参数data由调用者自行释放
 */
int32_t message_remote_method(void *obj, const char *remote_name, int64_t method_id, table_t *data, table_t **out, bool wait_reply);

/**
 * @brief 给指定通讯对象发送一条消息
 * @param obj 消息对象指针
 * @param remote_name 远程对象名称
 * @param msg_id 消息ID，必须通过TB_STR_ID(...)生成
 * @param data 消息数据(table格式) 
 * @return int32_t 成功返回0，失败返回-1
 * @note 内部会拷贝一份data数据，调用者负责释放data数据
 */
int32_t message_trigger_message_to_remote(void *obj, const char *remote_name, int64_t msg_id, table_t *data);

/**
 * @brief 触发远程广播消息
 * @param obj 消息对象指针
 * @param msg_id 消息ID，必须通过TB_STR_ID(...)生成
 * @param data 消息数据(table格式)
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t message_trigger_remote_broadcast(void *obj, int64_t msg_id, table_t *data);

/**
 * @brief 注册远程消息回调函数
 * @param obj 消息对象指针
 * @param remote_name 远程对象名称
 * @param msg_id 消息ID，必须通过TB_STR_ID(...)生成
 * @param cb 消息回调函数
 * @param arg 回调函数上下文参数
 * @param flags 订阅选项，参见 @ref message_sub_flag_e
 * @return 成功返回0，失败返回-1
 * @note 对同一(remote_name, msg_id)重复注册时，后一次注册会覆盖前一次回调和上下文
 */
int32_t message_register_remote_message(void *obj, const char *remote_name, int64_t msg_id, remote_message_t cb, void *arg, uint32_t flags);

/**
 * @brief 检查远程成员是否存在
 * @param obj 消息对象指针
 * @param remote_name 远程成员名称
 * @return true 当前本地成员缓存中存在该远端且消息可达
 * @return false 远程成员不存在或参数无效
 * @note 若obj或remote_name为NULL，会记录错误日志并返回false
 * @note 该接口是基于本地成员缓存的弱一致检查，不保证新成员已被同步，也不保证远端后续持续存活
 */
bool message_check_remote_exist(void *obj, const char *remote_name);

#ifdef __cplusplus
}
#endif

#endif // MESSAGE_H
