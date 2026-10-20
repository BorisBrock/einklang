// ---------------------------------------------------------------------------
// Welcome dialog: shown once, on the first start (no config file yet), to
// pick the user name and microphone, and optionally join a first room with a
// join code. Everything else stays in Settings.
// ---------------------------------------------------------------------------
window.addEventListener("DOMContentLoaded", async () => {
  const dialog      = document.getElementById("welcome-dialog");
  const userNameEl  = document.getElementById("welcome-username");
  const inputSelect = document.getElementById("welcome-input-select");
  const joinCodeEl  = document.getElementById("welcome-join-code");
  const errorEl     = document.getElementById("welcome-join-code-error");
  const okBtn       = document.getElementById("welcome-ok-btn");

  if (!dialog) return;

  const exposed = window.saucer?.exposed;
  try {
    if (!exposed?.is_first_start || !await exposed.is_first_start()) return;
  } catch { return; }

  try {
    userNameEl.value = await exposed.get_user_name();
  } catch { /* leave empty */ }

  let devices = { inputs: [], selectedInput: "", selectedOutput: "" };
  try {
    devices = JSON.parse(await exposed.get_audio_devices());
  } catch { /* only the system default is offered */ }

  const def = devices.inputs.find(d => d.isDefault);
  const defOpt = document.createElement("option");
  defOpt.value = "";
  defOpt.textContent = def ? "System default (" + def.name + ")" : "System default";
  inputSelect.appendChild(defOpt);
  devices.inputs.forEach(d => {
    const opt = document.createElement("option");
    opt.value = d.name;
    opt.textContent = d.name;
    inputSelect.appendChild(opt);
  });
  inputSelect.value = devices.selectedInput || "";

  function validate() {
    const valid = userNameEl.value.trim().length > 0;
    userNameEl.setAttribute("aria-invalid", valid ? "false" : "true");
    okBtn.disabled = !valid;
  }
  userNameEl.addEventListener("input", validate);

  function showError(message) {
    errorEl.textContent = message;
    errorEl.style.display = message ? "block" : "none";
    joinCodeEl.setAttribute("aria-invalid", message ? "true" : "false");
  }
  joinCodeEl.addEventListener("input", () => showError(""));

  async function finish() {
    if (okBtn.disabled) return;

    // Check the join code first, so a typo keeps the dialog open
    let joinCode = null;
    if (joinCodeEl.value.trim()) {
      try {
        joinCode = JoinCode.decode(joinCodeEl.value);
      } catch (err) {
        showError(err.message);
        joinCodeEl.focus();
        return;
      }
    }

    okBtn.disabled = true; // no second connect from a double click
    try {
      await exposed.save_user_name(userNameEl.value.trim());
      if (inputSelect.value !== (devices.selectedInput || ""))
        await exposed.save_audio_devices(inputSelect.value, devices.selectedOutput || "");
    } catch { /* settings can still be changed later */ }
    await window.closeModal(dialog);

    // Connect right away through the Connect dialog: it shows the progress,
    // and the room is saved once the server lets us in.
    if (joinCode) {
      window.openAddRoomDialog({ joinCode });
      document.getElementById("connect-btn").click();
    }
  }

  okBtn.addEventListener("click", (e) => {
    e.preventDefault();
    finish();
  });

  [userNameEl, joinCodeEl].forEach((el) => el.addEventListener("keydown", (e) => {
    if (e.key === "Enter") { e.preventDefault(); finish(); }
  }));

  // The setup is short and every choice has a sensible default, but it should
  // be confirmed explicitly — don't let Escape dismiss it.
  dialog.addEventListener("cancel", (e) => e.preventDefault());

  window.showModal("welcome-dialog");
  userNameEl.removeAttribute("aria-invalid");
  joinCodeEl.removeAttribute("aria-invalid");
  userNameEl.focus();
  userNameEl.select();
});
