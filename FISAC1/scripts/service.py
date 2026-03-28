# service.py

from flask import Flask, request, jsonify
import psycopg2
from psycopg2 import pool
import logging
import os

# --- Configuration ---
DB_NAME = os.environ.get("DB_NAME", "fisac")
DB_USER = os.environ.get("DB_USER", "postgres")
DB_PASSWORD = os.environ.get("DB_PASSWORD", "password")
DB_HOST = os.environ.get("DB_HOST", "localhost")
DB_PORT = os.environ.get("DB_PORT", "5432")

# --- Logging ---
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

# --- Database Connection Pool ---
try:
    db_pool = psycopg2.pool.SimpleConnectionPool(1, 10,
                                                 dbname=DB_NAME,
                                                 user=DB_USER,
                                                 password=DB_PASSWORD,
                                                 host=DB_HOST,
                                                 port=DB_PORT)
    logging.info("Database connection pool created successfully.")
except psycopg2.OperationalError as e:
    logging.error(f"Could not connect to database: {e}")
    db_pool = None

# --- Flask App ---
app = Flask(__name__)

def get_db_conn():
    if db_pool:
        return db_pool.getconn()
    return None

def put_db_conn(conn):
    if db_pool:
        db_pool.putconn(conn)

@app.route('/location', methods=['POST'])
def add_location():
    data = request.get_json()
    if not data:
        return jsonify({"error": "Invalid JSON"}), 400

    user_id = data.get('user_id')
    latitude = data.get('latitude')
    longitude = data.get('longitude')

    if not all([user_id, latitude, longitude]):
        return jsonify({"error": "Missing data"}), 400

    conn = get_db_conn()
    if not conn:
        return jsonify({"error": "Database connection failed"}), 500
        
    try:
        with conn.cursor() as cur:
            cur.execute(
                "INSERT INTO locations (user_id, latitude, longitude) VALUES (%s, %s, %s)",
                (user_id, latitude, longitude)
            )
            conn.commit()
            logging.info(f"Inserted location for user {user_id}")
        return jsonify({"message": "Location added"}), 201
    except Exception as e:
        logging.error(f"Database error: {e}")
        conn.rollback()
        return jsonify({"error": "Failed to add location"}), 500
    finally:
        put_db_conn(conn)

@app.route('/log', methods=['POST'])
def add_log():
    data = request.get_json()
    if not data:
        return jsonify({"error": "Invalid JSON"}), 400

    level = data.get('level')
    message = data.get('message')

    if not all([level, message]):
        return jsonify({"error": "Missing data"}), 400

    conn = get_db_conn()
    if not conn:
        return jsonify({"error": "Database connection failed"}), 500

    try:
        with conn.cursor() as cur:
            cur.execute(
                "INSERT INTO logs (level, message) VALUES (%s, %s)",
                (level, message)
            )
            conn.commit()
            logging.info(f"Log entry added: {level} - {message}")
        return jsonify({"message": "Log added"}), 201
    except Exception as e:
        logging.error(f"Database error: {e}")
        conn.rollback()
        return jsonify({"error": "Failed to add log"}), 500
    finally:
        put_db_conn(conn)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
