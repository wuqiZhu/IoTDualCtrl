/**
 * @file security_audit.h
 * @brief 安全审计模块接口定义
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 提供安全审计功能，包括：
 * - 安全事件记录
 * - 登录尝试监控
 * - 配置变更审计
 * - 证书状态监控
 * - 入侵检测
 */

#ifndef SECURITY_AUDIT_H
#define SECURITY_AUDIT_H

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              配置参数 */
/* ========================================================================== */

/** @brief 审计日志最大条数 */
#define AUDIT_MAX_ENTRIES 1000

/** @brief 审计日志文件路径 */
#define AUDIT_LOG_FILE "/var/log/security_audit.log"

/** @brief 证书目录路径 */
#define CERT_DIR "/etc/device/certs"

/** @brief 最大登录尝试次数 */
#define MAX_LOGIN_ATTEMPTS 5

/** @brief 登录锁定时间（秒） */
#define LOGIN_LOCKOUT_TIME 300

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 安全审计错误码 */
typedef enum {
  AUDIT_OK = 0,               /**< 操作成功 */
  AUDIT_ERROR = -1,           /**< 通用错误 */
  AUDIT_ERROR_PARAM = -2,     /**< 参数错误 */
  AUDIT_ERROR_FULL = -3,      /**< 审计日志已满 */
  AUDIT_ERROR_IO = -4,        /**< IO错误 */
} audit_error_t;

/* ========================================================================== */
/*                              安全事件类型 */
/* ========================================================================== */

/** @brief 安全事件类型 */
typedef enum {
  AUDIT_EVENT_LOGIN_SUCCESS = 0,  /**< 登录成功 */
  AUDIT_EVENT_LOGIN_FAILED,       /**< 登录失败 */
  AUDIT_EVENT_LOGIN_LOCKOUT,      /**< 登录锁定 */
  AUDIT_EVENT_CONFIG_CHANGE,      /**< 配置变更 */
  AUDIT_EVENT_CERT_EXPIRING,      /**< 证书即将过期 */
  AUDIT_EVENT_CERT_EXPIRED,       /**< 证书已过期 */
  AUDIT_EVENT_UNAUTHORIZED,       /**< 未授权访问 */
  AUDIT_EVENT_SYSTEM_START,       /**< 系统启动 */
  AUDIT_EVENT_SYSTEM_STOP,        /**< 系统停止 */
  AUDIT_EVENT_FIRMWARE_UPDATE,    /**< 固件更新 */
  AUDIT_EVENT_DATA_EXPORT,        /**< 数据导出 */
  AUDIT_EVENT_OTHER,              /**< 其他事件 */
} audit_event_type_t;

/** @brief 安全事件级别 */
typedef enum {
  AUDIT_LEVEL_INFO = 0,      /**< 信息 */
  AUDIT_LEVEL_WARNING,       /**< 警告 */
  AUDIT_LEVEL_ERROR,         /**< 错误 */
  AUDIT_LEVEL_CRITICAL,      /**< 严重 */
} audit_level_t;

/* ========================================================================== */
/*                              数据结构 */
/* ========================================================================== */

/** @brief 安全审计条目 */
typedef struct {
  time_t timestamp;                /**< 时间戳 */
  audit_event_type_t event_type;   /**< 事件类型 */
  audit_level_t level;             /**< 事件级别 */
  char source[64];                 /**< 事件来源（IP/模块） */
  char user[32];                   /**< 用户名 */
  char description[256];           /**< 事件描述 */
  char details[512];               /**< 详细信息 */
} audit_entry_t;

/** @brief 安全审计统计 */
typedef struct {
  int total_events;                /**< 总事件数 */
  int login_attempts;              /**< 登录尝试次数 */
  int login_failures;              /**< 登录失败次数 */
  int config_changes;              /**< 配置变更次数 */
  int unauthorized_access;         /**< 未授权访问次数 */
  time_t last_login;               /**< 最后登录时间 */
  time_t last_failure;             /**< 最后失败时间 */
} audit_stats_t;

/** @brief 证书信息 */
typedef struct {
  char cert_path[256];             /**< 证书文件路径 */
  char issuer[128];                /**< 颁发者 */
  char subject[128];               /**< 主题 */
  time_t not_before;               /**< 生效时间 */
  time_t not_after;                /**< 过期时间 */
  int days_remaining;              /**< 剩余天数 */
  bool is_valid;                   /**< 是否有效 */
} cert_info_t;

/* ========================================================================== */
/*                              接口函数 */
/* ========================================================================== */

/**
 * @brief 初始化安全审计模块
 * @return AUDIT_OK成功
 */
audit_error_t security_audit_init(void);

/**
 * @brief 清理安全审计模块资源
 */
void security_audit_cleanup(void);

/**
 * @brief 记录安全事件
 * @param event_type 事件类型
 * @param level 事件级别
 * @param source 事件来源
 * @param user 用户名
 * @param description 事件描述
 * @param details 详细信息
 * @return AUDIT_OK成功
 */
audit_error_t audit_log_event(audit_event_type_t event_type, audit_level_t level,
                              const char *source, const char *user,
                              const char *description, const char *details);

/**
 * @brief 记录登录尝试
 * @param source 来源IP
 * @param user 用户名
 * @param success 是否成功
 * @return AUDIT_OK成功
 */
audit_error_t audit_log_login(const char *source, const char *user, bool success);

/**
 * @brief 记录配置变更
 * @param source 来源
 * @param user 用户名
 * @param key 配置项
 * @param old_value 旧值
 * @param new_value 新值
 * @return AUDIT_OK成功
 */
audit_error_t audit_log_config_change(const char *source, const char *user,
                                      const char *key, const char *old_value,
                                      const char *new_value);

/**
 * @brief 获取审计统计信息
 * @param stats 输出统计信息
 * @return AUDIT_OK成功
 */
audit_error_t audit_get_stats(audit_stats_t *stats);

/**
 * @brief 获取最近的审计条目
 * @param entries 输出条目数组
 * @param max_count 数组最大容量
 * @param count 实际条目数
 * @return AUDIT_OK成功
 */
audit_error_t audit_get_recent(audit_entry_t *entries, int max_count, int *count);

/**
 * @brief 检查IP是否被锁定
 * @param ip IP地址
 * @return true被锁定, false未锁定
 */
bool audit_is_locked(const char *ip);

/**
 * @brief 清除IP锁定
 * @param ip IP地址
 * @return AUDIT_OK成功
 */
audit_error_t audit_clear_lock(const char *ip);

/**
 * @brief 检查证书状态
 * @param cert_path 证书文件路径
 * @param info 输出证书信息
 * @return AUDIT_OK成功
 */
audit_error_t audit_check_certificate(const char *cert_path, cert_info_t *info);

/**
 * @brief 检查所有证书状态
 * @return 即将过期的证书数量
 */
int audit_check_all_certificates(void);

/**
 * @brief 获取事件类型字符串
 * @param event_type 事件类型
 * @return 事件类型字符串
 */
const char *audit_get_event_string(audit_event_type_t event_type);

/**
 * @brief 获取事件级别字符串
 * @param level 事件级别
 * @return 级别字符串
 */
const char *audit_get_level_string(audit_level_t level);

/**
 * @brief 打印审计报告
 */
void audit_print_report(void);

/**
 * @brief 导出审计日志到文件
 * @param file_path 文件路径
 * @return AUDIT_OK成功
 */
audit_error_t audit_export_log(const char *file_path);

/**
 * @brief 清空审计日志
 * @return AUDIT_OK成功
 */
audit_error_t audit_clear_log(void);

#ifdef __cplusplus
}
#endif

#endif /* SECURITY_AUDIT_H */
