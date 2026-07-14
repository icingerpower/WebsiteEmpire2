"""
Reusable admin form widgets for the Pradize ecommerce engine.
"""

from django import forms


class DisabledOptionsSelect(forms.Select):
    """
    A <select> widget that renders specific choice values as
    `<option disabled>` instead of hiding them.

    Used for reserved/PENDING enum values that must stay visible in the admin
    (so the field's full vocabulary and the "pending decision" note are
    legible) but must never be actually selectable — e.g.
    GiftCardCampaign.value_mode's percent_of_order/percent_discount_coupon
    (ADR-029 D3) and .trigger_scope's conditions (ADR-029 D2). A disabled
    HTML option cannot be submitted by a normal browser; the model-level
    validator (discounts/validators.py) is still the authoritative,
    defense-in-depth rejection for any POST that bypasses this widget
    (curl, forged form, JS removal of the attribute).

    Usage::

        widget = DisabledOptionsSelect(disabled_values=['percent_of_order'])
    """

    def __init__(self, *args, disabled_values=(), **kwargs):
        self.disabled_values = {str(v) for v in disabled_values}
        super().__init__(*args, **kwargs)

    def create_option(self, name, value, label, selected, index, subindex=None, attrs=None):
        option = super().create_option(name, value, label, selected, index, subindex=subindex, attrs=attrs)
        if str(value) in self.disabled_values:
            option['attrs']['disabled'] = 'disabled'
        return option


class CsvListWidget(forms.Textarea):
    """
    Widget for JSONField(default=list) fields that store a list of strings.

    Renders the stored list as a comma-separated string for human editing.
    Parses the submitted comma-separated string back to a list on save.
    Blank entries and leading/trailing whitespace are stripped.

    Usage in a ModelAdmin form Meta::

        widgets = {
            'tags': CsvListWidget(attrs={'rows': 2, 'placeholder': 'tag1, tag2'}),
        }
    """

    def format_value(self, value):
        if isinstance(value, list):
            return ', '.join(str(v) for v in value)
        return value or ''

    def value_from_datadict(self, data, files, name):
        raw = data.get(name, '')
        return [item.strip() for item in raw.split(',') if item.strip()]


class CsvListFormField(forms.JSONField):
    """
    Form field to pair with CsvListWidget on a model JSONField(default=list)
    (ADR-031 Addendum 3, D3a — CSVLISTWIDGET-JSONFIELD-BOUND-DATA-CRASH).

    Wire it via the ModelForm's ``Meta.field_classes = {"<field_name>":
    CsvListFormField}`` for every field using CsvListWidget.

    Bug this fixes (confirmed by direct reproduction, not just static
    reading): stock ``forms.JSONField`` is built for widgets that show raw
    JSON text, and both of the methods that feed the widget assume that
    contract:

    - ``bound_data(data, initial)`` unconditionally calls ``json.loads(data)``
      on POST. ``CsvListWidget.value_from_datadict()`` returns an
      already-parsed Python ``list`` (by design — it re-splits the submitted
      comma-separated string itself), so ``json.loads(a_list)`` raises
      ``TypeError: the JSON object must be str, bytes or bytearray, not
      list`` — NOT a ``json.JSONDecodeError``, so it is not caught by
      ``bound_data``'s own ``except`` clause. This crashes the change-form
      the instant ANY validation error anywhere on the form forces Django to
      re-render a bound field using this widget (e.g. catalog.ProductAdminForm
      's ``tags``, stores.OrganizationAdminForm's ``coverage_areas_json``).
    ADR-032 D5 closes the two follow-ups TICKET-052 deliberately deferred
    (both confirmed by direct reproduction, not just static reading):

    - ``prepare_value()``: stock ``forms.JSONField.prepare_value()``
      unconditionally ``json.dumps()``s its input, called on EVERY render
      (initial GET included, not just bound POST redisplay) via
      ``BoundField.value()``. That made ``CsvListWidget.format_value()``'s
      ``isinstance(value, list)`` branch — the one that actually produces
      "red, blue" — unreachable: by the time ``format_value()`` ran, the
      list had already been JSON-encoded into a string (a Product with
      ``tags=["red", "blue"]`` rendered its textarea as ``["red", "blue"]``,
      not ``red, blue``, even on a plain, error-free GET). Overridden here to
      pass a ``list`` value straight through (falling back to
      ``JSONField.prepare_value()`` for any other input — disabled fields,
      direct JSON-text values), letting ``format_value()``'s list branch
      finally run.
    - This unmasks a *corruption* bug, not just a display one: with
      ``prepare_value()`` masking emptiness behind the JSON text ``"[]"``,
      an empty ``tags``/``coverage_areas_json`` rendered as the literal
      textarea content ``[]``; on ANY save — including a pure no-op save of
      an untouched, already-empty product — ``CsvListWidget
      .value_from_datadict()`` CSV-split that text into ``['[]']`` and
      persisted it, silently corrupting the field. ``to_python()`` is
      overridden to clean an empty submission to ``[]`` (matching the
      fields' own model default) instead of falling through to
      ``JSONField.to_python()``, which would clean it to ``None`` and fail
      construct_instance's write into a ``null=False`` JSONField. Together
      with ``blank=True`` on ``catalog.Product.tags`` and
      ``stores.Organization.coverage_areas_json`` (validation-only, no SQL
      schema change), an empty submission now round-trips as ``[]`` instead
      of corrupting or rejecting.
    """

    def bound_data(self, data, initial):
        if isinstance(data, list):
            return data
        return super().bound_data(data, initial)

    def prepare_value(self, value):
        if isinstance(value, list):
            return value
        return super().prepare_value(value)

    def to_python(self, value):
        if value in self.empty_values:
            return []
        return super().to_python(value)
