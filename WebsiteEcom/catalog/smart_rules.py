"""
Smart collection rule engine (pure Python, no Django models imported at module level).

Rule dict format:
    {"field": "title|price|tag", "op": "contains|equals|greater_than|less_than|starts_with", "value": "..."}

Supported fields:
    title       — matched against product.title (case-insensitive)
    tag         — matched against product.tags list (case-insensitive)
    price       — matched against the lowest active variant price (base price, v1).
                  Multi-currency matching PENDING (Part B): v1 uses store-default-currency
                  base price only.

Supported operators per field:
    title:  contains, equals, starts_with
    tag:    contains (membership check — value must be in the tags list)
    price:  greater_than, less_than, equals

Empty rules list always returns False (no match) to avoid accidentally populating a
collection that has not been configured yet.
"""

from decimal import Decimal, InvalidOperation


def _evaluate_rule(product, rule: dict) -> bool:
    """Evaluate a single rule dict against a product. Returns True if the rule matches."""
    field = rule.get("field", "")
    op = rule.get("op", "")
    value = rule.get("value", "")

    if field == "title":
        title = product.title.lower()
        v = str(value).lower()
        if op == "contains":
            return v in title
        if op == "equals":
            return title == v
        if op == "starts_with":
            return title.startswith(v)
        return False

    if field == "tag":
        tags = [t.lower() for t in (product.tags or [])]
        return str(value).lower() in tags

    if field == "price":
        # Use for_store() to obtain a regular QuerySet (the related manager returns a
        # _RaisingQuerySet that raises IsolationError on iteration — ADR-001 §4).
        # Scoping by store + product gives the same rows as product.variants, but via
        # a safe QuerySet path.
        from catalog.models import ProductVariant

        prices = list(
            ProductVariant.objects.for_store(product.store).filter(
                product=product, is_active=True
            ).values_list("price", flat=True)
        )
        if not prices:
            return False
        lowest = min(prices)
        try:
            v = Decimal(str(value))
        except InvalidOperation:
            return False
        if op == "greater_than":
            return lowest > v
        if op == "less_than":
            return lowest < v
        if op == "equals":
            return lowest == v
        return False

    return False


def evaluate_product(product, rules: list, match: str = "all") -> bool:
    """
    Returns True if the product satisfies the rules.

    - Empty rules always returns False (unconfigured collection should not auto-fill).
    - match='all': every rule must match (AND semantics).
    - match='any': at least one rule must match (OR semantics).
    """
    if not rules:
        return False
    results = [_evaluate_rule(product, rule) for rule in rules]
    return any(results) if match == "any" else all(results)


def apply_smart_rules(collection) -> list:
    """
    Returns a list of Product instances matching the collection's smart rules.

    Uses .for_store() to ensure cross-store isolation (ADR-001 §4).
    Only considers products with status='active'.
    """
    from catalog.models import Product

    # Do NOT use .prefetch_related("variants") here: the ProductVariant related
    # manager returns a _RaisingQuerySet (ADR-001 §4), and Django's prefetch machinery
    # iterates that queryset internally, causing IsolationError.
    # _evaluate_rule for "price" now queries ProductVariant.objects.for_store() directly,
    # so the prefetch is no longer needed.
    products = list(
        Product.objects.for_store(collection.store)
        .filter(status="active")
    )
    return [
        p
        for p in products
        if evaluate_product(p, collection.smart_rules, collection.smart_rules_match)
    ]
