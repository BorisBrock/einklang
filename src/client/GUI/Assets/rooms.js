// ---------------------------------------------------------------------------
// Room list management & C++ callbacks
// ---------------------------------------------------------------------------

// Show a modal error dialog for connection failures
function showConnectionError(message) {
  const dialog = document.getElementById("connection-error-dialog");
  const msgEl  = document.getElementById("connection-error-msg");
  if (!dialog || !msgEl) return;
  msgEl.textContent = message || "Unknown error.";
  window.showModal("connection-error-dialog");
}

// Called by C++ via webview->execute() after a room entry attempt
function handleRoomEntryResponse(success, errorMessage) {
  const dialog = document.getElementById("add-room-dialog");
  const isDialogOpen = dialog && dialog.open;

  if (success) {
    if (isDialogOpen) window.closeModal(dialog);
    // Clear any inline switch error
    const switchErr = document.getElementById("switch-room-error");
    if (switchErr) {
      switchErr.style.display = "none";
      switchErr.textContent = "";
    }
  } else {
    if (isDialogOpen) {
      // Reset dialog button state
      const connectBtn = document.getElementById("connect-btn");
      if (connectBtn) {
        connectBtn.disabled = false;
        connectBtn.textContent = "Connect";
      }
      // Close the connect dialog so the error modal can be shown
      window.closeModal(dialog).then(() => showConnectionError(errorMessage));
    } else {
      // Re-enable any loading room item
      document.querySelectorAll(".room-item.loading").forEach((el) => {
        el.classList.remove("loading");
      });
      // Show error modal
      showConnectionError(errorMessage);
    }
  }
}

// Called by C++ via webview->execute() to mark a room as active in the GUI
function setActiveRoom(address, port, roomName) {
  document.querySelectorAll(".room-item").forEach((el) => {
    el.classList.remove("active");
  });

  const selector =
    "[data-address='" + CSS.escape(address) + "']" +
    "[data-port='"    + port               + "']" +
    "[data-room='"    + CSS.escape(roomName) + "']";
  const item = document.querySelector(selector);
  if (item) {
    item.classList.add("active");
    item.classList.remove("loading");
  }

  // Clear switch error on success
  const switchErr = document.getElementById("switch-room-error");
  if (switchErr) {
    switchErr.style.display = "none";
    switchErr.textContent = "";
  }

  // Show chat content, hide placeholder, update title
  const chatContent = document.getElementById("chat-content");
  const chatPlaceholder = document.getElementById("chat-placeholder");
  const chatTitle = document.getElementById("chat-title");
  if (chatContent) chatContent.style.display = "";
  if (chatPlaceholder) chatPlaceholder.style.display = "none";
  if (chatTitle) chatTitle.textContent = roomName;

  // Clear chat history and reset per-room state
  const chatHistory = document.getElementById("chat-history");
  if (chatHistory) chatHistory.innerHTML = "";
  clearChatState();
}

// Called by C++ (via execute) after a new room connection is saved to config
function addKnownRoom(address, port, roomName, userName) {
  const container = document.getElementById("rooms-list");
  if (!container) return;

  const key = address + ":" + port;
  const escapedKey = key.replace(/'/g, "\\'");
  const existingCard = container.querySelector("[data-server-key='" + escapedKey + "']");

  if (existingCard) {
    const list = existingCard.querySelector(".room-list");
    const existingItem = Array.from(
      list.querySelectorAll(".room-item")
    ).find((el) => el.dataset.room === roomName);

    if (!existingItem) {
      list.appendChild(_makeRoomItem(address, port, roomName, userName || ""));
      _applyServerStatus(existingCard);
    } else {
      // Update the stored userName if we now have one
      if (userName) existingItem.dataset.userName = userName;
    }
  } else {
    container.appendChild(_buildServerCard(address, port, [{ roomName, userName: userName || "" }]));
  }
}

// ---------------------------------------------------------------------------
// Room list DOM helpers
// ---------------------------------------------------------------------------

function _makeRoomItem(address, port, roomName, userName) {
  const item = document.createElement("div");
  item.className        = "room-item";
  item.dataset.address  = address;
  item.dataset.port     = String(port);
  item.dataset.room     = roomName;
  item.dataset.userName = userName;

  const roomIcon = document.createElement("span");
  roomIcon.className = "room-icon";
  roomIcon.setAttribute("aria-hidden", "true");
  roomIcon.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><line x1="4" y1="9" x2="20" y2="9"/><line x1="4" y1="15" x2="20" y2="15"/><line x1="10" y1="3" x2="8" y2="21"/><line x1="16" y1="3" x2="14" y2="21"/></svg>';
  item.appendChild(roomIcon);

  const roomLabel = document.createElement("span");
  roomLabel.className = "room-name";
  roomLabel.textContent = roomName;
  item.appendChild(roomLabel);

  // User-count badge (filled in by _applyServerStatus)
  const usersBadge = document.createElement("span");
  usersBadge.className = "room-users";
  item.appendChild(usersBadge);

  item.addEventListener("click", async () => {
    // Ignore if already in this room
    if (item.classList.contains("active")) return;
    // Ignore if a switch is already in progress
    if (document.querySelector(".room-item.loading")) return;

    // Clear previous switch error
    const switchErr = document.getElementById("switch-room-error");
    if (switchErr) {
      switchErr.style.display = "none";
      switchErr.textContent = "";
    }

    item.classList.add("loading");

    try {
      await window.saucer.exposed.switch_to_room(address, port, roomName, item.dataset.userName);
      // Result arrives asynchronously via handleRoomEntryResponse() / setActiveRoom() from C++
    } catch (err) {
      console.error("Failed to call C++:", err);
      item.classList.remove("loading");
      if (switchErr) {
        switchErr.textContent = "Internal error: " + err;
        switchErr.style.display = "block";
      }
    }
  });

  return item;
}

function _buildServerCard(address, port, rooms) {
  const key = address + ":" + port;
  const card = document.createElement("div");
  card.className = "server-card";
  card.dataset.serverKey = key;

  const title = document.createElement("div");
  title.className = "server-card-title";

  const serverIcon = document.createElement("span");
  serverIcon.className = "server-icon";
  serverIcon.setAttribute("aria-hidden", "true");
  serverIcon.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="2" width="20" height="8" rx="2"/><rect x="2" y="14" width="20" height="8" rx="2"/><circle cx="7" cy="6" r="1" fill="currentColor"/><circle cx="7" cy="18" r="1" fill="currentColor"/></svg>';
  title.appendChild(serverIcon);

  const serverLabel = document.createElement("span");
  serverLabel.textContent = address;
  title.appendChild(serverLabel);

  // Online/offline indicator (filled in by _applyServerStatus)
  const statusDot = document.createElement("span");
  statusDot.className = "server-status";
  title.appendChild(statusDot);

  card.appendChild(title);

  const list = document.createElement("div");
  list.className = "room-list";
  for (const r of rooms) {
    list.appendChild(_makeRoomItem(address, port, r.roomName, r.userName));
  }
  card.appendChild(list);
  _applyServerStatus(card);
  return card;
}

// Populates the room list from a JSON array produced by get_known_rooms()
function _populateRoomList(rooms) {
  const container = document.getElementById("rooms-list");
  if (!container) return;
  container.innerHTML = "";

  // Group by address:port, preserving insertion order
  const servers = new Map();
  for (const room of rooms) {
    const key = room.address + ":" + room.port;
    if (!servers.has(key))
      servers.set(key, { address: room.address, port: room.port, rooms: [] });
    servers.get(key).rooms.push({ roomName: room.roomName, userName: room.userName });
  }

  for (const [, srv] of servers) {
    container.appendChild(_buildServerCard(srv.address, srv.port, srv.rooms));
  }
}

window.addEventListener("DOMContentLoaded", () => {
  (async () => {
    try {
      const json = await window.saucer.exposed.get_known_rooms();
      _populateRoomList(JSON.parse(json));
    } catch (err) {
      console.error("Failed to load known rooms:", err);
    }

    // Statuses polled before the page finished loading
    try {
      const json = await window.saucer.exposed.get_server_statuses();
      for (const status of JSON.parse(json)) setServerStatus(status);
    } catch (err) {
      console.error("Failed to load server statuses:", err);
    }
  })();
});

// ---------------------------------------------------------------------------
// Server status (online state and room occupancy)
// ---------------------------------------------------------------------------

// "address:port" → latest status object from C++:
// { address, port, online, serverVersion, rooms: [{ name, userCount, maxUsers }] }
const _serverStatus = new Map();

// Called by C++ via webview->execute() whenever a server's status changes
function setServerStatus(status) {
  const key = status.address + ":" + status.port;
  _serverStatus.set(key, status);

  const card = document.querySelector(".server-card[data-server-key='" + CSS.escape(key) + "']");
  if (card) _applyServerStatus(card);
}

// Updates a server card's status dot and room user counts from the cache
function _applyServerStatus(card) {
  const status = _serverStatus.get(card.dataset.serverKey);
  const known  = status !== undefined;
  const online = known && status.online;

  card.classList.toggle("offline", known && !online);

  const dot = card.querySelector(".server-status");
  if (dot) {
    dot.classList.toggle("online",  online);
    dot.classList.toggle("offline", known && !online);
    dot.title = !known ? "Checking server status…"
              : online ? "Online" + (status.serverVersion ? " (v" + status.serverVersion + ")" : "")
              : "Offline";
  }

  card.querySelectorAll(".room-item").forEach((item) => {
    const room = online ? status.rooms.find((r) => r.name === item.dataset.room) : undefined;
    _setRoomUserCount(item, room ? room.userCount : null, room ? room.maxUsers : 0);
  });
}

// Shows "Empty" or the user count on a room item; null hides the badge
function _setRoomUserCount(item, count, maxUsers) {
  const badge = item.querySelector(".room-users");
  if (!badge) return;

  if (count === null) {
    badge.textContent = "";
    badge.title = "";
    badge.style.display = "none";
    return;
  }

  badge.style.display = "";
  badge.classList.toggle("empty", count === 0);
  badge.textContent = count === 0 ? "Empty" : String(count);
  badge.title = maxUsers > 0
    ? count + " of " + maxUsers + " users"
    : count + (count === 1 ? " user" : " users");
}

// Keeps the active room's count current between polls (called from setRoomUsers)
function _updateActiveRoomUserCount(count) {
  const item = document.querySelector(".room-item.active");
  if (!item) return;

  const status = _serverStatus.get(item.dataset.address + ":" + item.dataset.port);
  const room   = status && status.online
    ? status.rooms.find((r) => r.name === item.dataset.room)
    : undefined;
  if (room) room.userCount = count;

  _setRoomUserCount(item, count, room ? room.maxUsers : 0);
}

// ---------------------------------------------------------------------------
// User list
// ---------------------------------------------------------------------------

// Called by C++ via webview->execute() with the current room's user list
function setRoomUsers(users) {
  const list = document.getElementById("users-list");
  if (!list) return;

  list.innerHTML = "";
  users.forEach((name) => {
    const item = document.createElement("div");
    item.className = "user-item";

    const userIcon = document.createElement("span");
    userIcon.className = "user-icon";
    userIcon.setAttribute("aria-hidden", "true");
    userIcon.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="8" r="4"/><path d="M4 20c0-4 3.6-7 8-7s8 3 8 7"/></svg>';
    item.appendChild(userIcon);

    const userLabel = document.createElement("span");
    userLabel.className = "user-name";
    userLabel.textContent = name;
    item.appendChild(userLabel);

    // Speaking indicator dot (activated from C++ via setUserSpeaking)
    const dot = document.createElement("span");
    dot.className = "speaking-indicator";
    item.appendChild(dot);

    list.appendChild(item);
  });

  _updateActiveRoomUserCount(users.length);
}

// ---------------------------------------------------------------------------
// Active-room reset (called by C++ when the currently active room is removed)
// ---------------------------------------------------------------------------

function clearActiveRoom() {
  // Remove active highlighting from all room items
  document.querySelectorAll(".room-item.active").forEach((el) => {
    el.classList.remove("active");
  });

  // Reset chat panel to placeholder
  const chatContent     = document.getElementById("chat-content");
  const chatPlaceholder = document.getElementById("chat-placeholder");
  const chatHistory     = document.getElementById("chat-history");
  const chatTitle       = document.getElementById("chat-title");
  if (chatContent)     chatContent.style.display = "none";
  if (chatPlaceholder) chatPlaceholder.style.display = "";
  if (chatHistory)     chatHistory.innerHTML = "";
  if (chatTitle)       chatTitle.textContent = "Chat";

  // Clear the users list
  const usersList = document.getElementById("users-list");
  if (usersList) usersList.innerHTML = "";

  // Reset send button and per-room chat state
  clearChatState();
}

// ---------------------------------------------------------------------------
// Context-menu helpers
// ---------------------------------------------------------------------------

let _ctxMenu = null;

function _ensureContextMenu() {
  if (_ctxMenu) return;
  _ctxMenu = document.createElement("div");
  _ctxMenu.id = "room-context-menu";
  _ctxMenu.style.display = "none";
  document.body.appendChild(_ctxMenu);
}

const _CTX_ICON_REMOVE =
  '<line x1="18" y1="6" x2="6" y2="18"/>' +
  '<line x1="6" y1="6" x2="18" y2="18"/>';

const _CTX_ICON_EDIT =
  '<path d="M12 20h9"/>' +
  '<path d="M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4Z"/>';

// items: [{ label, icon, danger, callback }]
function _showContextMenu(x, y, items) {
  _ensureContextMenu();
  _ctxMenu.innerHTML = "";

  for (const entry of items) {
    const item = document.createElement("div");
    item.className = "ctx-menu-item" + (entry.danger ? " ctx-menu-item--danger" : "");
    item.innerHTML =
      '<svg xmlns="http://www.w3.org/2000/svg" width="13" height="13" viewBox="0 0 24 24" ' +
      'fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true" class="ctx-menu-icon">'
      + entry.icon
      + '</svg>';
    const label = document.createElement("span");
    label.textContent = entry.label;
    item.appendChild(label);
    item.addEventListener("click", (e) => {
      e.stopPropagation();
      _hideContextMenu();
      entry.callback();
    });
    _ctxMenu.appendChild(item);
  }

  // Keep the menu inside the viewport
  _ctxMenu.style.left    = x + "px";
  _ctxMenu.style.top     = y + "px";
  _ctxMenu.style.display = "block";
}

function _hideContextMenu() {
  if (_ctxMenu) _ctxMenu.style.display = "none";
}

// ---------------------------------------------------------------------------
// DOM removal helpers
// ---------------------------------------------------------------------------

function _removeRoomItemFromDOM(address, port, roomName) {
  const container = document.getElementById("rooms-list");
  if (!container) return;

  const selector =
    ".room-item" +
    "[data-address='" + CSS.escape(address)  + "']" +
    "[data-port='"    + port                 + "']" +
    "[data-room='"    + CSS.escape(roomName) + "']";
  const item = container.querySelector(selector);
  if (!item) return;

  const card = item.closest(".server-card");
  item.remove();

  // Remove the server card entirely when it has no rooms left
  if (card && card.querySelectorAll(".room-item").length === 0) {
    card.remove();
  }
}

function _removeServerCardFromDOM(address, port) {
  const container = document.getElementById("rooms-list");
  if (!container) return;

  const key  = address + ":" + port;
  const card = container.querySelector("[data-server-key='" + CSS.escape(key) + "']");
  if (card) card.remove();
}

// ---------------------------------------------------------------------------
// Context-menu event delegation
// ---------------------------------------------------------------------------

document.addEventListener("contextmenu", (e) => {
  // Always close any previously open menu first
  _hideContextMenu();

  const roomItem    = e.target.closest(".room-item");
  const serverTitle = e.target.closest(".server-card-title");

  if (roomItem) {
    e.preventDefault();
    const address  = roomItem.dataset.address;
    const port     = parseInt(roomItem.dataset.port, 10);
    const roomName = roomItem.dataset.room;

    _showContextMenu(e.pageX, e.pageY, [{
      label: "Remove Room", icon: _CTX_ICON_REMOVE, danger: true,
      callback: async () => {
        try {
          await window.saucer.exposed.remove_room(address, port, roomName);
          _removeRoomItemFromDOM(address, port, roomName);
        } catch (err) {
          console.error("remove_room failed:", err);
        }
      },
    }]);
  } else if (serverTitle) {
    e.preventDefault();
    const card    = serverTitle.closest(".server-card");
    const key     = card.dataset.serverKey;          // "address:port"
    const colonIdx = key.lastIndexOf(":");
    const address = key.slice(0, colonIdx);
    const port    = parseInt(key.slice(colonIdx + 1), 10);

    // Pre-fill the user name only when all rooms on the server share one
    const userNames = new Set(
      Array.from(card.querySelectorAll(".room-item")).map((el) => el.dataset.userName)
    );
    const userName = userNames.size === 1 ? [...userNames][0] : "";

    _showContextMenu(e.pageX, e.pageY, [
      {
        label: "Edit Server", icon: _CTX_ICON_EDIT,
        callback: () => openEditServerDialog(address, port, userName),
      },
      {
        label: "Remove Server", icon: _CTX_ICON_REMOVE, danger: true,
        callback: async () => {
          try {
            await window.saucer.exposed.remove_server(address, port);
            _removeServerCardFromDOM(address, port);
          } catch (err) {
            console.error("remove_server failed:", err);
          }
        },
      },
    ]);
  }
});

// Close context menu when clicking anywhere else
document.addEventListener("click", (e) => {
  if (_ctxMenu && !_ctxMenu.contains(e.target)) {
    _hideContextMenu();
  }
});
