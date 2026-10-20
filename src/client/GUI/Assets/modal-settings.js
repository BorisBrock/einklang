document.addEventListener("DOMContentLoaded", () => {
  const dialog       = document.getElementById("settings-dialog");
  const cancelBtn    = document.getElementById("settings-cancel-btn");
  const okBtn        = document.getElementById("settings-ok-btn");
  const gainSlider   = document.getElementById("input-gain-slider");
  const gainValueEl  = document.getElementById("input-gain-value");
  const vadSlider    = document.getElementById("vad-threshold-slider");
  const meterBar     = document.getElementById("level-meter-bar");
  const meterThresh  = document.getElementById("level-meter-threshold");

  if (!dialog) return;

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
          ? "var(--primary)"            // above threshold: teal
          : "hsl(205, 22%, 45%)";       // below threshold: dimmed
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
  let savedVad  = VAD_MIN;
  let savedGain = 1.0;

  // ---------------------------------------------------------------------------
  // Dialog lifecycle
  // ---------------------------------------------------------------------------
  dialog.addEventListener("toggle", async () => {
    if (dialog.open) {
      // Load current settings from the backend and populate sliders.
      try {
        if (window.saucer?.exposed?.get_voice_settings) {
          const json = await window.saucer.exposed.get_voice_settings();
          const s    = JSON.parse(json);
          savedVad   = s.vadThreshold;
          savedGain  = s.inputGain;
        }
      } catch { /* use defaults */ }

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
  cancelBtn.addEventListener("click", (e) => {
    e.preventDefault();
    e.stopPropagation();
    if (window.saucer?.exposed?.apply_voice_settings) {
      window.saucer.exposed.apply_voice_settings(savedVad, savedGain)
        .catch(() => {});
    }
    window.closeModal(dialog);
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
    window.closeModal(dialog);
  });

  // ---------------------------------------------------------------------------
  // Close on click outside the article (treat as cancel)
  // ---------------------------------------------------------------------------
  dialog.addEventListener("click", (e) => {
    const rect = dialog.querySelector("article").getBoundingClientRect();
    const inside =
      e.clientX >= rect.left && e.clientX <= rect.right &&
      e.clientY >= rect.top  && e.clientY <= rect.bottom;
    if (!inside) {
      if (window.saucer?.exposed?.apply_voice_settings) {
        window.saucer.exposed.apply_voice_settings(savedVad, savedGain)
          .catch(() => {});
      }
      window.closeModal(dialog);
    }
  });
});
