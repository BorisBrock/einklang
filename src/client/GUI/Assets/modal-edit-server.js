// ---------------------------------------------------------------------------
// Edit Server dialog (opened from the server context menu in rooms.js)
// ---------------------------------------------------------------------------

let _editServerTarget = null; // { address, port } of the server being edited

// Opens the dialog pre-filled with the server's current settings.
function openEditServerDialog(address, port) {
  _editServerTarget = { address, port };

  document.getElementById("edit-server-address").value  = address;
  document.getElementById("edit-server-port").value     = String(port);

  window.showModal("edit-server-dialog");
}

document.addEventListener("DOMContentLoaded", () => {
  const dialog     = document.getElementById("edit-server-dialog");
  const addressEl  = document.getElementById("edit-server-address");
  const portEl     = document.getElementById("edit-server-port");
  const saveBtn    = document.getElementById("edit-server-save-btn");
  const cancelBtn  = document.getElementById("edit-server-cancel-btn");
  const errorEl    = document.getElementById("edit-server-error");

  if (!dialog) return;

  // --- Validation: address and a valid port are required ---
  function validate() {
    const portVal = parseInt(portEl.value, 10);
    const ok =
      addressEl.value.trim().length > 0 &&
      !isNaN(portVal) && portVal >= 1 && portVal <= 65535;
    saveBtn.disabled = !ok;
  }

  [addressEl, portEl].forEach((el) =>
    el.addEventListener("input", validate)
  );

  // Reset form state whenever the dialog is opened
  dialog.addEventListener("toggle", () => {
    if (dialog.open) {
      errorEl.style.display = "none";
      errorEl.textContent = "";
      validate();
    }
  });

  // --- Cancel ---
  cancelBtn.addEventListener("click", (e) => {
    e.preventDefault();
    window.closeModal(dialog);
  });

  // --- Save ---
  saveBtn.addEventListener("click", async (e) => {
    e.preventDefault();
    if (!_editServerTarget) return;

    const { address: oldAddress, port: oldPort } = _editServerTarget;
    const newAddress = addressEl.value.trim();
    const newPort    = parseInt(portEl.value, 10);

    // Remember the active room so its highlight survives the list rebuild.
    // If it is on the edited server, C++ disconnects and clears it instead.
    const active = document.querySelector(".room-item.active");
    const keepActive = active &&
      !(active.dataset.address === oldAddress && parseInt(active.dataset.port, 10) === oldPort)
      ? { address: active.dataset.address, port: active.dataset.port, room: active.dataset.room }
      : null;

    saveBtn.disabled = true;

    try {
      const json = await window.saucer.exposed.edit_server(
        oldAddress, oldPort, newAddress, newPort);
      _populateRoomList(JSON.parse(json));

      if (keepActive) {
        const selector =
          ".room-item" +
          "[data-address='" + CSS.escape(keepActive.address) + "']" +
          "[data-port='"    + keepActive.port                + "']" +
          "[data-room='"    + CSS.escape(keepActive.room)    + "']";
        const item = document.querySelector(selector);
        if (item) item.classList.add("active");
      }

      window.closeModal(dialog);
    } catch (err) {
      console.error("edit_server failed:", err);
      errorEl.textContent = "Internal error: " + err;
      errorEl.style.display = "block";
      saveBtn.disabled = false;
    }
  });

  // --- Close on click outside the article ---
  dialog.addEventListener("click", (e) => {
    // Only a click on the backdrop itself; anything inside (even keyboard-
    // activated buttons, which report 0/0 coordinates) is not outside.
    if (e.target !== dialog) return;
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
