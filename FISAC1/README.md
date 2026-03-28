# Real-Time Location Sharing Web Application

This project is a real-time location sharing web application with a C-based WebSocket server, a Python backend service, and a PostgreSQL database.

## Structure

```
.
├── FISAC1
│   ├── src/             # C Source files
│   ├── include/         # C Header files
│   ├── scripts/         # Python tests, service, and SQL schemas
│   ├── docs/            # Markdown documentation
│   ├── frontend/        # HTML, CSS, JS minimal frontend
│   ├── Makefile         # Build script
│   └── README.md
```

## How to Run

### 1. Set up the Database

1.  **Install PostgreSQL**: If you don't have it already, install PostgreSQL.
2.  **Create Database**: Create a new database named `fisac`.
    ```sql
    CREATE DATABASE fisac;
    ```
3.  **Create Tables**: Connect to the `fisac` database and run the `schema.sql` script to create the necessary tables.
    ```bash
    psql -d fisac -f scripts/schema.sql
    ```

### 2. Run the Python Service

1.  **Install Dependencies**:
    ```bash
    pip install Flask psycopg2-binary
    ```
2.  **Set Environment Variables** (optional, defaults are provided in the script):
    ```bash
    export DB_NAME="fisac"
    export DB_USER="your_postgres_user"
    export DB_PASSWORD="your_postgres_password"
    ```
3.  **Run the Service**:
    ```bash
    cd scripts
    python service.py
    ```
    The service will run on `http://localhost:5000`.

### 3. Compile and Run the C Server

1.  **Compile**:
    ```bash
    mingw32-make
    ```
    *(Alternatively, use `make` or run `gcc -Wall -Wextra -I./include -o server src/*.c -lm` if make is not available).*
2.  **Run**:
    ```bash
    ./server
    ```
    The server will run on `ws://localhost:8080`.

### 4. Use the Frontend

1.  **Open `frontend/index.html`**: Open the `index.html` file in a modern web browser.
2.  **Connect**: The frontend will automatically try to connect to the WebSocket server.
3.  **Share Location**: Click the "Share My Location" button to start sending your location to the server. The server will then broadcast your location to all other connected clients, and you will see the locations of other clients on the map.

## Running the Tests

### Load Test

```bash
pip install websockets
cd scripts
python load_test.py
```

### Chaos Test

```bash
pip install websockets
cd scripts
python chaos_test.py
```

### Log Analysis

```bash
pip install pandas psycopg2-binary
cd scripts
python log_analyzer.py
```
