"""
Checkout forms (ADR-015 §8 Phase 2; ADR-017 address i18n).

Forms:
  ContactForm  — email, phone, newsletter opt-in (spec CO-003, CO-002).
  AddressForm  — shipping address fields (spec SA-001...SA-005, ML-003).
  ShippingForm — shipping rate selection (spec SM-006, OF-002).

All forms work as plain HTML forms (NFR-1 — core flow must work without JavaScript).
JS is progressive enhancement only.

Notes:
  - AddressForm.country choices are populated from active shipping zones at construction
    time so the form only offers countries where the store actually ships (SA-002).
  - AddressForm widget and labels adapt to the render-country in __init__ (ADR-017 D2).
    Authoritative per-country validation runs in clean() from the *submitted* country
    regardless of the JS-enhanced client state (XV-5 — validation at persistence
    boundaries; NFR-1 — core flow works without JavaScript).
  - ContactForm.phone: the store setting checkout_phone_mode controls whether phone
    is optional (default) or required (CO-003). The view passes required_phone=True
    when the setting is 'required'.
  - ShippingForm.shipping_rate_id choices are built from the server-resolved rate list
    for the stored shipping address country. The view MUST re-resolve before rendering
    and re-validate after submission (OF-002 — client rate_id is never trusted for price).
"""

import re

from django import forms
from django.utils.translation import gettext_lazy as _

from storefront.address_meta import (
    get_country_meta,
    normalize_postal_code,
    normalize_subdivision,
)


# ---------------------------------------------------------------------------
# ContactForm
# ---------------------------------------------------------------------------

class ContactForm(forms.Form):
    """
    Step 1 — Contact information.

    Fields:
      email             required — customer email for order confirmation and recovery.
      phone             optional by default (required when checkout_phone_mode='required').
      newsletter_opt_in optional — GDPR-compliant marketing opt-in (unchecked by default).
    """

    email = forms.EmailField(
        required=True,
        max_length=254,
        widget=forms.EmailInput(attrs={'autocomplete': 'email', 'inputmode': 'email'}),
        error_messages={
            'required': _("Please enter your email address."),
            'invalid': _("Please enter a valid email address."),
        },
    )
    phone = forms.CharField(
        required=False,
        max_length=50,
        widget=forms.TextInput(attrs={'autocomplete': 'tel', 'inputmode': 'tel'}),
        help_text=_('Optional. Used for delivery notifications.'),
    )
    newsletter_opt_in = forms.BooleanField(
        required=False,
        initial=False,
        label=_('Sign up for news and special offers'),
    )

    def __init__(self, *args, required_phone: bool = False, hidden_phone: bool = False, **kwargs):
        """
        required_phone: pass True when checkout_phone_mode='required' (CO-003).
        hidden_phone:   pass True when checkout_phone_mode='hidden'; removes the field
                        entirely so it is never rendered or validated.
        """
        super().__init__(*args, **kwargs)
        if hidden_phone:
            del self.fields['phone']
        elif required_phone:
            self.fields['phone'].required = True
            self.fields['phone'].error_messages = {'required': _('Please enter your phone number.')}
            # The "Optional." lead-in is wrong once the field is required — the
            # label carries the required marker instead (checkout.html).
            self.fields['phone'].help_text = _('Used for delivery notifications.')


# ---------------------------------------------------------------------------
# AddressForm
# ---------------------------------------------------------------------------

class AddressForm(forms.Form):
    """
    Step 2 — Shipping address (ADR-015 + ADR-017).

    The address JSON stored on CheckoutState uses the Order snapshot shape:
      {name, line1, line2, city, state, postal_code, country}
    where name = "{first_name} {last_name}".strip().

    country choices are populated from active shipping zones at construction
    time (SA-002) so only serviceable countries appear in the dropdown.
    When no shipping_zones are provided, all fields are still valid (useful for tests).

    ADR-017 adaptive behaviour:
      __init__ resolves a render-country (bound data → initial → DEFAULT_META) and
      configures the state widget (Select/TextInput/HiddenInput), state label, postal
      label and postal placeholder accordingly.  This is cosmetic only — it does NOT
      affect validation.

      clean() is the authoritative path: it reads the *submitted* country, fetches meta,
      normalizes postal_code and state, and enforces per-country rules regardless of
      what the JS-enhanced client did (XV-5, NFR-1).

      self.subdivision_mode (set in __init__, mirrors meta.subdivision_mode) lets
      checkout.html hide the whole state row for 'none'-mode countries (SG/HK)
      instead of rendering an orphan label above a HiddenInput.

    state.required is always False at field level; requiredness for 'select' mode
    countries is enforced in clean() so the widget choice never accidentally tightens
    or relaxes validation.
    """

    first_name = forms.CharField(
        required=True,
        max_length=100,
        widget=forms.TextInput(attrs={'autocomplete': 'given-name'}),
        error_messages={'required': _('Please enter your first name.')},
    )
    last_name = forms.CharField(
        required=True,
        max_length=100,
        widget=forms.TextInput(attrs={'autocomplete': 'family-name'}),
        error_messages={'required': _('Please enter your last name.')},
    )
    line1 = forms.CharField(
        required=True,
        max_length=255,
        label=_('Address'),
        widget=forms.TextInput(attrs={'autocomplete': 'address-line1'}),
        error_messages={'required': _('Please enter your address.')},
    )
    line2 = forms.CharField(
        required=False,
        max_length=255,
        label=_('Apartment, suite, etc.'),
        widget=forms.TextInput(attrs={'autocomplete': 'address-line2'}),
    )
    city = forms.CharField(
        required=True,
        max_length=100,
        widget=forms.TextInput(attrs={'autocomplete': 'address-level2'}),
        error_messages={'required': _('Please enter your city.')},
    )
    state = forms.CharField(
        required=False,
        max_length=100,
        label=_('State / Province'),
        widget=forms.TextInput(attrs={'autocomplete': 'address-level1'}),
    )
    postal_code = forms.CharField(
        required=True,
        max_length=20,
        label=_('Postal code'),
        widget=forms.TextInput(attrs={'autocomplete': 'postal-code'}),
        error_messages={'required': _('Please enter your postal code.')},
    )
    country = forms.ChoiceField(
        required=True,
        choices=[],
        widget=forms.Select(attrs={'autocomplete': 'country'}),
        error_messages={'required': _('Please select your country.')},
    )

    def __init__(self, *args, country_choices=None, **kwargs):
        """
        country_choices: list of (code, label) tuples for the country dropdown.
        When None, a placeholder is shown ("Select country").

        Render-country resolution (ADR-017 D2): bound data country → initial
        country → DEFAULT_META.  The resolved meta configures:
          state widget   — Select (select mode), TextInput (text), HiddenInput (none)
          state.label    — subdivision_label from meta
          postal_code.label — postal_label from meta
          postal placeholder/pattern attrs — from meta
        """
        super().__init__(*args, **kwargs)

        if country_choices:
            self.fields['country'].choices = [('', _('Select country'))] + list(country_choices)
        else:
            self.fields['country'].choices = [('', _('Select country'))]

        # Resolve render-country: bound data → initial → DEFAULT_META
        render_country = ''
        if self.is_bound:
            render_country = self.data.get(self.add_prefix('country'), '') or ''
        if not render_country:
            render_country = (self.initial or {}).get('country', '') or ''

        meta = get_country_meta(render_country)

        # Exposed so checkout.html can hide the whole state row for 'none' mode
        # countries (e.g. SG/HK) instead of rendering an orphan label above a
        # HiddenInput with nothing to label (Group 4b).
        self.subdivision_mode = meta.subdivision_mode

        # Configure state widget and label
        if meta.subdivision_mode == 'select':
            self.fields['state'].widget = forms.Select(
                choices=[('', '---------')] + list(meta.subdivisions),
                attrs={'autocomplete': 'address-level1'},
            )
        elif meta.subdivision_mode == 'none':
            self.fields['state'].widget = forms.HiddenInput()
        else:
            # text mode (default)
            self.fields['state'].widget = forms.TextInput(
                attrs={'autocomplete': 'address-level1'},
            )
        self.fields['state'].label = meta.subdivision_label

        # Configure postal_code label and placeholder/pattern attrs
        self.fields['postal_code'].label = meta.postal_label
        postal_attrs = {
            'autocomplete': 'postal-code',
            'placeholder': meta.postal_example,
        }
        if meta.postal_pattern:
            postal_attrs['pattern'] = meta.postal_pattern
        self.fields['postal_code'].widget = forms.TextInput(attrs=postal_attrs)

    def clean(self):
        """
        Authoritative per-country postal and subdivision validation (ADR-017 D2, XV-5).

        Reads cleaned_data['country'] (the *submitted* value — correct even when
        the shopper changed country mid-form with JS disabled).  The country field
        is declared after postal_code in the class body, so per-field cleans run
        in declaration order; using cross-field logic here avoids a hidden ordering
        dependency on clean_postal_code() / clean_state().

        Actions:
          1. postal_code — normalize then validate against postal_pattern.
          2. state — 'select': required, normalize to canonical code;
                     'text':   strip whitespace, optional;
                     'none':   force to '' (clear stale JS-set values).
        """
        cleaned = super().clean()
        country = cleaned.get('country', '')
        meta = get_country_meta(country)

        # ── postal_code ───────────────────────────────────────────────────
        raw_postal = cleaned.get('postal_code', '')
        if raw_postal:
            normalized = normalize_postal_code(country, raw_postal)
            cleaned['postal_code'] = normalized
            if meta.postal_pattern and not re.fullmatch(meta.postal_pattern, normalized):
                self.add_error(
                    'postal_code',
                    _('Enter a valid %(label)s (e.g. %(example)s).') % {
                        'label': str(meta.postal_label),
                        'example': meta.postal_example,
                    },
                )

        # ── state ─────────────────────────────────────────────────────────
        raw_state = cleaned.get('state', '')
        if meta.subdivision_mode == 'select':
            if not raw_state:
                self.add_error(
                    'state',
                    _('Please select your %(label)s.') % {
                        'label': str(meta.subdivision_label),
                    },
                )
            else:
                canonical = normalize_subdivision(meta, raw_state)
                if canonical is None:
                    self.add_error(
                        'state',
                        _('Please select a valid %(label)s.') % {
                            'label': str(meta.subdivision_label),
                        },
                    )
                else:
                    cleaned['state'] = canonical
        elif meta.subdivision_mode == 'text':
            cleaned['state'] = raw_state.strip()
        elif meta.subdivision_mode == 'none':
            # Force to '' regardless of what was submitted (e.g. stale JS value
            # from a previous country selection that had subdivisions).
            cleaned['state'] = ''

        return cleaned

    def to_address_dict(self) -> dict:
        """
        Return the cleaned data as an address snapshot dict matching the Order shape.

        {name, line1, line2, city, state, postal_code, country}

        Values are already canonical (normalized postal code, normalized subdivision
        code) after clean() — no further processing needed.
        """
        d = self.cleaned_data
        name = f"{d.get('first_name', '')} {d.get('last_name', '')}".strip()
        return {
            'name': name,
            'line1': d.get('line1', ''),
            'line2': d.get('line2', ''),
            'city': d.get('city', ''),
            'state': d.get('state', ''),
            'postal_code': d.get('postal_code', ''),
            'country': d.get('country', ''),
        }


# ---------------------------------------------------------------------------
# ShippingForm
# ---------------------------------------------------------------------------

class ShippingForm(forms.Form):
    """
    Step 3 — Shipping method selection.

    shipping_rate_id: the PK of the selected ShippingRate, presented as a radio
    button group. The view must re-resolve available rates after submission and
    verify that the submitted PK is in the resolved set (OF-002 — server re-validates).

    SM-002: rates are always cheapest-first. SM-003: the form does NOT sort —
    shipping_rates must already be sorted by shipping.service.resolve_shipping_rates
    (which uses shipping.service.rate_sort_key: price, then estimated_days_max,
    then name) so there is a single source of truth for rate ordering and the
    "cheapest" tie-break can never disagree between the form and the resolver.
    """

    shipping_rate_id = forms.ChoiceField(
        required=True,
        widget=forms.RadioSelect(),
        error_messages={'required': _('Please select a shipping method.')},
    )

    def __init__(self, *args, shipping_rates=None, **kwargs):
        """
        shipping_rates: list of ShippingRate objects for the buyer's destination,
        already sorted by shipping.service.resolve_shipping_rates (SM-003 — this
        form must not re-sort; see class docstring).

        When no initial['shipping_rate_id'] is given, the first (cheapest) rate
        is pre-selected by setting self.initial (SM-002).
        """
        super().__init__(*args, **kwargs)
        rates = list(shipping_rates or [])
        if rates:
            self.fields['shipping_rate_id'].choices = [
                (str(r.pk), f"{r.name} — {r.price}")
                for r in rates
            ]
            # SM-002: pre-select the cheapest rate when no prior selection exists.
            if not self.initial.get('shipping_rate_id'):
                self.initial['shipping_rate_id'] = str(rates[0].pk)
        else:
            self.fields['shipping_rate_id'].choices = []

    def get_rate_pk(self) -> int | None:
        """Return the selected shipping rate PK as an int, or None if invalid."""
        try:
            return int(self.cleaned_data['shipping_rate_id'])
        except (KeyError, ValueError, TypeError):
            return None
