// ---------------------------------------------------------------------------
// Chat
// ---------------------------------------------------------------------------

// Color palette for other users' bubbles — cycles when more users join
const _BUBBLE_PALETTE = [
  { bg: "#e17055", fg: "#fff" }, // coral
  { bg: "#00b894", fg: "#fff" }, // emerald
  { bg: "#6c5ce7", fg: "#fff" }, // purple
  { bg: "#f0932b", fg: "#fff" }, // orange
  { bg: "#e84393", fg: "#fff" }, // pink
  { bg: "#00cec9", fg: "#fff" }, // teal
  { bg: "#a29bfe", fg: "#222" }, // lavender
  { bg: "#27ae60", fg: "#fff" }, // green
];

const _userColorMap = {};
let _colorCounter = 0;

// Resets per-room chat state (color assignments, input). Called when switching rooms.
function clearChatState() {
  for (const key in _userColorMap) delete _userColorMap[key];
  _colorCounter = 0;
  const input = document.getElementById("chat-msg");
  if (input) input.value = "";
  _updateSendBtn();
}

function _colorForUser(username) {
  if (!(username in _userColorMap)) {
    _userColorMap[username] = _colorCounter++ % _BUBBLE_PALETTE.length;
  }
  return _BUBBLE_PALETTE[_userColorMap[username]];
}

// Enable/disable send button based on input content
function _updateSendBtn() {
  const input = document.getElementById("chat-msg");
  const btn   = document.getElementById("chat-send-btn");
  if (input && btn) btn.disabled = input.disabled || input.value.trim().length === 0;
}

// Send message on Enter key; wire up input-driven button state
document.addEventListener("DOMContentLoaded", () => {
  const input = document.getElementById("chat-msg");
  if (input) {
    input.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.shiftKey) {
        e.preventDefault();
        handleBtnChatSend();
      }
    });
    input.addEventListener("input", _updateSendBtn);
  }

  // Paste anywhere in the chat area: intercept image clipboard items
  document.addEventListener("paste", (e) => {
    if (!document.getElementById("chat-content") ||
        document.getElementById("chat-content").style.display === "none") return;

    const items = e.clipboardData && e.clipboardData.items;
    if (!items) return;

    for (const item of items) {
      if (item.type.startsWith("image/")) {
        e.preventDefault();
        const file = item.getAsFile();
        if (file) _sendImageFile(file);
        break;
      }
    }
  });

  // File-picker button
  const fileInput = document.getElementById("chat-img-file");
  if (fileInput) {
    fileInput.addEventListener("change", () => {
      const file = fileInput.files && fileInput.files[0];
      if (file) _sendImageFile(file);
      fileInput.value = ""; // reset so the same file can be re-selected
    });
  }
});

// Called by HTML onclick
async function handleBtnChatSend() {
  try {
    const msg = document.getElementById("chat-msg").value.trim();
    if (!msg) return;
    if (msg.length > 2048) {
      console.warn("Message too long (", msg.length, "chars); not sending.");
      return;
    }
    await window.saucer.exposed.publish_chat_msg(msg);
    document.getElementById("chat-msg").value = "";
    _updateSendBtn();
  } catch (err) {
    console.error("Failed to call C++:", err);
  }
}

// Parses `text` and returns a DocumentFragment where detected URLs are wrapped
// in <a> elements that open in the system browser. All non-URL text is kept as
// plain text nodes (XSS-safe — no innerHTML involved).
function _linkifyMessage(text) {
  const fragment = document.createDocumentFragment();
  // Match http(s):// URLs or bare www. addresses
  const urlRegex = /https?:\/\/[^\s<>"'\u200B]+|www\.[^\s<>"'.][^\s<>"']*/gi;
  let lastIndex = 0;
  let match;
  while ((match = urlRegex.exec(text)) !== null) {
    // Plain text before this URL
    if (match.index > lastIndex) {
      fragment.appendChild(document.createTextNode(text.slice(lastIndex, match.index)));
    }
    // Build the anchor — prepend https:// for bare www. links
    const href = /^https?:\/\//i.test(match[0]) ? match[0] : "https://" + match[0];
    const a = document.createElement("a");
    a.href = href;
    a.textContent = match[0];
    a.addEventListener("click", async (e) => {
      e.preventDefault();
      try {
        await window.saucer.exposed.open_url(href);
      } catch (err) {
        console.error("Failed to open URL:", err);
      }
    });
    fragment.appendChild(a);
    lastIndex = match.index + match[0].length;
  }
  // Any remaining plain text
  if (lastIndex < text.length) {
    fragment.appendChild(document.createTextNode(text.slice(lastIndex)));
  }
  return fragment;
}

// Returns the username of the local user (from the active room item)
function getCurrentUsername() {
  const active = document.querySelector(".room-item.active");
  return active ? active.dataset.userName : null;
}

// Called from C++ via WebUI when a new chat message arrives from the server
function appendChatMessage(userName, message) {
  const history = document.getElementById("chat-history");
  const isSelf = userName === getCurrentUsername();

  const row = document.createElement("div");
  row.className = "bubble-row " + (isSelf ? "own" : "other");

  // Username label shown on every bubble
  const nameEl = document.createElement("div");
  nameEl.className = "bubble-username";
  nameEl.textContent = userName;
  if (!isSelf) {
    // Use the same hue as the bubble for a cohesive look
    nameEl.style.color = _colorForUser(userName).bg;
  }
  row.appendChild(nameEl);

  const bubble = document.createElement("div");
  bubble.className = "bubble";
  bubble.appendChild(_linkifyMessage(message));
  if (!isSelf) {
    const color = _colorForUser(userName);
    bubble.style.background = color.bg;
    bubble.style.color = color.fg;
  }
  row.appendChild(bubble);

  history.appendChild(row);
  history.scrollTop = history.scrollHeight; // Auto-scroll to bottom
}

// Called from C++ via WebUI when the server delivers the room's chat history upon entry.
// Populates the chat view with the given messages (chat-history has already been cleared
// by setActiveRoom before this is called).
function loadChatHistory(messages) {
  for (const entry of messages) {
    if (entry.imageData) {
      appendChatImage(entry.userName, entry.imageData);
    } else {
      appendChatMessage(entry.userName, entry.text);
    }
  }
}

// ---------------------------------------------------------------------------
// Image support
// ---------------------------------------------------------------------------

// Reads a File as base64 and sends it to the server.
async function _sendImageFile(file) {
  // Enforce a 5 MiB client-side guard (server enforces the same limit).
  if (file.size > 5 * 1024 * 1024) {
    console.warn("Image too large (" + file.size + " bytes); not sending.");
    return;
  }

  try {
    const b64 = await _fileToBase64(file);
    await window.saucer.exposed.publish_chat_image(b64);
  } catch (err) {
    console.error("Failed to send image:", err);
  }
}

// Wraps FileReader in a Promise and returns the base64-encoded data URL payload
// (everything after the "data:<mime>;base64," prefix).
function _fileToBase64(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload  = () => {
      const result = reader.result; // "data:image/png;base64,<b64>"
      const comma  = result.indexOf(",");
      resolve(comma >= 0 ? result.slice(comma + 1) : result);
    };
    reader.onerror = () => reject(reader.error);
    reader.readAsDataURL(file);
  });
}

// Opens the OS file picker for images.  Triggered by the attach button.
function handleBtnChatAttach() {
  const fileInput = document.getElementById("chat-img-file");
  if (fileInput) fileInput.click();
}

// Called from C++ via WebUI when a new chat image arrives from the server.
// `base64` is a plain base64-encoded string (no data-URL prefix).
function appendChatImage(userName, base64) {
  const history = document.getElementById("chat-history");
  const isSelf  = userName === getCurrentUsername();

  const row = document.createElement("div");
  row.className = "bubble-row " + (isSelf ? "own" : "other");

  // Username label
  const nameEl = document.createElement("div");
  nameEl.className = "bubble-username";
  nameEl.textContent = userName;
  if (!isSelf) {
    nameEl.style.color = _colorForUser(userName).bg;
  }
  row.appendChild(nameEl);

  const bubble = document.createElement("div");
  bubble.className = "bubble bubble--image";
  if (!isSelf) {
    const color = _colorForUser(userName);
    bubble.style.background = color.bg;
    bubble.style.color      = color.fg;
  }

  const src = "data:image;base64," + base64;

  const img = document.createElement("img");
  img.src    = src;
  img.alt    = userName + "'s image";
  img.className = "chat-image";
  img.title  = "Click to enlarge";
  img.addEventListener("click", () => _openLightbox(src));
  img.onerror = () => { img.alt = "[image unavailable]"; img.removeAttribute("src"); };
  bubble.appendChild(img);
  row.appendChild(bubble);

  history.appendChild(row);
  history.scrollTop = history.scrollHeight;
}

// ---------------------------------------------------------------------------
// Lightbox
// ---------------------------------------------------------------------------

function _openLightbox(src) {
  let overlay = document.getElementById("img-lightbox");
  if (!overlay) {
    // Create overlay lazily on first use
    overlay = document.createElement("div");
    overlay.id = "img-lightbox";
    overlay.className = "lightbox-overlay";
    overlay.setAttribute("role", "dialog");
    overlay.setAttribute("aria-modal", "true");
    overlay.setAttribute("aria-label", "Image viewer");

    const inner = document.createElement("div");
    inner.className = "lightbox-inner";

    const closeBtn = document.createElement("button");
    closeBtn.className = "lightbox-close";
    closeBtn.setAttribute("aria-label", "Close");
    closeBtn.innerHTML = "&times;";
    closeBtn.addEventListener("click", (e) => { e.stopPropagation(); _closeLightbox(); });

    const imgEl = document.createElement("img");
    imgEl.className = "lightbox-img";
    imgEl.id = "lightbox-img";

    inner.appendChild(closeBtn);
    inner.appendChild(imgEl);
    overlay.appendChild(inner);
    document.body.appendChild(overlay);

    // Close when clicking outside the image
    overlay.addEventListener("click", (e) => {
      if (e.target === overlay) _closeLightbox();
    });

    // Close on Escape key
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape") _closeLightbox();
    });
  }

  document.getElementById("lightbox-img").src = src;
  overlay.classList.add("open");
  document.body.style.overflow = "hidden";
}

function _closeLightbox() {
  const overlay = document.getElementById("img-lightbox");
  if (overlay) {
    overlay.classList.remove("open");
    document.body.style.overflow = "";
  }
}
