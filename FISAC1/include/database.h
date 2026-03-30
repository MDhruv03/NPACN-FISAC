/*
    database.h - SQLite3 database layer for the location sharing server.

    Provides functions for user authentication, location storage,
    session management, and event logging using an embedded SQLite3 database.
*/

#ifndef DATABASE_H
#define DATABASE_H

/* Initialize the database (creates tables if they don't exist) */
int db_init(const char *db_path);

/* Close the database */
void db_close(void);

/* Add a new user. Returns user_id on success, -1 on failure */
int db_add_user(const char *username, const char *password);

/* Authenticate a user. Returns user_id on success, -1 on failure */
int db_auth_user(const char *username, const char *password);

/* Create a session token for a user. Returns 0 on success */
int db_create_session(int user_id, char *token_out, int token_buf_size);

/* Add a location update. Returns 0 on success */
int db_add_location(int user_id, double latitude, double longitude);

/* Add a log entry. Returns 0 on success */
int db_add_log(const char *level, const char *message);

/* Seed default test users if they don't already exist */
void db_seed_users(void);

#endif /* DATABASE_H */
