"""
Shipping app: ShippingCarrier, ShippingZone, ShippingRate, ShippingRateException.

Design decisions (TICKET-032, ADR-001 §3b):
- ShippingCarrier and ShippingZone are platform-global, super-admin owned.
  They are NOT store-owned and are listed in EXEMPT_MODELS.
- ShippingRate and ShippingRateException are store-owned (StoreOwnedModel).
  Rates are configured per store, referencing platform-level zones and carriers.
- ShippingRateException.clean() enforces exactly one of product/collection is set.
- Zone resolution (most-specific-first) lives in service.py.
"""

from django.core.exceptions import ValidationError
from django.db import models

from core.models import StoreOwnedModel


class ShippingCarrier(models.Model):
    """
    Platform-level carrier definition (ADR-001 §3b — super-admin owned).
    Stores reference carriers when configuring shipping rates.

    tracking_url_template uses {tracking_number} as the placeholder.
    Call tracking_url(tracking_number) to resolve a fully-formed tracking URL.
    """

    name = models.CharField(max_length=100, unique=True)
    tracking_url_template = models.CharField(
        max_length=500,
        blank=True,
        help_text=(
            "URL template with {tracking_number} placeholder. "
            "e.g. https://tracking.ups.com/track/{tracking_number}"
        ),
    )
    is_active = models.BooleanField(default=True)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "shipping carrier"
        verbose_name_plural = "shipping carriers"
        ordering = ["name"]

    def __str__(self):
        return self.name

    def tracking_url(self, tracking_number: str) -> str:
        """Return the tracking URL for a tracking number, or empty string if no template."""
        if not self.tracking_url_template:
            return ""
        return self.tracking_url_template.replace("{tracking_number}", tracking_number)


class ShippingZone(models.Model):
    """
    Platform-level geographic zone (ADR-001 §3b — super-admin owned).

    country_codes: JSON list of ISO 3166-1 alpha-2 codes covered by this zone.
    A special zone with country_codes=[] represents "All Countries" (catch-all).

    Resolution rule (most-specific-first): a zone with explicit country_codes
    is more specific than "All Countries" and wins when a buyer's country is listed.
    """

    name = models.CharField(max_length=100)
    country_codes = models.JSONField(
        default=list,
        help_text=(
            "ISO 3166-1 alpha-2 codes. Empty list = 'All Countries' catch-all zone."
        ),
    )
    service_description = models.TextField(
        blank=True,
        default="",
        help_text=(
            "Optional customer-facing note shown at checkout for rates in this zone, "
            "e.g. 'Ships from EU warehouse — no customs'. Shared by ALL stores shipping "
            "to this zone — keep it operational and store-neutral, never store-branded."
        ),
    )
    is_active = models.BooleanField(default=True)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "shipping zone"
        verbose_name_plural = "shipping zones"
        ordering = ["name"]

    def __str__(self):
        return self.name

    @property
    def is_catch_all(self) -> bool:
        return not self.country_codes


class ShippingRateType(models.TextChoices):
    FLAT = "flat", "Flat Rate"
    WEIGHT_BASED = "weight", "Weight Based"
    FREE = "free", "Free Shipping"
    CARRIER_CALCULATED = "carrier", "Carrier Calculated"


class ShippingRate(StoreOwnedModel):
    """
    Per-store shipping rate for a zone.

    A store configures its own rates referencing platform-level zones and carriers.
    rate_type determines how the price is computed:
    - FLAT: fixed price regardless of weight.
    - WEIGHT_BASED: price applies when total weight is between min/max_weight_grams.
    - FREE: price is 0 (free shipping).
    - CARRIER_CALCULATED: stub for future carrier API integration.
    """

    zone = models.ForeignKey(
        ShippingZone,
        on_delete=models.PROTECT,
        related_name="rates",
    )
    carrier = models.ForeignKey(
        ShippingCarrier,
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="rates",
    )
    name = models.CharField(
        max_length=100,
        help_text='Display name shown to buyer, e.g. "Standard Shipping", "Express".',
    )
    rate_type = models.CharField(
        max_length=20,
        choices=ShippingRateType.choices,
        default=ShippingRateType.FLAT,
    )
    price = models.DecimalField(
        max_digits=10,
        decimal_places=2,
        default=0,
        help_text="Flat/free price. Set to 0 for FREE rate type.",
    )
    min_weight_grams = models.PositiveIntegerField(
        default=0,
        help_text="Minimum weight in grams for WEIGHT_BASED rates.",
    )
    max_weight_grams = models.PositiveIntegerField(
        null=True,
        blank=True,
        help_text="Maximum weight in grams for WEIGHT_BASED rates. Null = no upper bound.",
    )
    estimated_days_min = models.PositiveSmallIntegerField(null=True, blank=True)
    estimated_days_max = models.PositiveSmallIntegerField(null=True, blank=True)
    is_active = models.BooleanField(default=True)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "shipping rate"
        verbose_name_plural = "shipping rates"

    def __str__(self):
        return f"{self.name} ({self.zone})"


class ShippingRateException(StoreOwnedModel):
    """
    Per-product or per-collection shipping rate exception.

    Overrides the standard rate for specific products or collections.
    Exactly one of product or collection must be set — enforced by clean().
    exception_type must match which FK is set.
    """

    shipping_rate = models.ForeignKey(
        ShippingRate,
        on_delete=models.CASCADE,
        related_name="exceptions",
    )
    product = models.ForeignKey(
        "catalog.Product",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="shipping_exceptions",
    )
    collection = models.ForeignKey(
        "catalog.Collection",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="shipping_exceptions",
    )
    override_price = models.DecimalField(
        max_digits=10,
        decimal_places=2,
        help_text="The overriding shipping price for this exception.",
    )
    exception_type = models.CharField(
        max_length=50,
        choices=[("product", "Product"), ("collection", "Collection")],
    )

    class Meta:
        verbose_name = "shipping rate exception"
        verbose_name_plural = "shipping rate exceptions"

    def __str__(self):
        target = self.product or self.collection
        return f"Exception on {self.shipping_rate} for {target}"

    def clean(self):
        """Require exactly one of product or collection to be set."""
        has_product = self.product_id is not None
        has_collection = self.collection_id is not None

        if has_product and has_collection:
            raise ValidationError(
                "A shipping rate exception must target either a product or a collection, not both."
            )
        if not has_product and not has_collection:
            raise ValidationError(
                "A shipping rate exception must target either a product or a collection."
            )
