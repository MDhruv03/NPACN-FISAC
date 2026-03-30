/*
    database.c - Embedded SQLite3 database layer.

    Provides persistent storage for:
    - User accounts with password hashing
    - Location updates with timestamps
    - Session tokens for authenticated sessions
    - System event logs (syslogs)

    Uses the SQLite3 amalgamation compiled directly into the server,
    eliminating any external database dependency.
*/

#include "database.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>

static sqlite3 *db = NULL;

/* Simple string hash (djb2) for password hashing demonstration.
 * In production, use bcrypt/argon2. For this academic project,
 * we store a hex-encoded hash to demonstrate the concept. */
static unsigned long djb2_hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++) != 0) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static void hash_password(const char *password, char *out, int out_size) {
    unsigned long h = djb2_hash(password);
    snprintf(out, out_size, "%016lx", h);
}

/* Generate a random hex token for sessions */
static void generate_token(char *out, int len) {
    static const char hex[] = "0123456789abcdef";
    srand((unsigned int)time(NULL) ^ (unsigned int)GetCurrentProcessId());
    for (int i = 0; i < len - 1; i++) {
        out[i] = hex[rand() % 16];
    }
    out[len - 1] = '\0';
}

/* ---- Database Initialization ---- */

int db_init(const char *db_path) {
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Cannot open database '%s': %s\n", db_path, sqlite3_errmsg(db));
        return -1;
    }

    /* Enable WAL mode for better concurrent read/write performance */
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    /* Create tables if they don't exist */
    const char *schema =
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT UNIQUE NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS locations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL REFERENCES users(id),"
        "  latitude REAL NOT NULL,"
        "  longitude REAL NOT NULL,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL REFERENCES users(id),"
        "  session_token TEXT UNIQUE NOT NULL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  expires_at DATETIME NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS logs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  level TEXT NOT NULL,"
        "  message TEXT NOT NULL,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_locations_user ON locations(user_id);"
        "CREATE INDEX IF NOT EXISTS idx_locations_ts ON locations(timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_sessions_user ON sessions(user_id);"
        "CREATE INDEX IF NOT EXISTS idx_logs_level ON logs(level);"
        "CREATE INDEX IF NOT EXISTS idx_logs_ts ON logs(timestamp);";

    char *err_msg = NULL;
    rc = sqlite3_exec(db, schema, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Schema creation failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    printf("[DB] Database initialized: %s\n", db_path);
    return 0;
}

void db_close(void) {
    if (db) {
        sqlite3_close(db);
        db = NULL;
        printf("[DB] Database closed.\n");
    }
}

/* ---- User Management ---- */

int db_add_user(const char *username, const char *password) {
    if (!db) return -1;

    char pwd_hash[64];
    hash_password(password, pwd_hash, sizeof(pwd_hash));

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO users (username, password_hash) VALUES (?, ?)";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Prepare error (add_user): %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pwd_hash, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        /* SQLITE_CONSTRAINT means duplicate username */
        if (rc == SQLITE_CONSTRAINT) {
            fprintf(stderr, "[DB] User '%s' already exists.\n", username);
        } else {
            fprintf(stderr, "[DB] Insert error (add_user): %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
        return -1;
    }

    int user_id = (int)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    printf("[DB] User created: %s (id=%d)\n", username, user_id);
    return user_id;
}

int db_auth_user(const char *username, const char *password) {
    if (!db) return -1;

    char pwd_hash[64];
    hash_password(password, pwd_hash, sizeof(pwd_hash));

    sqlite3_stmt *stmt;
    const char *sql = "SELECT id FROM users WHERE username = ? AND password_hash = ?";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Prepare error (auth_user): %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pwd_hash, -1, SQLITE_TRANSIENT);

    int user_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user_id = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return user_id;
}

/* ---- Session Management ---- */

int db_create_session(int user_id, char *token_out, int token_buf_size) {
    if (!db || token_buf_size < 33) return -1;

    generate_token(token_out, 33); /* 32 hex chars + null */

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO sessions (user_id, session_token, expires_at) "
                      "VALUES (?, ?, datetime('now', '+24 hours'))";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, token_out, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* ---- Location Storage ---- */

int db_add_location(int user_id, double latitude, double longitude) {
    if (!db) return -1;

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO locations (user_id, latitude, longitude) VALUES (?, ?, ?)";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Prepare error (add_location): %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_double(stmt, 2, latitude);
    sqlite3_bind_double(stmt, 3, longitude);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[DB] Insert error (add_location): %s\n", sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

/* ---- Logging ---- */

int db_add_log(const char *level, const char *message) {
    if (!db) return -1;

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO logs (level, message) VALUES (?, ?)";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, level, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, message, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* ---- Seed Data ---- */

void db_seed_users(void) {
    /* Create test users if they don't exist */
    db_add_user("user1", "pass1");
    db_add_user("user2", "pass2");
    db_add_user("user3", "pass3");
    db_add_user("admin", "admin123");
    printf("[DB] Seed users checked/created.\n");
}
