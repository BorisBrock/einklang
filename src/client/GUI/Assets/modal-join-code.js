// ---------------------------------------------------------------------------
// Join Code dialog (opened from the "Use join code…" button of the Connect
// dialog). Decodes the pasted code and hands the result back to the Connect
// dialog, so the user can review it before connecting.
// ---------------------------------------------------------------------------

// Checks a join code field while the user types or pastes into it and shows
// the result in statusEl: green with the decoded room and server, or red if
// the code is broken. Returns a function that re-checks the field and gives
// back the decoded code, or null if the field is empty or the code is broken.
window.watchJoinCode = (inputEl, statusEl) => {
  function check() {
    const text = inputEl.value.trim();
    let joinCode = null;
    if (text) {
      try {
        joinCode = JoinCode.decode(text);
      } catch { /* shown as broken below */ }
    }

    statusEl.hidden = !text;
    statusEl.classList.toggle("valid", !!joinCode);
    statusEl.classList.toggle("invalid", !!text && !joinCode);
    if (joinCode) {
      const server = joinCode.address.includes(":") ? "[" + joinCode.address + "]" : joinCode.address;
      const port   = joinCode.port === JoinCode.DEFAULT_PORT ? "" : ":" + joinCode.port;
      statusEl.textContent = "Valid code for room \u201c" + joinCode.room + "\u201d on " + server + port;
    } else {
      statusEl.textContent = text ? "Not a valid join code \u2014 check for typos or missing characters" : "";
    }

    if (text) inputEl.setAttribute("aria-invalid", joinCode ? "false" : "true");
    else inputEl.removeAttribute("aria-invalid");
    return joinCode;
  }

  inputEl.addEventListener("input", check);
  return check;
};

document.addEventListener("DOMContentLoaded", () => {
  const dialog    = document.getElementById("join-code-dialog");
  const inputEl   = document.getElementById("join-code-input");
  const applyBtn  = document.getElementById("join-code-apply-btn");
  const cancelBtn = document.getElementById("join-code-cancel-btn");
  const statusEl  = document.getElementById("join-code-status");

  if (!dialog) return;

  const check = window.watchJoinCode(inputEl, statusEl);

  window.openJoinCodeDialog = () => {
    inputEl.value = "";
    window.showModal("join-code-dialog");
    inputEl.focus();
  };

  // Closes this dialog and goes back to the Connect dialog
  async function back(joinCode) {
    await window.closeModal(dialog);
    window.openAddRoomDialog(joinCode ? { joinCode } : {});
  }

  function apply() {
    const joinCode = check();
    if (joinCode) back(joinCode);
  }

  // Only a valid code can be applied
  inputEl.addEventListener("input", () => {
    applyBtn.disabled = !check();
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
    if (dialog.open) applyBtn.disabled = !check();
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
    // Only a click on the backdrop itself; anything inside (even keyboard-
    // activated buttons, which report 0/0 coordinates) is not outside.
    if (e.target !== dialog) return;
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
