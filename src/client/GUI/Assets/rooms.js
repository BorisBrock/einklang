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
      // Keep the connect dialog open, so the user can fix the input and retry
      window.showConnectError(errorMessage);
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

// The user name the active room was entered with (null when not in a room)
let _activeUserName = null;

// Called by C++ via webview->execute() to mark a room as active in the GUI
function setActiveRoom(address, port, roomName, userName) {
  _activeUserName = userName;

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
function addKnownRoom(address, port, roomName) {
  const container = document.getElementById("rooms-list");
  if (!container) return;

  const key = address + ":" + port;
  const existingCard = container.querySelector("[data-server-key='" + CSS.escape(key) + "']");

  if (existingCard) {
    const list = existingCard.querySelector(".room-list");
    const existingItem = Array.from(
      list.querySelectorAll(".room-item")
    ).find((el) => el.dataset.room === roomName);

    if (!existingItem) {
      list.appendChild(_makeRoomItem(address, port, roomName));
      _applyServerStatus(existingCard);
    }
  } else {
    container.appendChild(_buildServerCard(address, port, [roomName]));
  }
}

// Returns the address under which a server is already in the room list, or
// the given address if it is a new server. Matches ignore how the address is
// written ("MyServer.de" / "myserver.de", "[::1]" / "::1"), so a room added by
// hand or via join code joins the existing server instead of duplicating it.
function knownServerAddress(address, port) {
  const canonical = JoinCode.canonicalHost(address);
  for (const card of document.querySelectorAll(".server-card")) {
    const key      = card.dataset.serverKey;
    const colonIdx = key.lastIndexOf(":");
    const known    = key.slice(0, colonIdx);
    if (parseInt(key.slice(colonIdx + 1), 10) === port && JoinCode.canonicalHost(known) === canonical)
      return known;
  }
  return address;
}

// ---------------------------------------------------------------------------
// Room list DOM helpers
// ---------------------------------------------------------------------------

function _makeRoomItem(address, port, roomName) {
  const item = document.createElement("div");
  item.className        = "room-item";
  item.dataset.address  = address;
  item.dataset.port     = String(port);
  item.dataset.room     = roomName;

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
      await window.saucer.exposed.switch_to_room(address, port, roomName);
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

function _buildServerCard(address, port, roomNames) {
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

  // "+" to add another room of this server (visible on hover / focus)
  const addBtn = document.createElement("button");
  addBtn.type      = "button";
  addBtn.className = "server-add-room";
  addBtn.title     = "Add room on this server";
  addBtn.setAttribute("aria-label", "Add room on " + address);
  addBtn.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" width="11" height="11" viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" aria-hidden="true"><line x1="6" y1="1" x2="6" y2="11"/><line x1="1" y1="6" x2="11" y2="6"/></svg>';
  addBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    _openAddRoom(card);
  });
  title.appendChild(addBtn);

  // Online/offline indicator (filled in by _applyServerStatus)
  const statusDot = document.createElement("span");
  statusDot.className = "server-status";
  title.appendChild(statusDot);

  card.appendChild(title);

  const list = document.createElement("div");
  list.className = "room-list";
  for (const roomName of roomNames) {
    list.appendChild(_makeRoomItem(address, port, roomName));
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
    servers.get(key).rooms.push(room.roomName);
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
// Lost connection (called by C++ while re-entering the active room)
// ---------------------------------------------------------------------------

function setReconnecting(reconnecting) {
  const banner = document.getElementById("reconnect-banner");
  if (banner) banner.style.display = reconnecting ? "" : "none";

  // Sending is impossible while disconnected
  for (const id of ["chat-msg", "chat-attach-btn"]) {
    const el = document.getElementById(id);
    if (el) el.disabled = reconnecting;
  }
  _updateSendBtn();

  // The user list is stale until the room is re-entered
  if (reconnecting) {
    const usersList = document.getElementById("users-list");
    if (usersList) usersList.innerHTML = "";
  }
}

// ---------------------------------------------------------------------------
// Active-room reset (called by C++ when the currently active room is removed)
// ---------------------------------------------------------------------------

function clearActiveRoom() {
  setReconnecting(false);
  _activeUserName = null;

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

const _CTX_ICON_ADD =
  '<line x1="12" y1="5" x2="12" y2="19"/>' +
  '<line x1="5" y1="12" x2="19" y2="12"/>';

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

    _showContextMenu(e.pageX, e.pageY, [
      {
        label: "Add Room\u2026", icon: _CTX_ICON_ADD,
        callback: () => _openAddRoom(card),
      },
      {
        label: "Edit Server", icon: _CTX_ICON_EDIT,
        callback: () => openEditServerDialog(address, port),
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

// Close context menu / room picker when clicking anywhere else
document.addEventListener("click", (e) => {
  if (_ctxMenu && !_ctxMenu.contains(e.target)) {
    _hideContextMenu();
  }
  if (_roomPicker && !_roomPicker.contains(e.target)) {
    _hideRoomPicker();
  }
});

document.addEventListener("keydown", (e) => {
  if (e.key === "Escape") _hideRoomPicker();
});

// ---------------------------------------------------------------------------
// Adding a room to an existing server
// ---------------------------------------------------------------------------

let _roomPicker = null;

const _PICKER_ICON_LOCK =
  '<svg xmlns="http://www.w3.org/2000/svg" width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="4" y="11" width="16" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/></svg>';

const _PICKER_ICON_EDIT =
  '<svg xmlns="http://www.w3.org/2000/svg" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 20h9"/><path d="M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4Z"/></svg>';

// Entry point for the server card's "+" button and "Add Room…" menu entry.
// Online servers get a picker of the rooms they offer; otherwise the connect
// dialog opens with the server prefilled.
function _openAddRoom(card) {
  const key      = card.dataset.serverKey;
  const colonIdx = key.lastIndexOf(":");
  const address  = key.slice(0, colonIdx);
  const port     = parseInt(key.slice(colonIdx + 1), 10);
  const status   = _serverStatus.get(key);

  if (!status || !status.online) {
    window.openAddRoomDialog({ address, port });
    return;
  }

  const added = new Set(
    Array.from(card.querySelectorAll(".room-item")).map((el) => el.dataset.room)
  );
  const available = status.rooms.filter((r) => !added.has(r.name));

  _showRoomPicker(card.querySelector(".server-card-title"), address, port, available);
}

function _showRoomPicker(anchor, address, port, rooms) {
  _hideRoomPicker();

  const picker = document.createElement("div");
  picker.id = "room-picker";
  picker.setAttribute("role", "menu");

  const heading = document.createElement("div");
  heading.className = "room-picker-heading";
  heading.textContent = "Rooms on " + address;
  picker.appendChild(heading);

  if (rooms.length === 0) {
    const empty = document.createElement("div");
    empty.className = "room-picker-empty";
    empty.textContent = "You've added all rooms on this server.";
    picker.appendChild(empty);
  }

  for (const room of rooms) {
    picker.appendChild(_makePickerRoom(picker, address, port, room));
  }

  const sep = document.createElement("div");
  sep.className = "room-picker-sep";
  picker.appendChild(sep);

  const manual = document.createElement("button");
  manual.type = "button";
  manual.className = "room-picker-item room-picker-manual";
  manual.innerHTML = _PICKER_ICON_EDIT;
  const manualLabel = document.createElement("span");
  manualLabel.textContent = "Enter room name\u2026";
  manual.appendChild(manualLabel);
  manual.addEventListener("click", (e) => {
    e.stopPropagation();
    _hideRoomPicker();
    window.openAddRoomDialog({ address, port });
  });
  picker.appendChild(manual);

  document.body.appendChild(picker);
  _roomPicker = picker;

  // Below the server header, kept inside the viewport
  const rect   = anchor.getBoundingClientRect();
  const width  = picker.offsetWidth;
  const height = picker.offsetHeight;
  const left   = Math.max(8, Math.min(rect.left, window.innerWidth - width - 8));
  let   top    = rect.bottom + 4;
  if (top + height > window.innerHeight - 8) top = Math.max(8, rect.top - height - 4);
  picker.style.left = left + "px";
  picker.style.top  = top + "px";

  const first = picker.querySelector(".room-picker-item:not([disabled])");
  if (first) first.focus();
}

// One selectable room in the picker
function _makePickerRoom(picker, address, port, room) {
  const full = room.maxUsers > 0 && room.userCount >= room.maxUsers;

  const item = document.createElement("button");
  item.type = "button";
  item.className = "room-picker-item";
  item.disabled = full;

  const name = document.createElement("span");
  name.className = "room-name";
  name.textContent = room.name;
  item.appendChild(name);

  if (room.hasPassword) {
    const lock = document.createElement("span");
    lock.className = "room-picker-lock";
    lock.title = "Password required";
    lock.innerHTML = _PICKER_ICON_LOCK;
    item.appendChild(lock);
  }

  const count = document.createElement("span");
  count.className = "room-picker-count";
  count.textContent = full ? "Full"
                    : room.maxUsers > 0 ? room.userCount + "/" + room.maxUsers
                    : String(room.userCount);
  item.appendChild(count);

  item.addEventListener("click", (e) => {
    e.stopPropagation();
    if (room.hasPassword) {
      _showPickerPassword(picker, item, address, port, room.name);
    } else {
      _hideRoomPicker();
      _joinRoom(address, port, room.name, "");
    }
  });

  return item;
}

// Swaps a password-protected room's row for a password field + Join button
function _showPickerPassword(picker, item, address, port, roomName) {
  const existing = picker.querySelector(".room-picker-password");
  if (existing) existing.remove();

  const row = document.createElement("form");
  row.className = "room-picker-password";

  const input = document.createElement("input");
  input.type = "password";
  input.placeholder = "Password for " + roomName;
  input.autocomplete = "off";
  row.appendChild(input);

  const join = document.createElement("button");
  join.type = "submit";
  join.textContent = "Join";
  join.disabled = true;
  row.appendChild(join);

  input.addEventListener("input", () => { join.disabled = input.value.length === 0; });
  row.addEventListener("click", (e) => e.stopPropagation());
  row.addEventListener("submit", (e) => {
    e.preventDefault();
    if (!input.value) return;
    _hideRoomPicker();
    _joinRoom(address, port, roomName, input.value);
  });

  item.after(row);
  input.focus();
}

async function _joinRoom(address, port, roomName, password) {
  try {
    await window.saucer.exposed.connect_to_server(address, port, roomName, password);
    // Result arrives asynchronously via handleRoomEntryResponse() / addKnownRoom() from C++
  } catch (err) {
    console.error("Failed to call C++:", err);
    showConnectionError("Internal error: " + err);
  }
}

function _hideRoomPicker() {
  if (_roomPicker) {
    _roomPicker.remove();
    _roomPicker = null;
  }
}
