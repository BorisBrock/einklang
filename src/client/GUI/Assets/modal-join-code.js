// ---------------------------------------------------------------------------
// Join Code dialog (opened from the "Use join code…" button of the Connect
// dialog). Decodes the pasted code and hands the result back to the Connect
// dialog, so the user can review it before connecting.
// ---------------------------------------------------------------------------

document.addEventListener("DOMContentLoaded", () => {
  const dialog    = document.getElementById("join-code-dialog");
  const inputEl   = document.getElementById("join-code-input");
  const applyBtn  = document.getElementById("join-code-apply-btn");
  const cancelBtn = document.getElementById("join-code-cancel-btn");
  const errorEl   = document.getElementById("join-code-error");

  if (!dialog) return;

  window.openJoinCodeDialog = () => {
    inputEl.value = "";
    window.showModal("join-code-dialog");
    inputEl.focus();
  };

  function showError(message) {
    errorEl.textContent = message;
    errorEl.style.display = message ? "block" : "none";
  }

  // Closes this dialog and goes back to the Connect dialog
  async function back(joinCode) {
    await window.closeModal(dialog);
    window.openAddRoomDialog(joinCode ? { joinCode } : {});
  }

  function apply() {
    try {
      back(JoinCode.decode(inputEl.value));
    } catch (err) {
      showError(err.message);
    }
  }

  inputEl.addEventListener("input", () => {
    showError("");
    applyBtn.disabled = inputEl.value.trim().length === 0;
  });

  // Enter applies the code instead of adding a newline
  inputEl.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      if (!applyBtn.disabled) apply();
    }
  });

  // Reset form state whenever the dialog is opened
  dialog.addEventListener("toggle", () => {
    if (dialog.open) {
      showError("");
      applyBtn.disabled = true;
    }
  });

  applyBtn.addEventListener("click", (e) => {
    e.preventDefault();
    apply();
  });

  cancelBtn.addEventListener("click", (e) => {
    e.preventDefault();
    back(null);
  });

  // --- Close on click outside the article ---
  dialog.addEventListener("click", (e) => {
    const rect = dialog.querySelector("article").getBoundingClientRect();
    const inside =
      e.clientX >= rect.left && e.clientX <= rect.right &&
      e.clientY >= rect.top  && e.clientY <= rect.bottom;
    if (!inside) back(null);
  });

  // --- Close on Escape ---
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && dialog.open) {
      e.preventDefault();
      back(null);
    }
  });
});
