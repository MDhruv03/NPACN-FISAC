/* ============================
   GeoSync — Client Application
    ============================ */

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
let eventCount = 0;
let peakPeers = 0;
let totalDistanceKm = 0;
let myTrail = [];
let myTrailPolyline = null;
let updateTimestamps = [];
let demoRouteEnabled = false;
let demoRouteTimer = null;
let demoRouteAngle = 0;
let demoRouteCenter = { lat: 28.6139, lng: 77.2090 };
let reconnectCount = 0;
let authFailureCount = 0;
let malformedSentCount = 0;
let geolocationFallbackCount = 0;
let burstRunCount = 0;
let broadcastRxCount = 0;
let parseErrorCount = 0;
let lastAuthUsername = '';
let lastAuthPassword = '';
let manualSignOut = false;
let backendStatsTimer = null;
let activeSocketProfile = 'unknown';

const workflowState = {
    connected: false,
    authSent: false,
    authAccepted: false,
    subscribed: false,
    streaming: false,
};

const backendStats = {
    online: false,
    users: 0,
    locations: 0,
    logs: 0,
    lastSync: 'Never',
};

const initialProfile = (new URLSearchParams(window.location.search).get('profile') || '').trim().toLowerCase();
if (initialProfile) {
    activeSocketProfile = initialProfile;
}

const peerTrails = {};

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
    qaQ1State: document.getElementById('qa-q1-state'),
    qaQ2State: document.getElementById('qa-q2-state'),
    qaQ3State: document.getElementById('qa-q3-state'),
    qaQ4State: document.getElementById('qa-q4-state'),
    wfConnect: document.getElementById('wf-connect-state'),
    wfAuthSend: document.getElementById('wf-auth-send-state'),
    wfAuthOk: document.getElementById('wf-auth-ok-state'),
    wfSubscribe: document.getElementById('wf-sub-state'),
    wfStream: document.getElementById('wf-stream-state'),
    metricDistance: document.getElementById('metric-distance'),
    metricRate: document.getElementById('metric-rate'),
    metricPeakPeers: document.getElementById('metric-peak-peers'),
    metricEvents: document.getElementById('metric-events'),
    backendState: document.getElementById('backend-state'),
    backendProfile: document.getElementById('backend-profile'),
    backendUsers: document.getElementById('backend-users'),
    backendLocations: document.getElementById('backend-locations'),
    backendLogs: document.getElementById('backend-logs'),
    backendSync: document.getElementById('backend-sync'),
    robustReconnects: document.getElementById('robust-reconnects'),
    robustAuthFail: document.getElementById('robust-auth-fail'),
    robustMalformed: document.getElementById('robust-malformed'),
    robustGeoFallback: document.getElementById('robust-geo-fallback'),
    protoTx: document.getElementById('proto-tx'),
    protoRx: document.getElementById('proto-rx'),
    btnDemoRoute: document.getElementById('btn-demo-route'),
    logs: document.getElementById('logs'),
    userBadge: document.getElementById('user-badge'),
    mapZoom: document.getElementById('map-zoom-level'),
    mapPeerCount: document.getElementById('map-peer-count'),
    mapDistance: document.getElementById('map-distance'),
    mapProfile: document.getElementById('map-profile'),
};

eventCount = UI.logs ? UI.logs.children.length : 0;

function prettyJson(data) {
    if (typeof data === 'string') {
        return data;
    }
    try {
        return JSON.stringify(data, null, 2);
    } catch (_err) {
        return String(data);
    }
}

function updateProtocolTrace(direction, data) {
    const target = direction === 'tx' ? UI.protoTx : UI.protoRx;
    if (!target) return;
    target.textContent = prettyJson(data);
}

function updateDemoRouteButton() {
    if (!UI.btnDemoRoute) return;
    UI.btnDemoRoute.textContent = demoRouteEnabled ? 'Stop Demo Route' : 'Start Demo Route';
    UI.btnDemoRoute.classList.toggle('active', demoRouteEnabled);
}

function setStateBadge(element, text, stateClass) {
    if (!element) return;
    element.textContent = text;
    element.classList.remove('pending', 'good', 'warn', 'done');
    if (stateClass) {
        element.classList.add(stateClass);
    }
}

function profileLabel(profile) {
    const normalized = (profile || 'unknown').toLowerCase();
    if (normalized === 'tuned') return 'Tuned';
    if (normalized === 'baseline') return 'Baseline';
    return 'Unknown';
}

function updateWorkflowIndicators() {
    setStateBadge(UI.wfConnect, workflowState.connected ? 'Done' : 'Pending', workflowState.connected ? 'done' : 'pending');
    setStateBadge(UI.wfAuthSend, workflowState.authSent ? 'Done' : 'Pending', workflowState.authSent ? 'done' : 'pending');
    setStateBadge(UI.wfAuthOk, workflowState.authAccepted ? 'Done' : 'Pending', workflowState.authAccepted ? 'done' : 'pending');
    setStateBadge(UI.wfSubscribe, workflowState.subscribed ? 'Done' : 'Pending', workflowState.subscribed ? 'done' : 'pending');
    setStateBadge(UI.wfStream, workflowState.streaming ? 'Done' : 'Pending', workflowState.streaming ? 'done' : 'pending');
}

function markWorkflowStep(stepKey, isDone) {
    if (!(stepKey in workflowState)) return;
    workflowState[stepKey] = !!isDone;
    updateWorkflowIndicators();
}

function resetWorkflowState() {
    Object.keys(workflowState).forEach((key) => {
        workflowState[key] = false;
    });
    updateWorkflowIndicators();
}

function updateQuestionAlignment() {
    const workflowDoneCount = Object.values(workflowState).filter(Boolean).length;
    const q1Ready = workflowDoneCount === 5;
    setStateBadge(UI.qaQ1State, q1Ready ? 'Ready' : `${workflowDoneCount}/5`, q1Ready ? 'good' : 'pending');

    const q2Ready = backendStats.online && backendStats.users > 0 && backendStats.locations > 0 && backendStats.logs > 0;
    const q2Label = q2Ready ? `Ready (${broadcastRxCount} RX)` : (backendStats.online ? 'In Progress' : 'Waiting');
    setStateBadge(UI.qaQ2State, q2Label, q2Ready ? 'good' : (backendStats.online ? 'warn' : 'pending'));

    const profileText = profileLabel(activeSocketProfile);
    const q3Class = profileText === 'Unknown' ? 'pending' : (profileText === 'Tuned' ? 'good' : 'warn');
    setStateBadge(UI.qaQ3State, profileText, q3Class);

    const robustnessScore = reconnectCount + malformedSentCount + geolocationFallbackCount + burstRunCount;
    const q4Label = robustnessScore > 0 ? `Active (${robustnessScore})` : 'Pending';
    setStateBadge(UI.qaQ4State, q4Label, robustnessScore > 0 ? 'good' : 'pending');
}

function updateBackendEvidenceUI() {
    if (UI.backendState) UI.backendState.textContent = backendStats.online ? 'Online' : 'Offline';
    if (UI.backendUsers) UI.backendUsers.textContent = `${backendStats.users}`;
    if (UI.backendLocations) UI.backendLocations.textContent = `${backendStats.locations}`;
    if (UI.backendLogs) UI.backendLogs.textContent = `${backendStats.logs}`;
    if (UI.backendSync) UI.backendSync.textContent = backendStats.lastSync;

    const profileText = profileLabel(activeSocketProfile);
    if (UI.backendProfile) UI.backendProfile.textContent = profileText;
    if (UI.mapProfile) UI.mapProfile.textContent = `Profile: ${profileText.toLowerCase()}`;

    updateQuestionAlignment();
}

async function fetchBackendStats() {
    try {
        const response = await fetch('/stats', { cache: 'no-store' });
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const payload = await response.json();
        if (payload && payload.success) {
            backendStats.online = true;
            backendStats.users = Number(payload.db?.users || 0);
            backendStats.locations = Number(payload.db?.locations || 0);
            backendStats.logs = Number(payload.db?.logs || 0);
            backendStats.lastSync = new Date().toISOString().substring(11, 19);

            const profile = String(payload.socket_profile || '').trim().toLowerCase();
            if (profile) {
                activeSocketProfile = profile;
            }
        }
    } catch (_error) {
        backendStats.online = false;
    }

    updateBackendEvidenceUI();
}

function startBackendStatsPolling() {
    if (backendStatsTimer) {
        clearInterval(backendStatsTimer);
    }
    fetchBackendStats();
    backendStatsTimer = setInterval(fetchBackendStats, 4000);
}

function stopBackendStatsPolling() {
    if (backendStatsTimer) {
        clearInterval(backendStatsTimer);
        backendStatsTimer = null;
    }
}

/* ---- Logging ---- */

function log(msg, type = 'system') {
    const el = document.createElement('div');
    el.className = `log-entry ${type}`;
    const time = new Date().toISOString().substring(11, 19);
    el.textContent = `[${time}] ${msg}`;
    UI.logs.appendChild(el);
    eventCount++;
    UI.logs.scrollTop = UI.logs.scrollHeight;
    /* Keep log buffer small */
    while (UI.logs.children.length > 100) {
        UI.logs.removeChild(UI.logs.firstChild);
    }
    updatePresentationMetrics();
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

    lastAuthUsername = username;
    lastAuthPassword = password;
    manualSignOut = false;

    /* Connect WebSocket, then send auth */
    connectAndAuth(username, password);
    return false;
};

function connectAndAuth(username, password) {
    resetWorkflowState();

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
        markWorkflowStep('connected', true);
        log('WebSocket connected. Sending auth request...', 'system');
        /* Send auth or register message */
        const msg = {
            type: authMode === 'register' ? 'register' : 'auth',
            payload: { username, password }
        };
        sendWsPayload(msg);
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
            updateProtocolTrace('rx', data);
            if (data && data.type === 'auth_response' && authTimeoutId) {
                clearTimeout(authTimeoutId);
                authTimeoutId = null;
            }
            handleServerMessage(data);
            updatePresentationMetrics();
        } catch (err) {
            parseErrorCount++;
            log(`Parse error: ${err.message}`, 'err');
            updatePresentationMetrics();
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
        const wasConnected = state === STATE.CONNECTED;
        if (authTimeoutId) {
            clearTimeout(authTimeoutId);
            authTimeoutId = null;
        }
        if (wasConnected && !manualSignOut) {
            reconnectCount++;
        }
        manualSignOut = false;
        updateConnectionState(STATE.DISCONNECTED);
        log(`Connection closed (code: ${code.code}).`, 'system');
        stopDemoRoute(false, true);
        stopSharing();
        resetAuthButton(); // Ensure button is clickable again
        if (connectTime) {
            clearInterval(uptimeInterval);
            connectTime = null;
        }
        updatePresentationMetrics();
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
        if (latencyStart > 0) {
            const authLatency = Math.max(1, Math.round(performance.now() - latencyStart));
            UI.statLatency.textContent = `${authLatency}ms`;
            latencyStart = 0;
        }

        if (payload.success) {
            myUsername = payload.username;
            myUserId = payload.user_id;
            UI.authError.textContent = '';
            markWorkflowStep('authAccepted', true);

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
            markWorkflowStep('subscribed', true);
            startBackendStatsPolling();
            updatePresentationMetrics();
        } else {
            UI.authError.textContent = payload.message || 'Authentication failed';
            resetAuthButton();
            authFailureCount++;
            log(`Auth failed: ${payload.message}`, 'err');
            updatePresentationMetrics();
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
    const lat = Number(latitude);
    const lon = Number(longitude);

    if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
        return;
    }

    if (srcId === myUsername) return; /* Ignore own broadcasts */

    broadcastRxCount++;
    log(`RX: ${srcId} @ [${lat.toFixed(4)}, ${lon.toFixed(4)}]`, 'rx');
    updateOtherUser(srcId, lat, lon);
}

/* ---- Connection State ---- */

function updateConnectionState(newState) {
    state = newState;
    UI.dot.className = 'status-dot';
    switch (state) {
        case STATE.DISCONNECTED:
            UI.dot.classList.add('disconnected');
            UI.status.textContent = 'Offline';
            markWorkflowStep('connected', false);
            break;
        case STATE.CONNECTING:
            UI.dot.classList.add('connecting');
            UI.status.textContent = 'Connecting';
            markWorkflowStep('connected', false);
            break;
        case STATE.CONNECTED:
            UI.dot.classList.add('connected');
            UI.status.textContent = 'Live';
            markWorkflowStep('connected', true);
            break;
    }
}

function haversineKm(lat1, lon1, lat2, lon2) {
    const toRad = (value) => (value * Math.PI) / 180;
    const dLat = toRad(lat2 - lat1);
    const dLon = toRad(lon2 - lon1);
    const a = Math.sin(dLat / 2) ** 2
        + Math.cos(toRad(lat1)) * Math.cos(toRad(lat2)) * Math.sin(dLon / 2) ** 2;
    const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
    return 6371 * c;
}

function pruneUpdateWindow() {
    const cutoff = Date.now() - 60000;
    while (updateTimestamps.length > 0 && updateTimestamps[0] < cutoff) {
        updateTimestamps.shift();
    }
}

function updatePresentationMetrics() {
    pruneUpdateWindow();

    if (UI.metricDistance) UI.metricDistance.textContent = `${totalDistanceKm.toFixed(2)} km`;
    if (UI.metricRate) UI.metricRate.textContent = `${updateTimestamps.length}`;
    if (UI.metricPeakPeers) UI.metricPeakPeers.textContent = `${peakPeers}`;
    if (UI.metricEvents) UI.metricEvents.textContent = `${eventCount}`;
    if (UI.robustReconnects) UI.robustReconnects.textContent = `${reconnectCount}`;
    if (UI.robustAuthFail) UI.robustAuthFail.textContent = `${authFailureCount}`;
    if (UI.robustMalformed) UI.robustMalformed.textContent = `${malformedSentCount}`;
    if (UI.robustGeoFallback) UI.robustGeoFallback.textContent = `${geolocationFallbackCount}`;

    const peerCount = Object.keys(others).length;
    if (UI.mapPeerCount) UI.mapPeerCount.textContent = `Peers: ${peerCount}`;
    if (UI.mapDistance) UI.mapDistance.textContent = `Distance: ${totalDistanceKm.toFixed(2)} km`;
    if (UI.mapProfile) UI.mapProfile.textContent = `Profile: ${profileLabel(activeSocketProfile).toLowerCase()}`;

    updateQuestionAlignment();
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
        updatePresentationMetrics();
    });
    UI.mapZoom.textContent = `Zoom: ${map.getZoom()}`;
    updatePresentationMetrics();
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

    updateTimestamps.push(Date.now());

    if (myTrail.length > 0) {
        const [prevLat, prevLon] = myTrail[myTrail.length - 1];
        const segmentKm = haversineKm(prevLat, prevLon, lat, lon);
        if (segmentKm > 0.002 && segmentKm < 5) {
            totalDistanceKm += segmentKm;
        }
    }

    myTrail.push([lat, lon]);
    if (myTrail.length > 260) {
        myTrail.shift();
    }

    if (!map) {
        updatePresentationMetrics();
        return;
    }

    if (!myTrailPolyline) {
        myTrailPolyline = L.polyline(myTrail, {
            color: '#34d399',
            weight: 3,
            opacity: 0.72,
            lineCap: 'round'
        }).addTo(map);
    } else {
        myTrailPolyline.setLatLngs(myTrail);
    }

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

    updatePresentationMetrics();
}

function updateOtherUser(userId, lat, lon) {
    if (!map) return;

    if (!others[userId]) {
        const color = getColor(userId);
        const marker = L.marker([lat, lon], {
            icon: createPulseIcon(color, false),
        }).addTo(map);
        marker.bindTooltip(String(userId), { permanent: true, direction: 'top', className: 'marker-tooltip' });

        others[userId] = { marker, lat, lon, username: userId, color, trail: [[lat, lon]] };
        peerTrails[userId] = L.polyline(others[userId].trail, {
            color,
            weight: 2,
            opacity: 0.58,
            dashArray: '5 7',
            lineCap: 'round'
        }).addTo(map);
    } else {
        others[userId].marker.setLatLng([lat, lon]);
        others[userId].lat = lat;
        others[userId].lon = lon;
        others[userId].trail.push([lat, lon]);
        if (others[userId].trail.length > 160) {
            others[userId].trail.shift();
        }
        if (peerTrails[userId]) {
            peerTrails[userId].setLatLngs(others[userId].trail);
        }
    }

    peakPeers = Math.max(peakPeers, Object.keys(others).length);
    updateUserList();
    updatePresentationMetrics();
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

function emitOwnLocation(lat, lon) {
    updateMyLocation(lat, lon);
    markWorkflowStep('streaming', true);

    if (state === STATE.CONNECTED && ws && ws.readyState === WebSocket.OPEN) {
        sendWsPayload({
            type: 'location',
            payload: { latitude: lat, longitude: lon, userId: myUsername }
        });
    }
}

function startSharing() {
    if (demoRouteEnabled) {
        return;
    }

    if (!navigator.geolocation) {
        log('Geolocation not supported by browser.', 'err');
        return;
    }

    watcherId = navigator.geolocation.watchPosition(
        (pos) => {
            const lat = pos.coords.latitude;
            const lon = pos.coords.longitude;
            emitOwnLocation(lat, lon);
        },
        (err) => {
            log(`Geolocation error: ${err.message}`, 'err');
            geolocationFallbackCount++;
            /* Fallback: use a simulated position for demo */
            const fakeLat = 28.6139 + (Math.random() - 0.5) * 0.01;
            const fakeLon = 77.2090 + (Math.random() - 0.5) * 0.01;
            emitOwnLocation(fakeLat, fakeLon);
            updatePresentationMetrics();
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

function sendWsPayload(msg) {
    if (!(ws && ws.readyState === WebSocket.OPEN)) {
        return false;
    }

    ws.send(JSON.stringify(msg));
    txCount++;
    UI.statTx.textContent = txCount;
    updateProtocolTrace('tx', msg);

    if (msg.type === 'auth' || msg.type === 'register') {
        latencyStart = performance.now();
        markWorkflowStep('authSent', true);
    }

    if (msg.type === 'subscribe') {
        markWorkflowStep('subscribed', true);
    }

    updatePresentationMetrics();
    return true;
}

function sendMessage(msg) {
    sendWsPayload(msg);
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

function startDemoRoute() {
    if (demoRouteEnabled) {
        return;
    }

    if (state !== STATE.CONNECTED) {
        log('Connect first before starting Demo Route.', 'warn');
        return;
    }
    if (!map) {
        log('Map not ready yet. Try again in a second.', 'warn');
        return;
    }

    stopSharing();
    demoRouteEnabled = true;
    demoRouteAngle = 0;

    if (myMarker) {
        const pos = myMarker.getLatLng();
        demoRouteCenter = { lat: pos.lat, lng: pos.lng };
    }

    demoRouteTimer = setInterval(() => {
        demoRouteAngle = (demoRouteAngle + 14) % 360;
        const radians = (demoRouteAngle * Math.PI) / 180;
        const radius = 0.0024;
        const lat = demoRouteCenter.lat + Math.cos(radians) * radius;
        const lon = demoRouteCenter.lng + Math.sin(radians) * (radius * 1.35);
        emitOwnLocation(lat, lon);
    }, 1200);

    updateDemoRouteButton();
    log('Demo route started (simulated orbit movement).', 'system');
}

function stopDemoRoute(resumeGeo = true, silent = false) {
    const wasEnabled = demoRouteEnabled;
    demoRouteEnabled = false;

    if (demoRouteTimer) {
        clearInterval(demoRouteTimer);
        demoRouteTimer = null;
    }

    updateDemoRouteButton();

    if (wasEnabled && !silent) {
        log('Demo route stopped.', 'system');
    }

    if (resumeGeo && state === STATE.CONNECTED) {
        startSharing();
    }
}

window.toggleDemoRoute = function() {
    if (demoRouteEnabled) {
        stopDemoRoute(true, false);
    } else {
        startDemoRoute();
    }
};

window.runBurstTest = function() {
    if (!(ws && ws.readyState === WebSocket.OPEN) || state !== STATE.CONNECTED) {
        log('Connect and authenticate before running burst test.', 'warn');
        return;
    }

    const anchor = myMarker ? myMarker.getLatLng() : { lat: demoRouteCenter.lat, lng: demoRouteCenter.lng };
    burstRunCount++;

    for (let i = 0; i < 25; i++) {
        const offset = (i + 1) * 0.00003;
        const lat = anchor.lat + (Math.sin(i * 0.6) * offset);
        const lon = anchor.lng + (Math.cos(i * 0.6) * offset);
        sendWsPayload({
            type: 'location',
            payload: { latitude: lat, longitude: lon, userId: myUsername }
        });
    }

    log('Burst test sent 25 location frames.', 'system');
    updatePresentationMetrics();
};

window.sendMalformedPayload = function() {
    if (!(ws && ws.readyState === WebSocket.OPEN)) {
        log('Open a live connection before sending malformed payload.', 'warn');
        return;
    }

    ws.send('{"type":"location","payload":');
    malformedSentCount++;
    log('Malformed payload injected for robustness validation.', 'warn');
    updatePresentationMetrics();
};

window.runReconnectDrill = function() {
    if (!lastAuthUsername || !lastAuthPassword) {
        log('Sign in first so reconnect drill can reuse your credentials.', 'warn');
        return;
    }

    log('Reconnect drill started: cycling socket and re-authenticating.', 'system');
    manualSignOut = false;

    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.close();
        setTimeout(() => {
            connectAndAuth(lastAuthUsername, lastAuthPassword);
        }, 900);
    } else {
        connectAndAuth(lastAuthUsername, lastAuthPassword);
    }
};

window.clearTrails = function() {
    myTrail = [];
    totalDistanceKm = 0;

    if (myTrailPolyline) {
        myTrailPolyline.setLatLngs([]);
    }

    for (const id of Object.keys(others)) {
        if (others[id].trail) {
            others[id].trail = [];
        }
        if (peerTrails[id]) {
            peerTrails[id].setLatLngs([]);
        }
    }

    updatePresentationMetrics();
    log('Movement trails cleared.', 'system');
};

window.exportSessionSnapshot = function() {
    const peers = Object.keys(others).map((id) => ({
        id,
        latitude: others[id].lat,
        longitude: others[id].lon
    }));

    const snapshot = {
        generatedAt: new Date().toISOString(),
        user: myUsername || null,
        connected: state === STATE.CONNECTED,
        metrics: {
            tx: txCount,
            rx: rxCount,
            uptime: UI.statUptime ? UI.statUptime.textContent : '0s',
            distanceKm: Number(totalDistanceKm.toFixed(3)),
            updatesPerMinute: updateTimestamps.length,
            peakPeers,
            sessionEvents: eventCount,
            reconnects: reconnectCount,
            authFailures: authFailureCount,
            malformedSent: malformedSentCount,
            geolocationFallbacks: geolocationFallbackCount,
            burstRuns: burstRunCount,
            parseErrors: parseErrorCount,
            broadcastRx: broadcastRxCount,
            socketProfile: profileLabel(activeSocketProfile)
        },
        backendEvidence: {
            online: backendStats.online,
            users: backendStats.users,
            locations: backendStats.locations,
            logs: backendStats.logs,
            lastSync: backendStats.lastSync
        },
        peers,
        protocol: {
            lastTx: UI.protoTx ? UI.protoTx.textContent : null,
            lastRx: UI.protoRx ? UI.protoRx.textContent : null
        }
    };

    const blob = new Blob([JSON.stringify(snapshot, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = `geosync-session-${Date.now()}.json`;
    anchor.click();
    URL.revokeObjectURL(url);

    log('Session snapshot exported.', 'system');
};

window.recenterMap = function() {
    if (myMarker && map) {
        map.flyTo(myMarker.getLatLng(), 15, { duration: 0.5 });
    }
};

window.disconnect = function() {
    manualSignOut = true;
    if (ws) ws.close();
    stopBackendStatsPolling();
    stopDemoRoute(false, true);
    stopSharing();

    /* Clear state */
    others = {};
    Object.keys(peerTrails).forEach((id) => {
        if (peerTrails[id]) {
            peerTrails[id].remove();
            delete peerTrails[id];
        }
    });
    txCount = 0;
    rxCount = 0;
    peakPeers = 0;
    totalDistanceKm = 0;
    reconnectCount = 0;
    authFailureCount = 0;
    malformedSentCount = 0;
    geolocationFallbackCount = 0;
    burstRunCount = 0;
    broadcastRxCount = 0;
    parseErrorCount = 0;
    myTrail = [];
    updateTimestamps = [];
    myMarker = null;
    myTrailPolyline = null;
    latencyStart = 0;
    UI.statLatency.textContent = '—';
    UI.statTx.textContent = '0';
    UI.statRx.textContent = '0';
    UI.statUptime.textContent = '0s';

    updateProtocolTrace('tx', '—');
    updateProtocolTrace('rx', '—');

    if (map) {
        map.remove();
        map = null;
    }

    /* Show auth screen */
    UI.app.style.display = 'none';
    UI.authOverlay.style.display = 'flex';
    resetAuthButton();
    UI.authError.textContent = '';

    resetWorkflowState();

    log('Disconnected.', 'system');
    updateDemoRouteButton();
    updateBackendEvidenceUI();
    updatePresentationMetrics();
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

updateDemoRouteButton();
updateProtocolTrace('tx', '—');
updateProtocolTrace('rx', '—');
resetWorkflowState();
updateBackendEvidenceUI();
fetchBackendStats();
updatePresentationMetrics();
