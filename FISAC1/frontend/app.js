const STATE = {
    DISCONNECTED: 0,
    CONNECTING: 1,
    CONNECTED: 2
};

const UI = {
    dot: document.getElementById('connection-dot'),
    status: document.getElementById('connection-status'),
    toggleBtn: document.getElementById('toggle-location-btn'),
    map: document.getElementById('map-grid'),
    logs: document.getElementById('logs')
};

let ws = null;
let watcherId = null;
let state = STATE.DISCONNECTED;
let myId = 'USR_' + Math.floor(Math.random() * 10000);
let others = {};

// Default viewport for the "relative" grid map
// A simplistic view that centers around the first coordinate received
let mapCenter = null;
const mapScale = 10000; // Multiplier for lat/lon differences

function log(msg, type = 'system') {
    const el = document.createElement('div');
    el.className = `log-entry ${type}`;
    el.innerText = `[${new Date().toISOString().substring(11, 19)}] ${msg}`;
    UI.logs.appendChild(el);
    UI.logs.scrollTop = UI.logs.scrollHeight;
}

function updateState(newState) {
    state = newState;
    UI.dot.className = 'dot';
    
    switch(state) {
        case STATE.DISCONNECTED:
            UI.dot.classList.add('disconnected');
            UI.status.innerText = 'Disconnected';
            UI.toggleBtn.innerText = 'Connect & Share';
            UI.toggleBtn.classList.remove('active');
            stopSharing();
            break;
        case STATE.CONNECTING:
            UI.dot.classList.add('connecting');
            UI.status.innerText = 'Connecting...';
            break;
        case STATE.CONNECTED:
            UI.dot.classList.add('connected');
            UI.status.innerText = 'Synchronized';
            UI.toggleBtn.innerText = 'Stop Sharing & Disconnect';
            break;
    }
}

function initWebSocket() {
    updateState(STATE.CONNECTING);
    // Explicitly targeting the backend server mapping 127.0.0.1:8080 or localhost:8080
    ws = new WebSocket('ws://localhost:8080');

    ws.onopen = () => {
        updateState(STATE.CONNECTED);
        log('Connection established with relay.', 'system');
        
        // Protocol: Authenticate
        const authMsg = {
            type: 'auth',
            payload: { username: myId, password: 'x' }
        };
        ws.send(JSON.stringify(authMsg));
        log(`Sent AUTH: ${myId}`, 'tx');
        
        // Subscribe
        const subMsg = {
            type: 'subscribe',
            payload: { channel: 'locations' }
        };
        ws.send(JSON.stringify(subMsg));

        startSharing();
    };

    ws.onmessage = (e) => {
        try {
            const data = JSON.parse(e.data);
            if(data.type === 'location' && data.payload) {
                const { latitude, longitude, userId } = data.payload;
                const srcId = userId || 'Anonymous';
                log(`RECV: ${srcId} @ [${latitude.toFixed(4)}, ${longitude.toFixed(4)}]`, 'rx');
                updateMarker(srcId, latitude, longitude, false);
            }
        } catch (err) {
            log(`Parse error: ${err.message}`, 'err');
        }
    };

    ws.onerror = (e) => {
        log('Socket error occurred.', 'err');
    };

    ws.onclose = () => {
        updateState(STATE.DISCONNECTED);
        log('Connection terminated.', 'system');
        ws = null;
    };
}

function disconnectWebSocket() {
    if(ws) ws.close();
}

function startSharing() {
    if(!navigator.geolocation) {
        log('Geolocation not supported.', 'err');
        return;
    }

    watcherId = navigator.geolocation.watchPosition(
        (pos) => {
            const lat = pos.coords.latitude;
            const lon = pos.coords.longitude;
            
            if(state === STATE.CONNECTED && ws) {
                const msg = {
                    type: 'location',
                    payload: { latitude: lat, longitude: lon, userId: myId }
                };
                ws.send(JSON.stringify(msg));
                log(`TX: [${lat.toFixed(4)}, ${lon.toFixed(4)}]`, 'tx');
            }
            updateMarker('self', lat, lon, true);
        },
        (err) => {
            log(`Geoloc error: ${err.message}`, 'err');
        },
        { enableHighAccuracy: true, maximumAge: 1000 }
    );
}

function stopSharing() {
    if(watcherId !== null) {
        navigator.geolocation.clearWatch(watcherId);
        watcherId = null;
    }
}

function updateMarker(id, lat, lon, isSelf) {
    if(!mapCenter) mapCenter = { lat, lon };

    let el = document.getElementById('marker-' + id);
    if(!el) {
        el = document.createElement('div');
        el.id = 'marker-' + id;
        el.className = 'marker' + (isSelf ? ' self' : '');
        
        const label = document.createElement('div');
        label.className = 'id-label';
        label.innerText = isSelf ? 'You' : id;
        el.appendChild(label);
        
        UI.map.appendChild(el);
    }

    // Convert lat/lon offset to pixels relative to center
    const dx = (lon - mapCenter.lon) * mapScale;
    const dy = (mapCenter.lat - lat) * mapScale; // invert Y for screen coords

    // Center of the 300px Map Grid
    const cx = (UI.map.clientWidth / 2) + dx;
    const cy = (UI.map.clientHeight / 2) + dy;

    el.style.left = `${cx}px`;
    el.style.top = `${cy}px`;
}

UI.toggleBtn.addEventListener('click', () => {
    if(state === STATE.DISCONNECTED) {
        initWebSocket();
    } else {
        disconnectWebSocket();
    }
});

// Enable button out of startup
setTimeout(() => {
    UI.toggleBtn.disabled = false;
}, 500);
