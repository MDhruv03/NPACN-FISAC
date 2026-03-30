/* ============================
   GeoSync — Client Application

/* ---- Global Error Capture ---- */
window.onerror = function(msg, url, lineNo, columnNo, error) {
    const string = msg.toLowerCase();
    const substring = "script error";
    if (string.indexOf(substring) > -1) {
        log('Global Error: Script Error (see browser console)', 'err');
    } else {
        const message = [
            'Message: ' + msg,
            'Line: ' + lineNo,
            'Column: ' + columnNo,
            'Error object: ' + JSON.stringify(error)
        ].join(' - ');
        log('JS ERROR: ' + msg + ' (Line: ' + lineNo + ')', 'err');
    }
    return false;
};

/* ---- State ---- */

const STATE = { DISCONNECTED: 0, CONNECTING: 1, CONNECTED: 2 };

let ws = null;
let state = STATE.DISCONNECTED;
let watcherId = null;
let map = null;
let myMarker = null;
let myUsername = '';
let myUserId = 0;
let authMode = 'login'; /* 'login' or 'register' */
let others = {};         /* userId -> { marker, lat, lon, username } */
let txCount = 0, rxCount = 0;
let connectTime = null;
let uptimeInterval = null;
let latencyStart = 0;
let authTimeoutId = null;

/* Color palette for user markers */
const USER_COLORS = [
    '#f87171', '#fb923c', '#fbbf24', '#a3e635',
    '#34d399', '#22d3ee', '#60a5fa', '#a78bfa',
    '#f472b6', '#e879f9'
];

function getColor(id) {
    let hash = 0;
    for (let i = 0; i < String(id).length; i++) {
        hash = String(id).charCodeAt(i) + ((hash << 5) - hash);
    }
    return USER_COLORS[Math.abs(hash) % USER_COLORS.length];
}

/* ---- DOM References ---- */

const UI = {
    authOverlay: document.getElementById('auth-overlay'),
    app: document.getElementById('app'),
    authForm: document.getElementById('auth-form'),
    authUsername: document.getElementById('auth-username'),
    authPassword: document.getElementById('auth-password'),
    authError: document.getElementById('auth-error'),
    authBtnText: document.getElementById('auth-btn-text'),
    authSpinner: document.getElementById('auth-spinner'),
    authSubmitBtn: document.getElementById('auth-submit-btn'),
    dot: document.getElementById('connection-dot'),
    status: document.getElementById('connection-status'),
    myLat: document.getElementById('my-lat'),
    myLon: document.getElementById('my-lon'),
    userList: document.getElementById('user-list'),
    userCount: document.getElementById('user-count'),
    statLatency: document.getElementById('stat-latency'),
    statTx: document.getElementById('stat-tx'),
    statRx: document.getElementById('stat-rx'),
    statUptime: document.getElementById('stat-uptime'),
    logs: document.getElementById('logs'),
    userBadge: document.getElementById('user-badge'),
    mapZoom: document.getElementById('map-zoom-level'),
};

/* ---- Logging ---- */

function log(msg, type = 'system') {
    const el = document.createElement('div');
    el.className = `log-entry ${type}`;
    const time = new Date().toISOString().substring(11, 19);
    el.textContent = `[${time}] ${msg}`;
    UI.logs.appendChild(el);
    UI.logs.scrollTop = UI.logs.scrollHeight;
    /* Keep log buffer small */
    while (UI.logs.children.length > 100) {
        UI.logs.removeChild(UI.logs.firstChild);
    }
}

/* ---- Auth Tab Switching ---- */

/* Global function for onclick */
window.switchTab = function(mode) {
    authMode = mode;
    document.getElementById('tab-login').classList.toggle('active', mode === 'login');
    document.getElementById('tab-register').classList.toggle('active', mode === 'register');
    UI.authBtnText.textContent = mode === 'login' ? 'Sign In' : 'Create Account';
    UI.authError.textContent = '';
};

/* ---- Auth Handling ---- */

window.handleAuth = function(e) {
    e.preventDefault();
    const username = UI.authUsername.value.trim();
    const password = UI.authPassword.value;

    if (!username || !password) {
        UI.authError.textContent = 'Please fill in all fields';
        return false;
    }

    UI.authError.textContent = '';
    UI.authBtnText.textContent = 'Connecting...';
    UI.authSpinner.style.display = 'block';
    UI.authSubmitBtn.disabled = true;

    /* Connect WebSocket, then send auth */
    connectAndAuth(username, password);
    return false;
};

function connectAndAuth(username, password) {
    if (ws) {
        ws.close();
        ws = null;
    }

    if (authTimeoutId) {
        clearTimeout(authTimeoutId);
        authTimeoutId = null;
    }

    updateConnectionState(STATE.CONNECTING);
    ws = new WebSocket('ws://localhost:8080');

    ws.onopen = () => {
        log('WebSocket connected. Sending auth request...', 'system');
        /* Send auth or register message */
        const msg = {
            type: authMode === 'register' ? 'register' : 'auth',
            payload: { username, password }
        };
        ws.send(JSON.stringify(msg));
        txCount++;
        log(`Sent ${msg.type.toUpperCase()} request to server.`, 'tx');

        /* Guard against hanging forever if auth_response never arrives. */
        authTimeoutId = setTimeout(() => {
            if (state !== STATE.CONNECTED) {
                UI.authError.textContent = 'Authentication timed out. Please retry.';
                log('Auth timeout: no auth_response from server.', 'err');
                if (ws && ws.readyState === WebSocket.OPEN) {
                    ws.close();
                }
                resetAuthButton();
                updateConnectionState(STATE.DISCONNECTED);
            }
        }, 10000);
    };

    ws.onmessage = (e) => {
        rxCount++;
        UI.statRx.textContent = rxCount;
        try {
            const data = JSON.parse(e.data);
            if (data && data.type === 'auth_response' && authTimeoutId) {
                clearTimeout(authTimeoutId);
                authTimeoutId = null;
            }
            handleServerMessage(data);
        } catch (err) {
            log(`Parse error: ${err.message}`, 'err');
        }
    };

    ws.onerror = (err) => {
        console.error('WS Error:', err);
        log('WebSocket error: Could not reach server.', 'err');
        UI.authError.textContent = 'Connection failed. Is the server running?';
        if (authTimeoutId) {
            clearTimeout(authTimeoutId);
            authTimeoutId = null;
        }
        resetAuthButton();
    };

    ws.onclose = (code) => {
        if (authTimeoutId) {
            clearTimeout(authTimeoutId);
            authTimeoutId = null;
        }
        updateConnectionState(STATE.DISCONNECTED);
        log(`Connection closed (code: ${code.code}).`, 'system');
        stopSharing();
        resetAuthButton(); // Ensure button is clickable again
        if (connectTime) {
            clearInterval(uptimeInterval);
            connectTime = null;
        }
    };
}

function resetAuthButton() {
    UI.authBtnText.textContent = authMode === 'login' ? 'Sign In' : 'Create Account';
    UI.authSpinner.style.display = 'none';
    UI.authSubmitBtn.disabled = false;
}

/* ---- Message Handling ---- */

function handleServerMessage(data) {
    switch (data.type) {
        case 'auth_response':
            log(`Received AUTH_RESPONSE from server.`, 'rx');
            handleAuthResponse(data.payload);
            break;
        case 'location':
            handleLocationUpdate(data.payload);
            break;
        case 'error':
            log(`Error: ${data.payload.message}`, 'err');
            break;
        default:
            log(`Unknown message type: ${data.type}`, 'warn');
    }
}

function handleAuthResponse(payload) {
    try {
        if (payload.success) {
            myUsername = payload.username;
            myUserId = payload.user_id;
            UI.authError.textContent = '';

            /* Transition to main app */
            UI.authOverlay.style.display = 'none';
            UI.app.style.display = 'flex';
            UI.userBadge.textContent = myUsername;

            updateConnectionState(STATE.CONNECTED);
            log(`Authenticated as ${myUsername}`, 'system');

            /* Initialize map */
            if (typeof L === 'undefined') {
                log('CRITICAL: Leaflet library (L) not loaded!', 'err');
                return;
            }
            initMap();

            /* Start sharing location */
            startSharing();

            /* Start uptime counter */
            connectTime = Date.now();
            uptimeInterval = setInterval(updateUptime, 1000);

            /* Subscribe to location channel */
            sendMessage({ type: 'subscribe', payload: { channel: 'locations' } });
        } else {
            UI.authError.textContent = payload.message || 'Authentication failed';
            resetAuthButton();
            log(`Auth failed: ${payload.message}`, 'err');
        }
    } catch (err) {
        log(`Transition Error: ${err.message}`, 'err');
        console.error(err);
        resetAuthButton();
    }
}

function handleLocationUpdate(payload) {
    if (!payload) return;
    const { latitude, longitude, userId } = payload;
    const srcId = userId || 'Unknown';

    if (srcId === myUsername) return; /* Ignore own broadcasts */

    log(`RX: ${srcId} @ [${latitude.toFixed(4)}, ${longitude.toFixed(4)}]`, 'rx');
    updateOtherUser(srcId, latitude, longitude);
}

/* ---- Connection State ---- */

function updateConnectionState(newState) {
    state = newState;
    UI.dot.className = 'status-dot';
    switch (state) {
        case STATE.DISCONNECTED:
            UI.dot.classList.add('disconnected');
            UI.status.textContent = 'Offline';
            break;
        case STATE.CONNECTING:
            UI.dot.classList.add('connecting');
            UI.status.textContent = 'Connecting';
            break;
        case STATE.CONNECTED:
            UI.dot.classList.add('connected');
            UI.status.textContent = 'Live';
            break;
    }
}

/* ---- Map ---- */

function initMap() {
    if (map) return;
    map = L.map('map', {
        center: [20, 0],
        zoom: 3,
        zoomControl: true,
        attributionControl: true,
    });

    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        maxZoom: 19,
        attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OSM</a>'
    }).addTo(map);

    map.on('zoomend', () => {
        UI.mapZoom.textContent = `Zoom: ${map.getZoom()}`;
    });
    UI.mapZoom.textContent = `Zoom: ${map.getZoom()}`;
}

function createPulseIcon(color, isSelf) {
    const size = isSelf ? 16 : 12;
    const pulseSize = isSelf ? 40 : 30;
    return L.divIcon({
        className: 'custom-marker',
        html: `
            <div style="position:relative;width:${pulseSize}px;height:${pulseSize}px;display:flex;align-items:center;justify-content:center;">
                <div style="position:absolute;width:${pulseSize}px;height:${pulseSize}px;border-radius:50%;background:${color};opacity:0.2;animation:markerPulse 2s ease infinite;"></div>
                <div style="width:${size}px;height:${size}px;border-radius:50%;background:${color};border:2px solid rgba(255,255,255,0.8);box-shadow:0 0 10px ${color}40;z-index:10;"></div>
            </div>
        `,
        iconSize: [pulseSize, pulseSize],
        iconAnchor: [pulseSize / 2, pulseSize / 2],
    });
}

/* Inject marker animation CSS */
const markerStyle = document.createElement('style');
markerStyle.textContent = `
    @keyframes markerPulse {
        0%, 100% { transform: scale(0.8); opacity: 0.2; }
        50% { transform: scale(1.3); opacity: 0.05; }
    }
    .custom-marker { background: none !important; border: none !important; }
`;
document.head.appendChild(markerStyle);

function updateMyLocation(lat, lon) {
    UI.myLat.textContent = lat.toFixed(6);
    UI.myLon.textContent = lon.toFixed(6);

    if (!map) return;

    if (!myMarker) {
        myMarker = L.marker([lat, lon], {
            icon: createPulseIcon('#34d399', true),
            zIndexOffset: 1000,
        }).addTo(map);
        myMarker.bindTooltip('You', { permanent: true, direction: 'top', className: 'marker-tooltip' });
        map.setView([lat, lon], 15);
    } else {
        myMarker.setLatLng([lat, lon]);
    }
}

function updateOtherUser(userId, lat, lon) {
    if (!map) return;

    if (!others[userId]) {
        const color = getColor(userId);
        const marker = L.marker([lat, lon], {
            icon: createPulseIcon(color, false),
        }).addTo(map);
        marker.bindTooltip(String(userId), { permanent: true, direction: 'top', className: 'marker-tooltip' });

        others[userId] = { marker, lat, lon, username: userId, color };
    } else {
        others[userId].marker.setLatLng([lat, lon]);
        others[userId].lat = lat;
        others[userId].lon = lon;
    }

    updateUserList();
}

function updateUserList() {
    const keys = Object.keys(others);
    UI.userCount.textContent = keys.length;

    if (keys.length === 0) {
        UI.userList.innerHTML = '<div class="empty-state">No other users online</div>';
        return;
    }

    UI.userList.innerHTML = keys.map(id => {
        const u = others[id];
        return `
            <div class="user-item" onclick="flyToUser('${id}')">
                <div class="user-dot" style="background:${u.color}"></div>
                <span>${u.username}</span>
                <span class="user-coords">${u.lat.toFixed(2)}, ${u.lon.toFixed(2)}</span>
            </div>
        `;
    }).join('');
}

window.flyToUser = function(id) {
    if (others[id] && map) {
        map.flyTo([others[id].lat, others[id].lon], 15, { duration: 1 });
    }
};

/* ---- Location Sharing ---- */

function startSharing() {
    if (!navigator.geolocation) {
        log('Geolocation not supported by browser.', 'err');
        return;
    }

    watcherId = navigator.geolocation.watchPosition(
        (pos) => {
            const lat = pos.coords.latitude;
            const lon = pos.coords.longitude;

            updateMyLocation(lat, lon);

            if (state === STATE.CONNECTED && ws && ws.readyState === WebSocket.OPEN) {
                const msg = {
                    type: 'location',
                    payload: { latitude: lat, longitude: lon, userId: myUsername }
                };
                ws.send(JSON.stringify(msg));
                txCount++;
                UI.statTx.textContent = txCount;
            }
        },
        (err) => {
            log(`Geolocation error: ${err.message}`, 'err');
            /* Fallback: use a simulated position for demo */
            const fakeLat = 28.6139 + (Math.random() - 0.5) * 0.01;
            const fakeLon = 77.2090 + (Math.random() - 0.5) * 0.01;
            updateMyLocation(fakeLat, fakeLon);
            if (state === STATE.CONNECTED && ws && ws.readyState === WebSocket.OPEN) {
                const msg = {
                    type: 'location',
                    payload: { latitude: fakeLat, longitude: fakeLon, userId: myUsername }
                };
                ws.send(JSON.stringify(msg));
                txCount++;
                UI.statTx.textContent = txCount;
            }
        },
        { enableHighAccuracy: true, maximumAge: 2000, timeout: 10000 }
    );

    log('Location sharing started.', 'system');
}

function stopSharing() {
    if (watcherId !== null) {
        navigator.geolocation.clearWatch(watcherId);
        watcherId = null;
    }
}

/* ---- Utility ---- */

function sendMessage(msg) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(msg));
        txCount++;
        UI.statTx.textContent = txCount;
    }
}

function updateUptime() {
    if (!connectTime) return;
    const seconds = Math.floor((Date.now() - connectTime) / 1000);
    if (seconds < 60) {
        UI.statUptime.textContent = `${seconds}s`;
    } else if (seconds < 3600) {
        UI.statUptime.textContent = `${Math.floor(seconds / 60)}m ${seconds % 60}s`;
    } else {
        const h = Math.floor(seconds / 3600);
        const m = Math.floor((seconds % 3600) / 60);
        UI.statUptime.textContent = `${h}h ${m}m`;
    }
}

window.recenterMap = function() {
    if (myMarker && map) {
        map.flyTo(myMarker.getLatLng(), 15, { duration: 0.5 });
    }
};

window.disconnect = function() {
    if (ws) ws.close();
    stopSharing();

    /* Clear state */
    others = {};
    txCount = 0;
    rxCount = 0;
    myMarker = null;
    if (map) {
        map.remove();
        map = null;
    }

    /* Show auth screen */
    UI.app.style.display = 'none';
    UI.authOverlay.style.display = 'flex';
    resetAuthButton();
    UI.authError.textContent = '';

    log('Disconnected.', 'system');
};

/* ---- Tooltip styling injection ---- */
const tooltipStyle = document.createElement('style');
tooltipStyle.textContent = `
    .marker-tooltip {
        background: rgba(18, 19, 26, 0.9) !important;
        border: 1px solid rgba(255,255,255,0.1) !important;
        color: #e8eaed !important;
        font-family: 'Inter', sans-serif !important;
        font-size: 11px !important;
        font-weight: 500 !important;
        padding: 3px 8px !important;
        border-radius: 6px !important;
        box-shadow: 0 4px 12px rgba(0,0,0,0.4) !important;
    }
    .marker-tooltip::before {
        border-top-color: rgba(18, 19, 26, 0.9) !important;
    }
`;
document.head.appendChild(tooltipStyle);
