/*
 * badges.widgets.BadgePresetSelect — shows the custom_image upload field only
 * when the "custom" preset card is selected, hides it otherwise (ADR-030 D5).
 *
 * Vanilla JS + plain CSS, no build step (matches the T029-B convention
 * already used elsewhere in this admin — e.g. the chat widget config toggle).
 * Progressive enhancement only: if this script fails to load, both the
 * preset grid and the custom_image field simply stay visible — the
 * server-side clean() validation in badges.models.SecurityBadge is the real
 * enforcement, this is presentation only.
 */
(function () {
  "use strict";

  function customImageRow() {
    return document.querySelector(".field-custom_image");
  }

  function applyVisibility(selectedValue) {
    var row = customImageRow();
    if (!row) {
      return;
    }
    row.style.display = selectedValue === "custom" ? "" : "none";
  }

  function currentSelection() {
    var checked = document.querySelector('input[data-badge-preset]:checked');
    return checked ? checked.value : "";
  }

  function init() {
    applyVisibility(currentSelection());
    document.addEventListener("change", function (evt) {
      if (evt.target && evt.target.matches && evt.target.matches("input[data-badge-preset]")) {
        applyVisibility(evt.target.value);
      }
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
