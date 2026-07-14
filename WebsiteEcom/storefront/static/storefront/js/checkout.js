/**
 * Checkout accordion — client-side section expand/collapse (ADR-015 §6).
 *
 * Each completed checkout section (contact, address, shipping) keeps its form
 * in the DOM but is marked checkout-section--collapsed when the step has moved
 * past it.  CSS hides .checkout-section--collapsed .checkout-section__form-body.
 * Clicking an Edit button calls checkoutAccordion.openSection(sectionId) to
 * remove the collapsed state so the shopper can re-edit without a server round-trip.
 *
 * This object is defined at module scope (window) so that inline onclick
 * attributes on the edit buttons can reference it before the Stripe IIFE runs.
 */
window.checkoutAccordion = {
  openSection: function (sectionId) {
    var section = document.querySelector('[data-checkout-section="' + sectionId + '"]');
    if (!section) { return; }
    section.classList.remove('checkout-section--collapsed');
    section.classList.add('checkout-section--open');
    var firstField = section.querySelector('input, select, textarea');
    if (firstField) { firstField.focus(); }
  },
};

/**
 * Stripe Elements — two initialization paths (ADR-015 §6, PY-003).
 *
 * Path A — pre-loaded client_secret (non-AJAX fallback, checkout_payment.html):
 *   When checkout_payment.html renders with data-client-secret already in the DOM,
 *   Stripe Elements is mounted on page load.  The #payment-fallback-submit button
 *   calls stripe.confirmPayment() directly.  This path exits before wiring Path B.
 *
 * Path B — 2-step AJAX checkout (standard checkout.html flow):
 *   Step 1: "Pay now" intercepts the form submit and POSTs to /checkout/pay/ via
 *           AJAX with the X-Requested-With header.  The server creates the Order
 *           and PaymentIntent and returns { client_secret, return_url } (or
 *           { redirect_url } for zero-total / duplicate orders, or { error }).
 *   Step 2: On a successful JSON response with a client_secret, initialize
 *           Stripe Elements, mount #payment-element, and immediately call
 *           stripe.confirmPayment().  Stripe handles 3DS and redirects to
 *           return_url on completion.
 *
 * Zero-total / duplicate: the server returns { redirect_url }; JS redirects
 * the browser directly with no Stripe interaction.
 *
 * Error: the server returns { error }; displayed in #payment-errors.
 *
 * Graceful degradation: when data-publishable-key is absent (step < PAYMENT)
 * or Stripe.js is not loaded, the script exits early and the form submits
 * normally (non-JS fallback preserved — NFR-1).
 */
(function () {
  'use strict';

  // Publishable key lives on the #payment-element mount point container.
  var container = document.querySelector('[data-publishable-key]');
  if (!container) return;

  var publishableKey = container.dataset.publishableKey;
  if (!publishableKey) return;

  // Stripe.js must be loaded before this script runs.
  if (typeof Stripe === 'undefined') return;

  var stripe = Stripe(publishableKey);

  // ---------------------------------------------------------------------------
  // Path A: pre-loaded client_secret (non-AJAX fallback — checkout_payment.html)
  //
  // The server has already rendered a client_secret into data-client-secret on the
  // page container.  Mount Elements immediately and wire up the fallback pay button.
  // ---------------------------------------------------------------------------
  var pageContainer = document.querySelector('[data-client-secret]');
  var preloadedSecret = pageContainer ? pageContainer.dataset.clientSecret : '';

  if (preloadedSecret) {
    var preElements = stripe.elements({ clientSecret: preloadedSecret });
    var prePaymentEl = preElements.create('payment');
    prePaymentEl.mount('#payment-element');

    var fallbackBtn = document.getElementById('payment-fallback-submit');
    if (fallbackBtn) {
      fallbackBtn.addEventListener('click', function () {
        fallbackBtn.disabled = true;
        var returnUrl = pageContainer.dataset.returnUrl || '';
        stripe.confirmPayment({
          elements: preElements,
          confirmParams: { return_url: returnUrl },
        }).then(function (result) {
          if (result.error) {
            showError(result.error.message);
            fallbackBtn.disabled = false;
          }
          // On success Stripe redirects to return_url automatically.
        });
      });
    }
    // Path A is active — do not also wire up the AJAX path.
    return;
  }

  // ---------------------------------------------------------------------------
  // Path B: 2-step AJAX checkout (standard checkout.html flow)
  // ---------------------------------------------------------------------------
  var form = document.getElementById('payment-form');
  if (!form) return;

  // Translated fallback copy (Group 3c) — checkout.html renders these via
  // {% trans %} since this JS file cannot. Falls back to English when the
  // data attribute is absent (e.g. older cached HTML) so behavior degrades
  // gracefully rather than showing "undefined".
  var errorInitMsg = form.dataset.errorInit || 'Payment could not be initialized. Please try again.';
  var errorProcessingMsg = form.dataset.errorProcessing || 'Payment could not be processed. Please try again.';

  form.addEventListener('submit', function (e) {
    e.preventDefault();

    var submitBtn = form.querySelector('[data-testid="payment-submit"]');
    if (submitBtn) submitBtn.disabled = true;

    // Step 1: create Order + PaymentIntent via AJAX POST.
    fetch(form.action, {
      method: 'POST',
      body: new FormData(form),
      headers: {
        'X-Requested-With': 'XMLHttpRequest',
      },
    })
      .then(function (r) {
        if (!r.ok && r.status !== 400) {
          throw new Error('Payment setup failed (' + r.status + ')');
        }
        return r.json();
      })
      .then(function (data) {
        if (data.error) {
          showError(data.error);
          if (submitBtn) submitBtn.disabled = false;
          return;
        }

        // Zero-total or duplicate order: server returns a direct redirect URL.
        if (data.redirect_url) {
          window.location.href = data.redirect_url;
          return;
        }

        if (!data.client_secret) {
          showError(errorInitMsg);
          if (submitBtn) submitBtn.disabled = false;
          return;
        }

        // Step 2: mount Stripe Elements and confirm immediately.
        var elements = stripe.elements({ clientSecret: data.client_secret });
        var paymentEl = elements.create('payment');
        paymentEl.mount('#payment-element');

        stripe.confirmPayment({
          elements: elements,
          confirmParams: {
            return_url: data.return_url,
          },
        }).then(function (result) {
          if (result.error) {
            showError(result.error.message);
            if (submitBtn) submitBtn.disabled = false;
          }
          // On success Stripe redirects to return_url automatically.
        });
      })
      .catch(function () {
        showError(errorProcessingMsg);
        if (submitBtn) submitBtn.disabled = false;
      });
  });

  function showError(msg) {
    var errEl = document.getElementById('payment-errors');
    if (errEl) errEl.textContent = msg;
  }
}());

/**
 * Address section — adaptive country fields (ADR-017 §4, D3).
 *
 * On country <select> change: fetches
 * /checkout/address/country-meta/?country=XX&lang=YY, then updates:
 *   - state field: swap between <select> (select mode), <input type="text"> (text mode),
 *     or hidden + row hidden (none mode).  The `name` attribute stays "state" throughout.
 *   - state label text
 *   - postal code label text (preserving the required asterisk)
 *   - postal code placeholder and HTML pattern attribute
 *
 * Progressive enhancement: silently exits when the country select is absent
 * (e.g. a page that does not include the address form) — NFR-1.
 */
(function () {
  'use strict';

  var countrySelect = document.getElementById('id_country');
  if (!countrySelect) { return; }

  function applyMeta(meta) {
    var stateRow = document.querySelector('[data-address-state-row]');

    if (stateRow) {
      if (meta.subdivision_mode === 'none') {
        stateRow.style.display = 'none';
        var hiddenField = stateRow.querySelector('[name="state"]');
        if (hiddenField) { hiddenField.value = ''; }
      } else {
        stateRow.style.display = '';
        var existingField = stateRow.querySelector('[name="state"]');
        var prevValue = existingField ? existingField.value : '';

        if (meta.subdivision_mode === 'select') {
          // Build a <select> and replace whatever widget is currently there.
          var sel = document.createElement('select');
          sel.name = 'state';
          sel.id = 'id_state';
          sel.setAttribute('autocomplete', 'address-level1');
          if (existingField) { sel.className = existingField.className; }

          var emptyOpt = document.createElement('option');
          emptyOpt.value = '';
          emptyOpt.textContent = '---------';
          sel.appendChild(emptyOpt);

          for (var i = 0; i < meta.subdivisions.length; i++) {
            var opt = document.createElement('option');
            opt.value = meta.subdivisions[i][0];
            opt.textContent = meta.subdivisions[i][1];
            if (opt.value === prevValue) { opt.selected = true; }
            sel.appendChild(opt);
          }

          if (existingField && existingField.parentNode) {
            existingField.parentNode.replaceChild(sel, existingField);
          }

        } else {
          // text mode — restore <input type="text"> if a <select> OR a
          // hidden input (switching away from a 'none'-mode country) is
          // present. Checking only tagName === 'select' missed the hidden
          // case entirely, leaving a non-interactive hidden field in place.
          var needsTextSwap = existingField && (
            existingField.tagName.toLowerCase() === 'select' ||
            existingField.type === 'hidden'
          );
          if (needsTextSwap) {
            var inp = document.createElement('input');
            inp.type = 'text';
            inp.name = 'state';
            inp.id = 'id_state';
            inp.setAttribute('autocomplete', 'address-level1');
            inp.className = existingField.className;
            if (existingField.parentNode) {
              existingField.parentNode.replaceChild(inp, existingField);
            }
          }
        }

        // Update the state label text.
        var stateLabel = stateRow.querySelector('label');
        if (stateLabel) { stateLabel.textContent = meta.subdivision_label; }
      }
    }

    // Update postal code label (preserve the required asterisk " *").
    var postalRow = document.querySelector('[data-address-postal-row]');
    if (postalRow) {
      var postalLabel = postalRow.querySelector('label');
      if (postalLabel) { postalLabel.textContent = meta.postal_label + ' *'; }
    }

    // Update postal code placeholder and HTML pattern attribute.
    var postalInput = document.getElementById('id_postal_code');
    if (postalInput) {
      postalInput.placeholder = meta.postal_example || '';
      if (meta.postal_pattern) {
        postalInput.setAttribute('pattern', meta.postal_pattern);
      } else {
        postalInput.removeAttribute('pattern');
      }
    }
  }

  countrySelect.addEventListener('change', function () {
    var country = countrySelect.value;
    if (!country) { return; }
    // ADR-021 §Country-meta: this endpoint is fetched via a bare, un-prefixed
    // /checkout/... URL (correct — checkout routes never carry the /fr/ prefix),
    // so the request itself carries no language signal. Pass the storefront's
    // active language explicitly via `lang` — base.html always sets
    // <html lang="..."> from request.locale.language.lang_code, so this reflects
    // the language the shopper is actually browsing under, not request.locale's
    // (possibly root, on a path-prefixed store) language.
    var lang = document.documentElement.lang || '';
    fetch(
      '/checkout/address/country-meta/?country=' + encodeURIComponent(country) +
      '&lang=' + encodeURIComponent(lang)
    )
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (meta) { if (meta) { applyMeta(meta); } })
      .catch(function () {
        // Fail silently — the form still works server-side (NFR-1).
      });
  });
}());
