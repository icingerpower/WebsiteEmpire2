"""
Reusable form fields for the Pradize ecommerce admin.
"""

import json

from django import forms
from django.contrib.admin.widgets import FilteredSelectMultiple


class JsonMultipleChoiceField(forms.MultipleChoiceField):
    """
    MultipleChoiceField backed by a JSONField that stores a list of strings.

    Uses Django's built-in FilteredSelectMultiple widget (the dual-list selector
    already present in every Django admin installation) — no extra packages needed.

    Round-trip contract:
      - prepare_value: DB Python list (or legacy JSON string) → list of codes
      - to_python: submitted list of codes → plain Python list (stored by JSONField as-is)

    Usage in a ModelAdmin form::

        from core.fields import JsonMultipleChoiceField
        from core.choices import CURRENCY_CHOICES

        class MyForm(forms.ModelForm):
            settlement_currencies = JsonMultipleChoiceField(
                choices=CURRENCY_CHOICES,
                label='Settlement currencies',
            )
    """

    def __init__(self, choices, label, **kwargs):
        widget = FilteredSelectMultiple(label, is_stacked=False)
        super().__init__(choices=choices, widget=widget, required=False, **kwargs)

    def prepare_value(self, value):
        """
        Convert the DB value (Python list or legacy JSON string) to a list of codes
        so FilteredSelectMultiple can mark the correct options as selected.
        """
        if isinstance(value, str):
            try:
                value = json.loads(value)
            except (json.JSONDecodeError, TypeError):
                return []
        return value or []

    def to_python(self, value):
        """
        Return the submitted selection as a plain Python list.

        The parent MultipleChoiceField.to_python() validates each item is in choices
        and returns a list. We convert to list() to ensure consistent type regardless
        of the parent's concrete return type.
        """
        if not value:
            return []
        return list(super().to_python(value))
