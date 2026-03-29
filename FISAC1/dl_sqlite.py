import urllib.request
import zipfile
import io
import sys

URLS = [
    'https://www.sqlite.org/2025/sqlite-amalgamation-3510300.zip',
    'https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip',
    'https://github.com/nicedreams/sqlite-amalgamation/raw/master/sqlite3.zip',
    'https://raw.githubusercontent.com/nicedreams/sqlite-amalgamation/master/sqlite3.c'
]

def download():
    for url in URLS:
        print(f"Trying {url}...", flush=True)
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=15) as r:
                data = r.read()
                print(f"Downloaded {len(data)} bytes.", flush=True)
                
                if url.endswith('.zip'):
                    z = zipfile.ZipFile(io.BytesIO(data))
                    for f in z.namelist():
                        if f.endswith('sqlite3.c'):
                            with open('src/sqlite3.c', 'wb') as out:
                                out.write(z.read(f))
                            print(f"Extracted sqlite3.c", flush=True)
                        elif f.endswith('sqlite3.h'):
                            with open('include/sqlite3.h', 'wb') as out:
                                out.write(z.read(f))
                            print(f"Extracted sqlite3.h", flush=True)
                    return True
                else:
                    # Direct .c file
                    with open('src/sqlite3.c', 'wb') as out:
                        out.write(data)
                    
                    # Try to fetch .h
                    print(f"Fetching sqlite3.h...", flush=True)
                    h_url = url.replace('.c', '.h')
                    req2 = urllib.request.Request(h_url, headers={'User-Agent': 'Mozilla/5.0'})
                    with urllib.request.urlopen(req2, timeout=15) as r2:
                        with open('include/sqlite3.h', 'wb') as out:
                            out.write(r2.read())
                    return True
        except Exception as e:
            print(f"Failed: {e}", flush=True)
    return False

if download():
    print("SUCCESS", flush=True)
else:
    print("ALL DOWNLOADS FAILED", flush=True)
    sys.exit(1)
