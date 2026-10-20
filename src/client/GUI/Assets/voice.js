// Voice chat UI logic
// Manages the mute/unmute button and per-user speaking indicators.

let voiceMuted = false;
const speakingTimers = new Map();

function applyVoiceEnabled(enabled) {
  const btn = document.getElementById('voice-mute-btn');
  if (!btn) return;

  if (enabled) {
    btn.disabled = false;
    btn.style.display = '';
    btn.title = voiceMuted ? 'Unmute microphone' : 'Mute microphone';
  } else {
    voiceMuted = false;
    btn.disabled = true;
    btn.style.display = 'none';
  }
}

/**
 * Toggle microphone mute state.
 * Calls into the C++ backend via the exposed `toggle_voice_mute` bridge.
 */
function handleVoiceMuteToggle() {
  voiceMuted = !voiceMuted;

  const btn      = document.getElementById('voice-mute-btn');
  const iconOn   = document.getElementById('mic-on-icon');
  const iconOff  = document.getElementById('mic-off-icon');

  if (voiceMuted) {
    btn.classList.add('muted');
    btn.title = 'Unmute microphone';
    iconOn.style.display  = 'none';
    iconOff.style.display = '';
  } else {
    btn.classList.remove('muted');
    btn.title = 'Mute microphone';
    iconOn.style.display  = '';
    iconOff.style.display = 'none';
  }

  // Notify C++ backend
  if (window.saucer?.exposed?.toggle_voice_mute) {
    window.saucer.exposed.toggle_voice_mute(voiceMuted).catch(err => {
      console.error('Failed to toggle voice mute:', err);
    });
  }
}

/**
 * Called from C++ to set speaking indicator for a given user.
 * @param {string} userName
 * @param {boolean} isSpeaking
 */
function setUserSpeaking(userName, isSpeaking) {
  const targetName = String(userName ?? '').trim();
  if (!targetName) return;

  const userItems = document.querySelectorAll('#users-list .user-item');
  for (const item of userItems) {
    const nameEl = item.querySelector('.user-name');
    if (nameEl && nameEl.textContent.trim() === targetName) {
      const dot = item.querySelector('.speaking-indicator');
      if (dot) {
        if (isSpeaking) {
          dot.classList.add('active');

          const oldTimer = speakingTimers.get(targetName);
          if (oldTimer) {
            clearTimeout(oldTimer);
          }

          const timer = setTimeout(() => {
            dot.classList.remove('active');
            speakingTimers.delete(targetName);
          }, 300);
          speakingTimers.set(targetName, timer);
        } else {
          dot.classList.remove('active');

          const oldTimer = speakingTimers.get(targetName);
          if (oldTimer) {
            clearTimeout(oldTimer);
            speakingTimers.delete(targetName);
          }
        }
      }
      break;
    }
  }
}

window.addEventListener('DOMContentLoaded', () => {
  (async () => {
    try {
      if (window.saucer?.exposed?.get_voice_enabled) {
        const enabled = await window.saucer.exposed.get_voice_enabled();
        applyVoiceEnabled(Boolean(enabled));
      }
    } catch (err) {
      console.error('Failed to query voice setting:', err);
    }
  })();
});
