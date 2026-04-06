import sqlite3
import os
import hashlib
import time
from flask import Flask, request, jsonify, send_from_directory

app = Flask(__name__, static_folder='../frontend', static_url_path='/')
DB_PATH = 'fisac.db'
START_TIME = time.time()

def hash_password(password):
    # Match the djb2 hash in database.c exactly.
    # On Windows 64-bit (LLP64), C's `unsigned long` is typically 32 bits.
    # We must truncate to 32 bits to match the C server's behavior.
    hash_val = 5381
    for c in password:
        hash_val = ((hash_val << 5) + hash_val) + ord(c)
        hash_val &= 0xFFFFFFFF  # 32-bit truncation per Windows unsigned long
    return f"{hash_val:016x}"

def get_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    # Enable WAL mode for better concurrency
    conn.execute('PRAGMA journal_mode=WAL')
    return conn

def init_db():
    # Do NOT drop tables — that would wipe all registered users on every restart.
    # Just ensure the schema exists and demo users are seeded.
    schema = """
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS locations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL REFERENCES users(id),
            latitude REAL NOT NULL,
            longitude REAL NOT NULL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            level TEXT NOT NULL,
            message TEXT NOT NULL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    """
    with get_db() as conn:
        conn.executescript(schema)
        # Seed demo users — INSERT OR IGNORE means existing accounts are preserved
        try:
            conn.execute("INSERT OR IGNORE INTO users (username, password_hash) VALUES (?, ?)", ("user1", hash_password("pass1")))
            conn.execute("INSERT OR IGNORE INTO users (username, password_hash) VALUES (?, ?)", ("user2", hash_password("pass2")))
        except sqlite3.Error:
            pass
        conn.commit()

@app.route('/')
def serve_index():
    return app.send_static_file('index.html')

@app.after_request
def force_close_connection(response):
    response.headers['Connection'] = 'close'
    return response

@app.route('/auth', methods=['POST'])
def auth():
    data = request.json
    username = data.get('username')
    password = data.get('password')
    
    if not username or not password:
        return jsonify({"success": False, "message": "Missing credentials"})
        
    pwd_hash = hash_password(password)
    
    with get_db() as conn:
        cur = conn.cursor()
        cur.execute("SELECT id FROM users WHERE username = ? AND password_hash = ?", (username, pwd_hash))
        row = cur.fetchone()
        
        if row:
            return jsonify({"success": True, "user_id": row['id']})
        return jsonify({"success": False, "message": "Invalid credentials"})

@app.route('/register', methods=['POST'])
def register():
    data = request.json
    username = data.get('username')
    password = data.get('password')
    
    if not username or not password:
        return jsonify({"success": False, "message": "Missing credentials"})
        
    pwd_hash = hash_password(password)
    
    with get_db() as conn:
        try:
            cur = conn.cursor()
            cur.execute("INSERT INTO users (username, password_hash) VALUES (?, ?)", (username, pwd_hash))
            conn.commit()
            return jsonify({"success": True, "user_id": cur.lastrowid})
        except sqlite3.IntegrityError:
            return jsonify({"success": False, "message": "Username already taken"})
        except Exception as e:
            return jsonify({"success": False, "message": str(e)})

@app.route('/location', methods=['POST'])
def location():
    data = request.json
    user_id = data.get('user_id')
    lat = data.get('latitude')
    lon = data.get('longitude')
    
    if not user_id or lat is None or lon is None:
        return jsonify({"success": False})
        
    with get_db() as conn:
        try:
            conn.execute("INSERT INTO locations (user_id, latitude, longitude) VALUES (?, ?, ?)", (user_id, lat, lon))
            conn.commit()
            return jsonify({"success": True})
        except:
            return jsonify({"success": False})

@app.route('/log', methods=['POST'])
def log_event():
    data = request.json
    level = data.get('level', 'INFO')
    message = data.get('message', '')
    
    if message:
        with get_db() as conn:
            try:
                conn.execute("INSERT INTO logs (level, message) VALUES (?, ?)", (level, message))
                conn.commit()
            except:
                pass
    return jsonify({"success": True})

@app.route('/stats', methods=['GET'])
def stats():
    try:
        with get_db() as conn:
            cur = conn.cursor()
            users = cur.execute("SELECT COUNT(*) AS c FROM users").fetchone()[0]
            locations = cur.execute("SELECT COUNT(*) AS c FROM locations").fetchone()[0]
            logs = cur.execute("SELECT COUNT(*) AS c FROM logs").fetchone()[0]
            last_location = cur.execute("SELECT MAX(timestamp) FROM locations").fetchone()[0]
            last_log = cur.execute("SELECT MAX(timestamp) FROM logs").fetchone()[0]

        return jsonify({
            "success": True,
            "socket_profile": os.getenv('FISAC_SOCKOPTS_PROFILE', 'unknown'),
            "service_uptime_s": int(max(0, time.time() - START_TIME)),
            "db": {
                "users": users,
                "locations": locations,
                "logs": logs,
                "last_location": last_location,
                "last_log": last_log,
            }
        })
    except Exception:
        return jsonify({"success": False, "message": "stats_unavailable"}), 500

if __name__ == '__main__':
    print("Initializing Database...")
    init_db()
    print("Starting Flask DB Backend on port 5000...")
    # Enable logging for debugging
    app.run(host='127.0.0.1', port=5000)
