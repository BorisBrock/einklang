document.addEventListener("DOMContentLoaded", () => {
  const dialog       = document.getElementById("settings-dialog");
  const cancelBtn    = document.getElementById("settings-cancel-btn");
  const okBtn        = document.getElementById("settings-ok-btn");
  const gainSlider   = document.getElementById("input-gain-slider");
  const gainValueEl  = document.getElementById("input-gain-value");
  const vadSlider    = document.getElementById("vad-threshold-slider");
  const meterBar     = document.getElementById("level-meter-bar");
  const meterThresh  = document.getElementById("level-meter-threshold");
  const themeGrid    = document.getElementById("theme-grid");
  const userNameEl   = document.getElementById("profile-username");
  const inputSelect  = document.getElementById("audio-input-select");
  const outputSelect = document.getElementById("audio-output-select");
  const deviceWarnEl = document.getElementById("audio-device-warning");
  const versionEl    = document.getElementById("settings-app-version");
  const navItems     = dialog ? dialog.querySelectorAll(".settings-nav-item") : [];
  const panels       = dialog ? dialog.querySelectorAll(".settings-panel") : [];

  if (!dialog) return;

  // ---------------------------------------------------------------------------
  // Section navigation
  // ---------------------------------------------------------------------------
  function showSection(name) {
    navItems.forEach(item => item.classList.toggle("active", item.dataset.section === name));
    panels.forEach(panel => { panel.hidden = panel.dataset.section !== name; });
  }

  navItems.forEach(item => item.addEventListener("click", () => showSection(item.dataset.section)));

  // ---------------------------------------------------------------------------
  // Profile: the user name must not be empty
  // ---------------------------------------------------------------------------
  function validateUserName() {
    const valid = userNameEl.value.trim().length > 0;
    userNameEl.setAttribute("aria-invalid", valid ? "false" : "true");
    okBtn.disabled = !valid;
  }

  userNameEl.addEventListener("input", validateUserName);

  // ---------------------------------------------------------------------------
  // Theme picker: one card per theme, each previewing its own palette
  // ---------------------------------------------------------------------------
  const CHECK_SVG = '<svg xmlns="http://www.w3.org/2000/svg" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polyline points="20 6 9 17 4 12"/></svg>';

  function markSelectedTheme(id) {
    themeGrid.querySelectorAll(".theme-card").forEach(card => {
      card.setAttribute("aria-checked", card.dataset.theme === id ? "true" : "false");
    });
  }

  THEMES.forEach(theme => {
    const card = document.createElement("button");
    card.type = "button";
    card.className = "theme-card";
    card.dataset.theme = theme.id;
    card.setAttribute("role", "radio");
    card.innerHTML =
      '<span class="theme-preview" data-palette="' + theme.id + '">' +
        '<span class="theme-preview-side"><i class="on"></i><i></i><i></i></span>' +
        '<span class="theme-preview-main"><i class="text"></i><i class="other"></i><i class="own"></i></span>' +
      '</span>' +
      '<span class="theme-card-label"><span></span><span class="theme-card-check">' + CHECK_SVG + '</span></span>';
    card.querySelector(".theme-card-label > span").textContent = theme.name;
    card.addEventListener("click", () => {
      applyTheme(theme.id);       // live preview; persisted on Save
      markSelectedTheme(theme.id);
    });
    themeGrid.appendChild(card);
  });

  // ---------------------------------------------------------------------------
  // Audio devices: "" = system default.  A saved device that is currently
  // missing (e.g. unplugged headset) stays selectable and is flagged, so the
  // choice survives until it is back; meanwhile the backend uses the default.
  // ---------------------------------------------------------------------------
  function fillDeviceSelect(select, devices, selected) {
    select.replaceChildren();

    const def = devices.find(d => d.isDefault);
    const defOpt = document.createElement("option");
    defOpt.value = "";
    defOpt.textContent = def ? "System default (" + def.name + ")" : "System default";
    select.appendChild(defOpt);

    devices.forEach(d => {
      const opt = document.createElement("option");
      opt.value = d.name;
      opt.textContent = d.name;
      select.appendChild(opt);
    });

    const missing = selected !== "" && !devices.some(d => d.name === selected);
    if (missing) {
      const opt = document.createElement("option");
      opt.value = selected;
      opt.textContent = selected + " (not connected)";
      opt.dataset.missing = "true";
      select.appendChild(opt);
    }
    select.value = selected;
    return missing;
  }

  function updateDeviceWarning() {
    const missing = [inputSelect, outputSelect]
      .map(sel => sel.selectedOptions[0])
      .filter(opt => opt && opt.dataset.missing === "true")
      .map(opt => opt.value);
    deviceWarnEl.hidden = missing.length === 0;
    deviceWarnEl.textContent = missing.length === 0 ? "" :
      "Not connected: " + missing.join(", ") +
      ". Using the system default until it is available again.";
  }

  function applyDevicesLive() {
    updateDeviceWarning();
    if (window.saucer?.exposed?.apply_audio_devices) {
      window.saucer.exposed.apply_audio_devices(inputSelect.value, outputSelect.value)
        .catch(() => {});
    }
  }

  inputSelect.addEventListener("change", applyDevicesLive);
  outputSelect.addEventListener("change", applyDevicesLive);

  // ---------------------------------------------------------------------------
  // Scale helpers
  // ---------------------------------------------------------------------------

  // VAD threshold: logarithmic mapping between slider integer (0-100) and
  // threshold float (0.005-0.5).  The log scale gives finer control at low
  // values where most useful tweaking happens.
  const VAD_MIN  = 0.005, VAD_MAX  = 0.5;
  const VAD_BASE = Math.log(VAD_MAX / VAD_MIN);   // log(100)

  function sliderToVad(val)  { return VAD_MIN * Math.exp(VAD_BASE * val / 100); }
  function vadToSlider(vad)  { return Math.round(Math.log(vad / VAD_MIN) / VAD_BASE * 100); }

  // Input gain: linear mapping between slider integer (0-200) and gain float
  // (0.0-2.0).  100 = unity gain (1.0).
  function sliderToGain(val) { return val / 100; }
  function gainToSlider(g)   { return Math.round(g * 100); }

  // Map an RMS value to a percentage position on the level-meter track.
  // Uses a logarithmic scale so the bar is usefully spread across its width.
  const METER_LOG_MIN = Math.log(0.002), METER_LOG_MAX = Math.log(0.35);
  function rmsToPercent(rms) {
    if (rms <= 0) return 0;
    return Math.max(0, Math.min(100,
      (Math.log(Math.max(rms, 0.002)) - METER_LOG_MIN) /
      (METER_LOG_MAX - METER_LOG_MIN) * 100));
  }

  function currentVad()    { return sliderToVad(parseInt(vadSlider.value,   10)); }
  function currentGain()   { return sliderToGain(parseInt(gainSlider.value,  10)); }
  function formatGain(g)   { return g.toFixed(2) + "\u00D7"; } // e.g. "1.00×"

  // ---------------------------------------------------------------------------
  // Level-meter polling
  // ---------------------------------------------------------------------------
  let pollTimer = null;

  function startPolling() {
    if (pollTimer !== null) return;
    pollTimer = setInterval(async () => {
      try {
        const rms  = await window.saucer.exposed.get_input_rms();
        const vad  = currentVad();
        const rmsP = rmsToPercent(rms);
        meterBar.style.width      = rmsP + "%";
        meterBar.style.background = (rms >= vad)
          ? "var(--accent)"             // above threshold: transmitting
          : "var(--text-faint)";        // below threshold: dimmed
      } catch { /* ignore if bridge not yet ready */ }
    }, 80);
  }

  function stopPolling() {
    if (pollTimer === null) return;
    clearInterval(pollTimer);
    pollTimer = null;
    meterBar.style.width = "0%";
  }

  // Move the threshold marker to reflect the current VAD slider position.
  function updateThresholdMarker() {
    meterThresh.style.left = rmsToPercent(currentVad()) + "%";
  }

  // ---------------------------------------------------------------------------
  // Saved values (captured on dialog open; restored on cancel)
  // ---------------------------------------------------------------------------
  let savedVad   = VAD_MIN;
  let savedGain  = 1.0;
  let savedTheme = getCurrentTheme();
  let savedUserName = "";
  let savedInputDevice  = "";
  let savedOutputDevice = "";

  // ---------------------------------------------------------------------------
  // Dialog lifecycle
  // ---------------------------------------------------------------------------
  dialog.addEventListener("toggle", async () => {
    if (dialog.open) {
      savedTheme = getCurrentTheme();
      markSelectedTheme(savedTheme);

      try {
        if (window.saucer?.exposed?.get_user_name)
          savedUserName = await window.saucer.exposed.get_user_name();
      } catch { /* keep previous value */ }
      userNameEl.value = savedUserName;

      try {
        if (versionEl && !versionEl.textContent && window.saucer?.exposed?.get_app_version)
          versionEl.textContent = "Einklang v" + await window.saucer.exposed.get_app_version();
      } catch { /* leave the label empty */ }
      userNameEl.removeAttribute("aria-invalid");
      okBtn.disabled = false;

      // Load current settings from the backend and populate sliders.
      try {
        if (window.saucer?.exposed?.get_voice_settings) {
          const json = await window.saucer.exposed.get_voice_settings();
          const s    = JSON.parse(json);
          savedVad   = s.vadThreshold;
          savedGain  = s.inputGain;
        }
      } catch { /* use defaults */ }

      let devices = { inputs: [], outputs: [] };
      try {
        if (window.saucer?.exposed?.get_audio_devices) {
          devices = JSON.parse(await window.saucer.exposed.get_audio_devices());
          savedInputDevice  = devices.selectedInput  || "";
          savedOutputDevice = devices.selectedOutput || "";
        }
      } catch { /* keep previous selection */ }
      fillDeviceSelect(inputSelect,  devices.inputs,  savedInputDevice);
      fillDeviceSelect(outputSelect, devices.outputs, savedOutputDevice);
      updateDeviceWarning();

      vadSlider.value  = vadToSlider(savedVad);
      gainSlider.value = gainToSlider(savedGain);
      gainValueEl.textContent = formatGain(savedGain);
      updateThresholdMarker();
      startPolling();
    } else {
      stopPolling();
    }
  });

  // ---------------------------------------------------------------------------
  // Live preview: apply settings to the pipeline as the user drags
  // ---------------------------------------------------------------------------
  function applyLive() {
    updateThresholdMarker();
    gainValueEl.textContent = formatGain(currentGain());
    if (window.saucer?.exposed?.apply_voice_settings) {
      window.saucer.exposed.apply_voice_settings(currentVad(), currentGain())
        .catch(() => {});
    }
  }

  vadSlider.addEventListener("input",  applyLive);
  gainSlider.addEventListener("input", applyLive);

  // ---------------------------------------------------------------------------
  // Cancel: restore original settings and close
  // ---------------------------------------------------------------------------
  function cancelAndClose() {
    if (window.saucer?.exposed?.apply_voice_settings) {
      window.saucer.exposed.apply_voice_settings(savedVad, savedGain)
        .catch(() => {});
    }
    if ((inputSelect.value !== savedInputDevice || outputSelect.value !== savedOutputDevice) &&
        window.saucer?.exposed?.apply_audio_devices) {
      window.saucer.exposed.apply_audio_devices(savedInputDevice, savedOutputDevice)
        .catch(() => {});
    }
    applyTheme(savedTheme);
    window.closeModal(dialog);
  }

  cancelBtn.addEventListener("click", (e) => {
    e.preventDefault();
    e.stopPropagation();
    cancelAndClose();
  });

  // Escape: same as Cancel (instead of the native close, which skips the revert)
  dialog.addEventListener("cancel", (e) => {
    e.preventDefault();
    cancelAndClose();
  });

  // ---------------------------------------------------------------------------
  // OK: persist settings and close
  // ---------------------------------------------------------------------------
  okBtn.addEventListener("click", (e) => {
    e.preventDefault();
    e.stopPropagation();
    if (window.saucer?.exposed?.save_voice_settings) {
      window.saucer.exposed.save_voice_settings(currentVad(), currentGain())
        .catch(() => {});
    }
    if ((inputSelect.value !== savedInputDevice || outputSelect.value !== savedOutputDevice) &&
        window.saucer?.exposed?.save_audio_devices) {
      window.saucer.exposed.save_audio_devices(inputSelect.value, outputSelect.value)
        .catch(() => {});
    }
    if (getCurrentTheme() !== savedTheme) saveTheme(getCurrentTheme());
    const userName = userNameEl.value.trim();
    if (userName && userName !== savedUserName && window.saucer?.exposed?.save_user_name) {
      window.saucer.exposed.save_user_name(userName).catch(() => {});
    }
    window.closeModal(dialog);
  });

  // ---------------------------------------------------------------------------
  // Close on click outside the article (treat as cancel)
  // ---------------------------------------------------------------------------
  dialog.addEventListener("click", (e) => {
    // Clicks on the backdrop target the dialog itself; anything else (incl.
    // keyboard-activated buttons, which report 0/0 coordinates) is inside.
    if (e.target !== dialog) return;
    const rect = dialog.querySelector("article").getBoundingClientRect();
    const inside =
      e.clientX >= rect.left && e.clientX <= rect.right &&
      e.clientY >= rect.top  && e.clientY <= rect.bottom;
    if (!inside) cancelAndClose();
  });
});
