// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on
#include <access/genam.h>
#include <access/skey.h>
#include <access/table.h>
#include <access/xact.h>
#include <catalog/pg_extension.h>
#if PG_MAJORVERSION_NUM == 13
#include <catalog/indexing.h>
#endif
#include <commands/dbcommands.h>
#include <commands/defrem.h>
#include <commands/extension.h>
#include <miscadmin.h>
#include <storage/lwlock.h>
#include <utils/builtins.h>
#include <utils/fmgroids.h>
#include <utils/rel.h>

#include "omni_ext.h"

ProcessUtility_hook_type old_process_utility_hook;

typedef struct {
  char *name;
  char *version;
} LoadExtension;

static List *pending_loads = NIL;

void omni_ext_process_utility_hook(PlannedStmt *pstmt, const char *queryString,
#if PG_MAJORVERSION_NUM > 13
                                   bool readOnlyTree,
#endif
                                   ProcessUtilityContext context, ParamListInfo params,
                                   QueryEnvironment *queryEnv, DestReceiver *dest,
                                   QueryCompletion *qc) {
  if (old_process_utility_hook != NULL) {
    old_process_utility_hook(pstmt, queryString,
#if PG_MAJORVERSION_NUM > 13
                             readOnlyTree,
#endif
                             context, params, queryEnv, dest, qc);
  } else {
    standard_ProcessUtility(pstmt, queryString,
#if PG_MAJORVERSION_NUM > 13
                            readOnlyTree,
#endif
                            context, params, queryEnv, dest, qc);
  }
  Node *node = pstmt->utilityStmt;
  if (node != NULL) {
    switch (nodeTag(node)) {
    case T_CreatedbStmt: {
      // This one is not transactional so we can process it right away
      CreatedbStmt *stmt = castNode(CreatedbStmt, node);
      Oid dboid = get_database_oid(stmt->dbname, false);
      populate_bgworker_requests_for_db(dboid);
      break;
    }
    case T_CreateExtensionStmt: {
      // At this point, the extension has been created. We don't know the version necessarily,
      // but we do know the name.
      //
      CreateExtensionStmt *stmt = castNode(CreateExtensionStmt, node);
      char *extname = stmt->extname;
      char *version = NULL;
      // Try to see if the version is supplied
      {
        ListCell *lc;
        foreach (lc, stmt->options) {
          DefElem *defelem = lfirst_node(DefElem, lc);
          if (strncmp(defelem->defname, "new_version", sizeof("new_version")) == 0) {
            // If we do know the version, it's easy:
            version = defGetString(defelem);
            // proceed
            goto version_ready;
          }
        }
      }
      // The version was not supplied, going to find it

      {
        Oid result;
        Relation rel;
        SysScanDesc scandesc;
        HeapTuple tuple;
        struct ScanKeyData entry[1];

        rel = table_open(ExtensionRelationId, AccessShareLock);

        ScanKeyInit(&entry[0], Anum_pg_extension_extname, BTEqualStrategyNumber, F_NAMEEQ,
                    CStringGetDatum(extname));

        scandesc = systable_beginscan(rel, ExtensionNameIndexId, true, NULL, 1, entry);

        tuple = systable_getnext(scandesc);

        /* We assume that there can be at most one matching tuple */
        if (HeapTupleIsValid(tuple)) {
          bool is_version_null;
          Datum version_datum =
              heap_getattr(tuple, Anum_pg_extension_extversion, rel->rd_att, &is_version_null);
          if (!is_version_null) {
            version = text_to_cstring(DatumGetTextPP(version_datum));
          }
        }

        systable_endscan(scandesc);

        table_close(rel, AccessShareLock);

        if (version == NULL)
          ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT),
                          errmsg("extension \"%s\" does not exist", extname)));
      }

    version_ready: {
      MemoryContext oldcontext = MemoryContextSwitchTo(TopTransactionContext);
      LoadExtension *load = palloc(sizeof(LoadExtension));
      load->name = pstrdup(extname);
      load->version = pstrdup(version);
      pending_loads = lappend(pending_loads, load);
      MemoryContextSwitchTo(oldcontext);
    } break;
    }
    default:
      break;
    }
  }
}

void omni_ext_transaction_callback(XactEvent event, void *arg) {
  switch (event) {
  case XACT_EVENT_COMMIT: {
    ListCell *lc;
    foreach (lc, pending_loads) {
      LoadExtension *ext = (LoadExtension *)lfirst(lc);
      load_extension(ext->name, ext->version);

      // Process new matching entries
      process_extensions_for_database(ext->name, ext->version, MyDatabaseId);
    }
  }
  case XACT_EVENT_ABORT:
    // Cleanup
    list_free_deep(pending_loads);
    pending_loads = NIL;
    break;
  default:
    break;
  }
}
