// ---------------------------------------------------------------------------
// Splitter / layout resizing
// ---------------------------------------------------------------------------

function _makeSplitter(splitter, leftEl, rightEl, fixedSide) {
  // fixedSide: 'left' means leftEl is fixed, 'right' means rightEl is fixed
  let isDragging = false;
  let startX = 0;
  let startLeftWidth = 0;
  let startRightWidth = 0;

  splitter.addEventListener("mousedown", (e) => {
    isDragging = true;
    startX = e.clientX;
    startLeftWidth = leftEl.getBoundingClientRect().width;
    startRightWidth = rightEl.getBoundingClientRect().width;
    splitter.classList.add("active");

    // Prevent text selection while dragging
    document.body.style.userSelect = "none";
    document.body.style.cursor = "col-resize";
  });

  document.addEventListener("mousemove", (e) => {
    if (!isDragging) return;
    const delta = e.clientX - startX;

    if (fixedSide === "left") {
      // Resize the left fixed column
      const newWidth = Math.max(100, Math.min(400, startLeftWidth + delta));
      leftEl.style.width = newWidth + "px";
    } else if (fixedSide === "right") {
      // Resize the right fixed column
      const newWidth = Math.max(100, Math.min(400, startRightWidth - delta));
      rightEl.style.width = newWidth + "px";
    }
  });

  document.addEventListener("mouseup", () => {
    if (!isDragging) return;
    isDragging = false;
    splitter.classList.remove("active");
    document.body.style.userSelect = "";
    document.body.style.cursor = "";
  });
}

window.addEventListener("DOMContentLoaded", () => {
  // Left splitter resizes the left column (center adapts automatically)
  _makeSplitter(
    document.getElementById("splitter-left"),
    document.getElementById("col-left"),
    document.getElementById("col-center"),
    "left",
  );

  // Right splitter resizes the right column (center adapts automatically)
  _makeSplitter(
    document.getElementById("splitter-right"),
    document.getElementById("col-center"),
    document.getElementById("col-right"),
    "right",
  );
});
