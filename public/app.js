(function () {
  function markLoaded(id) {
    var el = document.getElementById(id);
    if (el) {
      el.classList.add("loaded");
      el.textContent = el.textContent + " ✓";
    }
  }

  markLoaded("status-html");
  markLoaded("status-css");
  markLoaded("status-js");
})();
