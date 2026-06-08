/**
 * @file plugin_manager.c
 * @brief 插件管理器实现
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 实现插件动态加载和管理功能。
 */

#include "plugin_manager.h"
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ========================================================================== */
/*                              内部数据结构 */
/* ========================================================================== */

/** @brief 插件管理器上下文 */
typedef struct {
  plugin_context_t plugins[PLUGIN_MAX_PLUGINS]; /**< 插件数组 */
  int plugin_count;                             /**< 插件数量 */
  pthread_mutex_t lock;                         /**< 互斥锁 */
  bool initialized;                             /**< 初始化标志 */
} plugin_manager_t;

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief 插件管理器全局上下文 */
static plugin_manager_t g_manager = {
    .plugin_count = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .initialized = false,
};

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

/**
 * @brief 查找插件
 * @param name 插件名称
 * @return 插件索引, -1未找到
 */
static int find_plugin(const char *name) {
  if (!name) {
    return -1;
  }

  for (int i = 0; i < g_manager.plugin_count; i++) {
    if (strcmp(g_manager.plugins[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief 获取插件状态字符串
 * @param state 插件状态
 * @return 状态字符串
 */
static const char *get_state_string(plugin_state_t state) {
  switch (state) {
  case PLUGIN_STATE_UNLOADED:
    return "UNLOADED";
  case PLUGIN_STATE_LOADED:
    return "LOADED";
  case PLUGIN_STATE_INITIALIZED:
    return "INITIALIZED";
  case PLUGIN_STATE_STARTED:
    return "STARTED";
  case PLUGIN_STATE_STOPPED:
    return "STOPPED";
  case PLUGIN_STATE_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

/* ========================================================================== */
/*                              接口实现 */
/* ========================================================================== */

plugin_error_t plugin_manager_init(void) {
  pthread_mutex_lock(&g_manager.lock);

  if (g_manager.initialized) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_OK;
  }

  memset(g_manager.plugins, 0, sizeof(g_manager.plugins));
  g_manager.plugin_count = 0;
  g_manager.initialized = true;

  /* 创建插件目录 */
  mkdir(PLUGIN_DIR, 0755);

  pthread_mutex_unlock(&g_manager.lock);

  printf("[PLUGIN] Plugin manager initialized\n");
  return PLUGIN_OK;
}

void plugin_manager_cleanup(void) {
  pthread_mutex_lock(&g_manager.lock);

  /* 卸载所有插件 */
  for (int i = 0; i < g_manager.plugin_count; i++) {
    plugin_context_t *ctx = &g_manager.plugins[i];

    if (ctx->ops) {
      /* 停止插件 */
      if (ctx->state == PLUGIN_STATE_STARTED && ctx->ops->stop) {
        ctx->ops->stop();
      }

      /* 清理插件 */
      if (ctx->ops->cleanup) {
        ctx->ops->cleanup();
      }
    }

    /* 关闭动态库 */
    if (ctx->handle) {
      dlclose(ctx->handle);
      ctx->handle = NULL;
    }

    ctx->state = PLUGIN_STATE_UNLOADED;
  }

  memset(g_manager.plugins, 0, sizeof(g_manager.plugins));
  g_manager.plugin_count = 0;
  g_manager.initialized = false;

  pthread_mutex_unlock(&g_manager.lock);

  printf("[PLUGIN] Plugin manager cleanup completed\n");
}

plugin_error_t plugin_load(const char *path) {
  if (!path) {
    return PLUGIN_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_manager.lock);

  if (!g_manager.initialized) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR;
  }

  /* 检查是否已满 */
  if (g_manager.plugin_count >= PLUGIN_MAX_PLUGINS) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR_FULL;
  }

  /* 打开动态库 */
  void *handle = dlopen(path, RTLD_LAZY);
  if (!handle) {
    printf("[PLUGIN] Failed to load plugin: %s\n", dlerror());
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR_LOAD;
  }

  /* 获取插件操作接口 */
  typedef const plugin_ops_t *(*get_ops_func)(void);
  get_ops_func get_ops = (get_ops_func)dlsym(handle, "plugin_get_ops");
  if (!get_ops) {
    printf("[PLUGIN] Failed to find plugin_get_ops: %s\n", dlerror());
    dlclose(handle);
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR_SYMBOL;
  }

  const plugin_ops_t *ops = get_ops();
  if (!ops) {
    printf("[PLUGIN] Failed to get plugin ops\n");
    dlclose(handle);
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR;
  }

  /* 获取插件信息 */
  const plugin_info_t *info = NULL;
  if (ops->get_info) {
    info = ops->get_info();
  }

  if (!info) {
    printf("[PLUGIN] Failed to get plugin info\n");
    dlclose(handle);
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR;
  }

  /* 检查是否已存在 */
  if (find_plugin(info->name) >= 0) {
    printf("[PLUGIN] Plugin already loaded: %s\n", info->name);
    dlclose(handle);
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR_ALREADY;
  }

  /* 创建插件上下文 */
  plugin_context_t *ctx = &g_manager.plugins[g_manager.plugin_count];
  strncpy(ctx->name, info->name, PLUGIN_MAX_NAME_LEN - 1);
  ctx->name[PLUGIN_MAX_NAME_LEN - 1] = '\0';
  strncpy(ctx->path, path, PLUGIN_MAX_PATH_LEN - 1);
  ctx->path[PLUGIN_MAX_PATH_LEN - 1] = '\0';
  ctx->handle = handle;
  ctx->ops = ops;
  memcpy(&ctx->info, info, sizeof(plugin_info_t));
  ctx->state = PLUGIN_STATE_LOADED;
  ctx->user_data = NULL;

  g_manager.plugin_count++;

  pthread_mutex_unlock(&g_manager.lock);

  printf("[PLUGIN] Loaded plugin: %s (v%s)\n", info->name, info->version);
  return PLUGIN_OK;
}

plugin_error_t plugin_unload(const char *name) {
  if (!name) {
    return PLUGIN_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_manager.lock);

  int index = find_plugin(name);
  if (index < 0) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR_NOT_FOUND;
  }

  plugin_context_t *ctx = &g_manager.plugins[index];

  /* 停止并清理插件 */
  if (ctx->ops) {
    if (ctx->state == PLUGIN_STATE_STARTED && ctx->ops->stop) {
      ctx->ops->stop();
    }
    if (ctx->ops->cleanup) {
      ctx->ops->cleanup();
    }
  }

  /* 关闭动态库 */
  if (ctx->handle) {
    dlclose(ctx->handle);
  }

  /* 移除插件（移动后面的插件） */
  for (int i = index; i < g_manager.plugin_count - 1; i++) {
    memcpy(&g_manager.plugins[i], &g_manager.plugins[i + 1],
           sizeof(plugin_context_t));
  }
  g_manager.plugin_count--;

  pthread_mutex_unlock(&g_manager.lock);

  printf("[PLUGIN] Unloaded plugin: %s\n", name);
  return PLUGIN_OK;
}

plugin_error_t plugin_init(const char *name, const char *config) {
  if (!name) {
    return PLUGIN_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_manager.lock);

  int index = find_plugin(name);
  if (index < 0) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR_NOT_FOUND;
  }

  plugin_context_t *ctx = &g_manager.plugins[index];

  if (ctx->state != PLUGIN_STATE_LOADED) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR;
  }

  if (!ctx->ops || !ctx->ops->init) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR;
  }

  /* 初始化插件 */
  int ret = ctx->ops->init(config);
  if (ret != 0) {
    ctx->state = PLUGIN_STATE_ERROR;
    pthread_mutex_unlock(&g_manager.lock);
    printf("[PLUGIN] Failed to initialize plugin: %s\n", name);
    return PLUGIN_ERROR_INIT;
  }

  ctx->state = PLUGIN_STATE_INITIALIZED;

  pthread_mutex_unlock(&g_manager.lock);

  printf("[PLUGIN] Initialized plugin: %s\n", name);
  return PLUGIN_OK;
}

plugin_error_t plugin_start(const char *name) {
  if (!name) {
    return PLUGIN_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_manager.lock);

  int index = find_plugin(name);
  if (index < 0) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR_NOT_FOUND;
  }

  plugin_context_t *ctx = &g_manager.plugins[index];

  if (ctx->state != PLUGIN_STATE_INITIALIZED &&
      ctx->state != PLUGIN_STATE_STOPPED) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR;
  }

  if (!ctx->ops || !ctx->ops->start) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR;
  }

  /* 启动插件 */
  int ret = ctx->ops->start();
  if (ret != 0) {
    ctx->state = PLUGIN_STATE_ERROR;
    pthread_mutex_unlock(&g_manager.lock);
    printf("[PLUGIN] Failed to start plugin: %s\n", name);
    return PLUGIN_ERROR;
  }

  ctx->state = PLUGIN_STATE_STARTED;

  pthread_mutex_unlock(&g_manager.lock);

  printf("[PLUGIN] Started plugin: %s\n", name);
  return PLUGIN_OK;
}

plugin_error_t plugin_stop(const char *name) {
  if (!name) {
    return PLUGIN_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_manager.lock);

  int index = find_plugin(name);
  if (index < 0) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR_NOT_FOUND;
  }

  plugin_context_t *ctx = &g_manager.plugins[index];

  if (ctx->state != PLUGIN_STATE_STARTED) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR;
  }

  if (!ctx->ops || !ctx->ops->stop) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR;
  }

  /* 停止插件 */
  int ret = ctx->ops->stop();
  if (ret != 0) {
    ctx->state = PLUGIN_STATE_ERROR;
    pthread_mutex_unlock(&g_manager.lock);
    printf("[PLUGIN] Failed to stop plugin: %s\n", name);
    return PLUGIN_ERROR;
  }

  ctx->state = PLUGIN_STATE_STOPPED;

  pthread_mutex_unlock(&g_manager.lock);

  printf("[PLUGIN] Stopped plugin: %s\n", name);
  return PLUGIN_OK;
}

const plugin_context_t *plugin_get(const char *name) {
  if (!name) {
    return NULL;
  }

  pthread_mutex_lock(&g_manager.lock);

  int index = find_plugin(name);
  if (index < 0) {
    pthread_mutex_unlock(&g_manager.lock);
    return NULL;
  }

  const plugin_context_t *ctx = &g_manager.plugins[index];
  pthread_mutex_unlock(&g_manager.lock);

  return ctx;
}

plugin_error_t plugin_list(char plugins[][PLUGIN_MAX_NAME_LEN], int max_count,
                           int *count) {
  if (!plugins || max_count <= 0 || !count) {
    return PLUGIN_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_manager.lock);

  *count = 0;
  for (int i = 0; i < g_manager.plugin_count && *count < max_count; i++) {
    strncpy(plugins[*count], g_manager.plugins[i].name, PLUGIN_MAX_NAME_LEN - 1);
    plugins[*count][PLUGIN_MAX_NAME_LEN - 1] = '\0';
    (*count)++;
  }

  pthread_mutex_unlock(&g_manager.lock);
  return PLUGIN_OK;
}

int plugin_load_all(const char *dir) {
  if (!dir) {
    return 0;
  }

  DIR *d = opendir(dir);
  if (!d) {
    printf("[PLUGIN] Plugin directory not found: %s\n", dir);
    return 0;
  }

  int loaded = 0;
  struct dirent *entry;

  while ((entry = readdir(d)) != NULL) {
    /* 跳过.和.. */
    if (entry->d_name[0] == '.') {
      continue;
    }

    /* 检查是否为.so文件 */
    size_t len = strlen(entry->d_name);
    if (len > 3 && strcmp(entry->d_name + len - 3, ".so") == 0) {
      char path[512];
      snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

      if (plugin_load(path) == PLUGIN_OK) {
        loaded++;
      }
    }
  }

  closedir(d);

  printf("[PLUGIN] Loaded %d plugins from %s\n", loaded, dir);
  return loaded;
}

plugin_error_t plugin_handle_request(const char *name, const char *request,
                                     char *response, int response_size) {
  if (!name || !request || !response || response_size <= 0) {
    return PLUGIN_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_manager.lock);

  int index = find_plugin(name);
  if (index < 0) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR_NOT_FOUND;
  }

  plugin_context_t *ctx = &g_manager.plugins[index];

  if (ctx->state != PLUGIN_STATE_STARTED) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR;
  }

  if (!ctx->ops || !ctx->ops->handle_request) {
    pthread_mutex_unlock(&g_manager.lock);
    return PLUGIN_ERROR;
  }

  /* 处理请求 */
  int ret = ctx->ops->handle_request(request, response, response_size);

  pthread_mutex_unlock(&g_manager.lock);
  return (ret == 0) ? PLUGIN_OK : PLUGIN_ERROR;
}

const char *plugin_get_error_string(plugin_error_t error) {
  switch (error) {
  case PLUGIN_OK:
    return "Success";
  case PLUGIN_ERROR:
    return "Generic error";
  case PLUGIN_ERROR_PARAM:
    return "Invalid parameter";
  case PLUGIN_ERROR_NOT_FOUND:
    return "Plugin not found";
  case PLUGIN_ERROR_LOAD:
    return "Failed to load plugin";
  case PLUGIN_ERROR_INIT:
    return "Failed to initialize plugin";
  case PLUGIN_ERROR_FULL:
    return "Plugin slots full";
  case PLUGIN_ERROR_ALREADY:
    return "Plugin already loaded";
  case PLUGIN_ERROR_SYMBOL:
    return "Symbol not found";
  default:
    return "Unknown error";
  }
}

void plugin_print_status(void) {
  pthread_mutex_lock(&g_manager.lock);

  printf("\n=== Plugin Status ===\n");
  printf("Total plugins: %d\n", g_manager.plugin_count);
  printf("\n%-20s %-10s %-10s %-30s\n", "Name", "Version", "State",
         "Description");
  printf("--------------------------------------------------------------"
         "----------------\n");

  for (int i = 0; i < g_manager.plugin_count; i++) {
    plugin_context_t *ctx = &g_manager.plugins[i];
    printf("%-20s %-10s %-10s %-30s\n", ctx->name, ctx->info.version,
           get_state_string(ctx->state), ctx->info.description);
  }

  printf("========================\n\n");

  pthread_mutex_unlock(&g_manager.lock);
}
