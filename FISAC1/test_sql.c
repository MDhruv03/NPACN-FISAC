#include <sqlite3.h>
#include <stdio.h>

int main() {
    sqlite3 *db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc) {
        printf("Can't open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    printf("Successfully opened in-memory SQLite database!\n");
    sqlite3_close(db);
    return 0;
}
