// SPDX-License-Identifier: GPL-3.0-or-later

#include "sqlite_db_migration.h"

static int return_int_cb(void *data, int argc, char **argv, char **column)
{
    int *status = data;
    UNUSED(argc);
    UNUSED(column);
    *status = str2uint32_t(argv[0]);
    return 0;
}


static int table_exists_in_database(const char *table)
{
    char *err_msg = NULL;
    char sql[128];

    int exists = 0;

    snprintf(sql, 127, "select 1 from sqlite_schema where type = 'table' and name = '%s';", table);

    int rc = sqlite3_exec(db_meta, sql, return_int_cb, (void *) &exists, &err_msg);
    if (rc != SQLITE_OK) {
        info("Error checking table existence; %s", err_msg);
        sqlite3_free(err_msg);
    }

    return exists;
}

static int column_exists_in_table(const char *table, const char *column)
{
    char *err_msg = NULL;
    char sql[128];

    int exists = 0;

    snprintf(sql, 127, "SELECT 1 FROM pragma_table_info('%s') where name = '%s';", table, column);

    int rc = sqlite3_exec(db_meta, sql, return_int_cb, (void *) &exists, &err_msg);
    if (rc != SQLITE_OK) {
        info("Error checking column existence; %s", err_msg);
        sqlite3_free(err_msg);
    }

    return exists;
}

const char *database_migrate_v1_v2[] = {
    "ALTER TABLE host ADD hops INTEGER;",
    NULL
};

const char *database_migrate_v2_v3[] = {
    "ALTER TABLE host ADD memory_mode INT NOT NULL DEFAULT 0;",
    "ALTER TABLE host ADD abbrev_timezone TEXT NOT NULL DEFAULT '';",
    "ALTER TABLE host ADD utc_offset INT NOT NULL DEFAULT 0;",
    "ALTER TABLE host ADD program_name TEXT NOT NULL DEFAULT 'unknown';",
    "ALTER TABLE host ADD program_version TEXT NOT NULL DEFAULT 'unknown';",
    "ALTER TABLE host ADD entries INT NOT NULL DEFAULT 0;",
    "ALTER TABLE host ADD health_enabled INT NOT NULL DEFAULT 0;",
    NULL
};

static int do_migration_v1_v2(sqlite3 *database, const char *name)
{
    UNUSED(name);
    info("Running \"%s\" database migration", name);

    if (table_exists_in_database("host") && !column_exists_in_table("host", "hops"))
        return init_database_batch(database, DB_CHECK_NONE, 0, &database_migrate_v1_v2[0]);
    return 0;
}

static int do_migration_v2_v3(sqlite3 *database, const char *name)
{
    UNUSED(name);
    info("Running \"%s\" database migration", name);

    if (table_exists_in_database("host") && !column_exists_in_table("host", "memory_mode"))
        return init_database_batch(database, DB_CHECK_NONE, 0, &database_migrate_v2_v3[0]);
    return 0;
}


static int do_migration_noop(sqlite3 *database, const char *name)
{
    UNUSED(database);
    UNUSED(name);
    info("Running database migration %s", name);
    return 0;
}

typedef struct database_func_migration_list {
    char *name;
    int (*func)(sqlite3 *database, const char *name);
} DATABASE_FUNC_MIGRATION_LIST;


static int migrate_database(sqlite3 *database, int target_version, char *db_name, DATABASE_FUNC_MIGRATION_LIST *migration_list)
{
    int user_version = 0;
    char *err_msg = NULL;

    int rc = sqlite3_exec(database, "PRAGMA user_version;", return_int_cb, (void *) &user_version, &err_msg);
    if (rc != SQLITE_OK) {
        info("Error checking the %s database version; %s", db_name, err_msg);
        sqlite3_free(err_msg);
    }

    if (likely(user_version == target_version)) {
        info("%s database version is %d (no migration needed)", db_name, target_version);
        return target_version;
    }

    info("Database version is %d, current version is %d. Running migration for %s ...", user_version, target_version, db_name);
    for (int i = user_version; migration_list[i].func && i < target_version; i++) {
        rc = (migration_list[i].func)(database, migration_list[i].name);
        if (unlikely(rc)) {
            error_report("Database %s migration from version %d to version %d failed", db_name, i, i + 1);
            return i;
        }
    }
    return target_version;

}

DATABASE_FUNC_MIGRATION_LIST migration_action[] = {
    {.name = "v0 to v1",  .func = do_migration_noop},
    {.name = "v1 to v2",  .func = do_migration_v1_v2},
    {.name = "v2 to v3",  .func = do_migration_v2_v3},
    // the terminator of this array
    {.name = NULL, .func = NULL}
};

DATABASE_FUNC_MIGRATION_LIST context_migration_action[] = {
    {.name = "v0 to v1",  .func = do_migration_noop},

    // the terminator of this array
    {.name = NULL, .func = NULL}
};


int perform_database_migration(sqlite3 *database, int target_version)
{
    return migrate_database(database, target_version, "metadata", migration_action);
}

int perform_context_database_migration(sqlite3 *database, int target_version)
{
    return migrate_database(database, target_version, "context", context_migration_action);
}
