#ifndef OMNI_EXT_H
#define OMNI_EXT_H

#include <access/xact.h>
#include <executor/executor.h>
#include <tcop/utility.h>

#include <dynpgext.h>

#include <libgluepg_stc.h>

/**
 * @brief omni_ext.shmem_size configuration variable
 *
 */
extern int shmem_size;

/**
 * @brief omni_ext.max_databases configuration variable
 *
 */
extern int max_databases;

extern int max_allocation_dictionary_entries;

extern dsa_area *My_dsa_area;
extern void *My_dsa_mem;
extern pid_t My_dsa_pid;

#ifndef HASH_STRINGS
#define HASH_STRINGS 0
#endif

#define ALLOCATION_DICTIONARY_KEY_SIZE 128

typedef struct {
  char key[ALLOCATION_DICTIONARY_KEY_SIZE];
  int flags;
  size_t size;
  dsa_pointer ptr;
} allocation_dictionary_entry;

extern HASHCTL allocation_dictionary_ctl;

typedef struct {
  Oid dboid;
  int index;
} database_oid_mapping_entry;

extern HASHCTL database_oid_mapping_ctl;

int dynpgext_handle_cmp(dynpgext_handle *const *left, dynpgext_handle *const *right);

#define i_tag handle
#define i_val dynpgext_handle *
#define i_cmp dynpgext_handle_cmp
#include <stc/cdeq.h>

/**
 * @brief All (tentatively) loaded handles
 *
 */
extern cdeq_handle handles;

/**
 * @brief Dynpgext extension allocation request
 *
 */
typedef struct {
  const dynpgext_handle *handle;
  const char *name;
  void (*callback)(void *ptr, void *data);
  void *data;
  dynpgext_allocate_shmem_flags flags;
  size_t size;
} allocation_request;

int allocation_request_cmp(const allocation_request *left, const allocation_request *right);

#define i_val allocation_request
#define i_tag allocation_request
#define i_cmp allocation_request_cmp
#include <stc/cdeq.h>

/**
 * @brief Dynpgext allocation requests collected during the startup phase
 *
 */
extern cdeq_allocation_request allocation_requests;

typedef struct {
  BackgroundWorker bgw;
  dynpgext_register_bgworker_flags flags;
  NameData extname;
  NameData extver; // this is strictly speaking incorrect, version is a varlen
} background_worker_request;

int background_worker_request_cmp(const background_worker_request *left,
                                  const background_worker_request *right);

#define i_val background_worker_request
#define i_tag background_worker_request
#define i_cmp background_worker_request_cmp
#include <stc/cdeq.h>

typedef struct {
  uint64 id;
  background_worker_request request;
  bool started;
  Oid databaseOid;
} BackgroundWorkerRequest;

extern HTAB *BackgroundWorkerRequests;

typedef struct {
  pg_atomic_uint64 bgworker_next_id;
  // This is currently used by tests and nothing else:
  char reserved[64];
} SharedInfo;

extern SharedInfo *shared_info;

/**
 * @brief Dynpgext background worker requests collected during the startup phase
 *
 */
extern cdeq_background_worker_request background_worker_requests;

/**
 * @brief Allocates shmem during the startup phase
 *
 * @param handle Dynpgext handle
 * @param size amount of memory to allocate
 * @param callback callback to call when allocated
 * @param data Opaque data structure to pass to the callback
 * @param flags allocation flags
 */
void allocate_shmem_startup(const struct dynpgext_handle *handle, const char *name, size_t size,
                            void (*callback)(void *ptr, void *data), void *data,
                            dynpgext_allocate_shmem_flags flags);

/**
 * @brief Registers a background worker during the startup phase
 *
 * @param handle Dynpgext handle
 * @param bgw background worker
 */
void register_bgworker_startup(const struct dynpgext_handle *handle, BackgroundWorker *bgw,
                               dynpgext_register_bgworker_flags flags);

/**
 * @brief Allocates shmem during the runtime phase
 *
 * @param handle Dynpgext handle
 * @param size amount of memory to allocate
 * @param callback callback to call when allocated
 * @param data Opaque data structure to pass to the callback
 * @param flags allocation flags
 */
void allocate_shmem_runtime(const struct dynpgext_handle *handle, const char *name, size_t size,
                            void (*callback)(void *ptr, void *data), void *data,
                            dynpgext_allocate_shmem_flags flags);

/**
 * @brief Registers a background worker during the runtime phase
 *
 * @param handle Dynpgext handle
 * @param bgw background worker
 */
void register_bgworker_runtime(const struct dynpgext_handle *handle, BackgroundWorker *bgw,
                               dynpgext_register_bgworker_flags flags);

/**
 * @brief Get this extension's shared library name
 *
 * @return const char* library name to load
 */
const char *get_library_name();

extern bool dsa_attached;
void ensure_dsa_attached();

bool unload_extension(char *name, char *version);
char *load_extension(char *name, char *version);

extern ExecutorFinish_hook_type old_executor_finish_hook;
extern ProcessUtility_hook_type old_process_utility_hook;
void omni_ext_executor_finish_hook(QueryDesc *queryDesc);
void omni_ext_process_utility_hook(PlannedStmt *pstmt, const char *queryString,
#if PG_MAJORVERSION_NUM > 13
                                   bool readOnlyTree,
#endif
                                   ProcessUtilityContext context, ParamListInfo params,
                                   QueryEnvironment *queryEnv, DestReceiver *dest,
                                   QueryCompletion *qc);
void omni_ext_transaction_callback(XactEvent event, void *arg);

void populate_bgworker_requests_for_db(Oid dboid);
void process_extensions_for_database(char *extname, char *extversion, Oid dboid);

char *get_fitting_library_name(char *library_name);

#endif // OMNI_EXT_H