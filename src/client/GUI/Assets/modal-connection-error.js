document.addEventListener("DOMContentLoaded", () => {
  const errorDialog = document.getElementById("connection-error-dialog");
  const errorOkBtn  = document.getElementById("connection-error-ok-btn");

  if (!errorDialog || !errorOkBtn) return;

  errorOkBtn.addEventListener("click", (e) => {
    e.preventDefault();
    window.closeModal(errorDialog);
  });

  // Close on click outside the article
  errorDialog.addEventListener("click", (e) => {
    const rect = errorDialog.querySelector("article").getBoundingClientRect();
    const inside =
      e.clientX >= rect.left && e.clientX <= rect.right &&
      e.clientY >= rect.top  && e.clientY <= rect.bottom;
    if (!inside) window.closeModal(errorDialog);
  });
});
