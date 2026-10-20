// ---------------------------------------------------------------------------
// Color themes
//
// Each theme's colors live in main.css as CSS custom properties under
// [data-palette=<id>]. `mode` selects Pico's light or dark base styles.
// The chosen theme is persisted in client-config.yml via save_theme(); a
// localStorage copy lets us apply it before first paint (no flash on startup).
// ---------------------------------------------------------------------------

const THEMES = [
  { id: "dark",            name: "Midnight",        mode: "dark"  },
  { id: "light",           name: "Daylight",        mode: "light" },
  { id: "nord",            name: "Nord",            mode: "dark"  },
  { id: "dracula",         name: "Dracula",         mode: "dark"  },
  { id: "gruvbox",         name: "Gruvbox",         mode: "dark"  },
  { id: "rose-pine",       name: "Rosé Pine",       mode: "dark"  },
  { id: "solarized-dark",  name: "Solarized Dark",  mode: "dark"  },
  { id: "solarized-light", name: "Solarized Light", mode: "light" },
];

const DEFAULT_THEME = "dark";
const THEME_CACHE_KEY = "einklang-theme";

function _findTheme(id) {
  return THEMES.find(t => t.id === id) || THEMES.find(t => t.id === DEFAULT_THEME);
}

// Applies a theme to the document. Returns the id actually applied.
function applyTheme(id) {
  const theme = _findTheme(id);
  const root  = document.documentElement;
  root.dataset.theme   = theme.mode;   // Pico base (light / dark)
  root.dataset.palette = theme.id;     // our color tokens
  return theme.id;
}

function getCurrentTheme() {
  return document.documentElement.dataset.palette || DEFAULT_THEME;
}

// Applies a theme and persists it (config file + local cache).
function saveTheme(id) {
  const applied = applyTheme(id);
  try { localStorage.setItem(THEME_CACHE_KEY, applied); } catch { /* storage unavailable */ }
  if (window.saucer?.exposed?.save_theme) {
    window.saucer.exposed.save_theme(applied).catch(() => {});
  }
  return applied;
}

// Before first paint: use the cached theme so the window doesn't flash.
(() => {
  let cached = null;
  try { cached = localStorage.getItem(THEME_CACHE_KEY); } catch { /* storage unavailable */ }
  applyTheme(cached || DEFAULT_THEME);
})();

// Once the bridge is up, the config file is the source of truth.
window.addEventListener("DOMContentLoaded", async () => {
  try {
    if (!window.saucer?.exposed?.get_theme) return;
    const id = applyTheme(await window.saucer.exposed.get_theme());
    try { localStorage.setItem(THEME_CACHE_KEY, id); } catch { /* storage unavailable */ }
  } catch { /* keep the cached theme */ }
});
