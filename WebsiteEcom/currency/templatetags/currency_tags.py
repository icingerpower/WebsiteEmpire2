"""
Currency template tags/filters (ADR-023 §4, §5, §6, TICKET-031).

Registered as a Django template "builtin" (webecom/settings/base.py TEMPLATES
OPTIONS['builtins']) so every storefront template can use these WITHOUT a
{% load currency_tags %} line — this keeps price-line edits in templates
owned by other tickets/agents (e.g. product.html) to the exact price
expression, with no accompanying {% load %} edit required.

{{ amount|display_price:request }}
    Converts `amount` (Decimal, in the store's own transactional currency) to
    the shopper's resolved display currency (ADR-023 §4) via the single
    resolver currency.resolver.display_amount(), then formats it with the
    display currency's symbol/placement. Returns `amount` COMPLETELY
    UNCHANGED when the resolved display currency equals the store's own
    currency (or when there is no store/request context) — every existing
    template renders byte-identically for a shopper who never picks a
    different currency (ADR-023 §6 "Tests required": byte-identical output
    across surfaces).

{% currency_conversion_note request %}
    Renders the mandatory "approximate, informational only" disclaimer
    (ADR-023 §6) — ONLY when the resolved display currency differs from the
    store's own currency. Renders '' otherwise. Mirrors the always-adjacent
    "All taxes included" note already used on product_card.html/cart.html/
    product.html.

{% currency_picker %}
    Inclusion tag for the header currency picker (ADR-023 §4, component C-07).
    Renders nothing when the store offers 1 or fewer distinct display
    currencies (its own currency + enabled StoreCurrencySetting rows) —
    ADR-023 §3 "the picker simply does not render".
"""

import logging
from decimal import Decimal, InvalidOperation

from django import template
from django.utils.html import format_html
from django.utils.translation import gettext as _

from currency.models import CurrencyDefinition, CurrencyRate, StoreCurrencySetting, SymbolPlacement
from currency.resolver import display_amount
from currency.session import resolve_display_currency

logger = logging.getLogger(__name__)

register = template.Library()


def _format_amount(amount: Decimal, currency_def: CurrencyDefinition) -> str:
    """
    Format `amount` with currency_def's decimal places, symbol placement, and
    — when show_code is on — the ISO code per code_placement (ADR-023 §2
    addendum, human decision 2026-07-11, restores the admin-011 screenshot's
    "Code visibility" control). Symbol and code placement are independent:
    e.g. symbol_placement=suffix + code_placement=suffix -> "45.99 $ CAD".

    This helper is only ever reached from the conversion branch of
    display_price() — the identity (no-conversion) path returns `amount`
    unchanged before calling here, so show_code never affects the
    byte-identical passthrough guarantee for shoppers who never convert.
    """
    text = f"{amount:.{currency_def.decimal_places}f}"
    symbol = currency_def.symbol or currency_def.code
    if currency_def.symbol_placement == SymbolPlacement.SUFFIX:
        base = f"{text} {symbol}"
    else:
        base = f"{symbol}{text}"

    if not currency_def.show_code:
        return base

    code = currency_def.code
    if currency_def.code_placement == SymbolPlacement.SUFFIX:
        return f"{base} {code}"
    return f"{code} {base}"


def _resolve(request):
    """Return (store, store_currency, display_currency) or (None, None, None)."""
    store = getattr(request, "store", None) if request is not None else None
    if store is None:
        return None, None, None
    display_currency = resolve_display_currency(request, store)
    return store, store.default_currency, display_currency


@register.filter(name="display_price")
def display_price(amount, request):
    """
    Convert + format `amount` for the request's resolved display currency.

    Falls back to the plain, unconverted `amount` (identical to the template's
    pre-existing rendering) whenever there is no usable store/request context,
    OR when a conversion is attempted but the referenced CurrencyDefinition/
    CurrencyRate data is missing — logged at ERROR (loud in logs), never a
    500 to the shopper for a purely informational number (ADR-023 §4's
    "never break the page for money-adjacent features" principle).
    """
    if amount is None:
        return amount

    store, store_currency, display_currency = _resolve(request)
    if store is None or display_currency == store_currency:
        return amount

    try:
        converted = display_amount(Decimal(amount), store_currency, display_currency)
        currency_def = CurrencyDefinition.objects.get(code=display_currency)
    except (CurrencyDefinition.DoesNotExist, CurrencyRate.DoesNotExist, InvalidOperation, TypeError) as exc:
        logger.error("display_price: could not convert %r to %r: %s", amount, display_currency, exc)
        return amount

    return format_html("{}", _format_amount(converted, currency_def))


@register.simple_tag(name="currency_conversion_note")
def currency_conversion_note(request):
    """Render the mandatory conversion disclaimer, or '' when no conversion applies."""
    store, store_currency, display_currency = _resolve(request)
    if store is None or display_currency == store_currency:
        return ""
    return format_html(
        '<span class="price-conversion-note" data-testid="currency-conversion-note">{}</span>',
        _("Converted price, informational only."),
    )


@register.inclusion_tag("storefront/partials/currency_picker.html", takes_context=True)
def currency_picker(context):
    """
    Render the header currency picker (ADR-023 §4). Empty when the store
    offers 1 or fewer distinct display currencies.
    """
    request = context.get("request")
    store = getattr(request, "store", None) if request is not None else None
    if store is None:
        return {"currencies": []}

    enabled = list(
        StoreCurrencySetting.objects.for_store(store)
        .filter(is_enabled=True, currency__is_active=True)
        .select_related("currency")
    )
    codes = {store.default_currency}
    codes.update(setting.currency.code for setting in enabled)
    if len(codes) < 2:
        return {"currencies": []}

    current = resolve_display_currency(request, store)
    definitions = {d.code: d for d in CurrencyDefinition.objects.filter(code__in=codes)}

    options = [
        {
            "code": code,
            "label": f"{code} {definitions[code].symbol}".strip() if code in definitions else code,
            "is_current": code == current,
        }
        for code in sorted(codes)
    ]
    return {"currencies": options, "next": request.get_full_path()}
