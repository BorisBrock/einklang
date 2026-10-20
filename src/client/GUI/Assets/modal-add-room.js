// ---------------------------------------------------------------------------
// Animated modal open / close helpers (Pico CSS class-driven animations)
// ---------------------------------------------------------------------------

// Animation duration must match the CSS @keyframes durations.
const MODAL_ANIM_MS = 250;

// Expose a global helper used by inline handlers: `showModal('id')`.
window.showModal = (id) => {
  const modal = document.getElementById(id);
  if (!modal) return;
  if (typeof modal.showModal !== "function") {
    modal.style.display = "block";
    return;
  }
  try { modal.showModal(); } catch (err) { return; }
  const root = document.documentElement;
  root.classList.add("modal-is-open", "modal-is-opening");
  setTimeout(() => root.classList.remove("modal-is-opening"), MODAL_ANIM_MS);
};

// Global animated close — plays the closing animation, then actually closes.
// Returns a Promise that resolves once the dialog is fully closed.
window.closeModal = (dialog) => {
  if (!dialog || !dialog.open) return Promise.resolve();
  return new Promise((resolve) => {
    const root = document.documentElement;
    root.classList.add("modal-is-closing");
    setTimeout(() => {
      root.classList.remove("modal-is-open", "modal-is-closing");
      dialog.close();
      resolve();
    }, MODAL_ANIM_MS);
  });
};

document.addEventListener("DOMContentLoaded", () => {
  const dialog     = document.getElementById("add-room-dialog");
  const serverEl   = document.getElementById("dlg-server");
  const portEl     = document.getElementById("dlg-port");
  const roomEl     = document.getElementById("dlg-room");
  const usernameEl = document.getElementById("dlg-username");
  const passwordEl = document.getElementById("dlg-password");
  const connectBtn = document.getElementById("connect-btn");
  const cancelBtn  = document.getElementById("connect-cancel-btn");
  const errorEl    = document.getElementById("connect-error");

  if (!dialog) return;

  // --- Validation: enable Connect only when required fields are non-empty ---
  function validate() {
    const portVal = parseInt(portEl.value, 10);
    const ok =
      serverEl.value.trim().length > 0 &&
      !isNaN(portVal) && portVal >= 1 && portVal <= 65535 &&
      roomEl.value.trim().length > 0 &&
      usernameEl.value.trim().length > 0 &&
      usernameEl.value.trim().length <= 64;
    connectBtn.disabled = !ok;
  }

  [serverEl, portEl, roomEl, usernameEl, passwordEl].forEach((el) =>
    el.addEventListener("input", validate)
  );

  // Reset form state whenever the dialog is opened
  dialog.addEventListener("toggle", () => {
    if (dialog.open) {
      errorEl.style.display = "none";
      errorEl.textContent = "";
      connectBtn.disabled = true;
      connectBtn.textContent = "Connect";
    }
  });

  // --- Cancel ---
  cancelBtn.addEventListener("click", (e) => {
    e.preventDefault();
    window.closeModal(dialog);
  });

  // --- Connect ---
  connectBtn.addEventListener("click", async (e) => {
    e.preventDefault();

    const server   = serverEl.value.trim();
    const port     = parseInt(portEl.value, 10);
    const room     = roomEl.value.trim();
    const username = usernameEl.value.trim();
    const password = passwordEl.value; // password may be empty

    // Hide previous error and show loading state
    errorEl.style.display = "none";
    connectBtn.disabled = true;
    connectBtn.textContent = "Connecting\u2026";

    try {
      await window.saucer.exposed.connect_to_server(server, port, room, password, username);
      // Result arrives asynchronously via handleRoomEntryResponse() called from C++
    } catch (err) {
      console.error("Failed to call C++:", err);
      errorEl.textContent = "Internal error: " + err;
      errorEl.style.display = "block";
      connectBtn.disabled = false;
      connectBtn.textContent = "Connect";
    }
  });

  // --- Close on click outside the article ---
  dialog.addEventListener("click", (e) => {
    const rect = dialog.querySelector("article").getBoundingClientRect();
    const inside =
      e.clientX >= rect.left && e.clientX <= rect.right &&
      e.clientY >= rect.top  && e.clientY <= rect.bottom;
    if (!inside) window.closeModal(dialog);
  });

  // --- Close on Escape ---
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && dialog.open) window.closeModal(dialog);
  });
});
