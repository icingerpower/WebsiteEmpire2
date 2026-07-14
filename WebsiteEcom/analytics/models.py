"""
Analytics app models — live in the 'analytics' database (ADR-004).

Design invariants:
- No FKs into the main DB. All cross-DB references are soft integer IDs.
- Event rows are raw ingest; purged after ANALYTICS_EVENT_RETENTION_DAYS days.
- AggregatedMetric rows are written by nightly aggregation (TICKET-013) and
  are never purged.
- BeaconDlq holds failed async ingest batches for operator replay.
"""

from django.db import models


class EventType(models.TextChoices):
    """
    Closed vocabulary per ADR-004 §2 v1. Do NOT add entries without updating the ADR.
    """
    PAGE_VIEW = 'page_view', 'Page view'
    PRODUCT_IMPRESSION = 'product_impression', 'Product impression'
    PRODUCT_CLICK = 'product_click', 'Product click'
    COLLECTION_IMPRESSION = 'collection_impression', 'Collection impression'
    COLLECTION_SCROLL_DEPTH = 'collection_scroll_depth', 'Collection scroll depth'
    ADD_TO_CART = 'add_to_cart', 'Add to cart'
    CHECKOUT_START = 'checkout_start', 'Checkout start'
    PURCHASE = 'purchase', 'Purchase'


class Event(models.Model):
    """
    Single analytics event row.

    Soft IDs: store_id, product_id, collection_id, order_id are plain integers
    with no FK constraints — the analytics DB has no FK relationships into the
    main DB (ADR-004).

    customer_local_hour is denormalized at ingest time (0–23) to avoid timezone
    joins at query time.

    properties holds event-specific payload (e.g. scroll depth, click target).
    """

    event_type = models.CharField(max_length=50, choices=EventType.choices, db_index=True)

    # Soft reference to stores.Store.pk — no FK constraint (ADR-004).
    store_id = models.IntegerField(db_index=True)
    session_id = models.CharField(max_length=255, db_index=True)

    # Soft references — no FK constraints.
    product_id = models.IntegerField(null=True, blank=True)
    collection_id = models.IntegerField(null=True, blank=True)
    order_id = models.IntegerField(null=True, blank=True)

    # UTM attribution (first-touch, copied from session at ingest time).
    utm_source = models.CharField(max_length=255, blank=True, default='')
    utm_medium = models.CharField(max_length=255, blank=True, default='')
    utm_campaign = models.CharField(max_length=255, blank=True, default='')
    utm_term = models.CharField(max_length=255, blank=True, default='')
    utm_content = models.CharField(max_length=255, blank=True, default='')
    referrer = models.CharField(max_length=500, blank=True, default='')
    landing_page = models.CharField(max_length=500, blank=True, default='')

    # Denormalized at ingest (0–23 in the customer's local timezone).
    customer_local_hour = models.SmallIntegerField(null=True, blank=True)

    # Page URL where the event occurred (set by the beacon or server-side ingest).
    url = models.CharField(max_length=2048, blank=True, default='')

    # Client context — populated at ingest time by the beacon JS.
    device_type = models.CharField(max_length=20, blank=True, default='')
    country = models.CharField(max_length=2, blank=True, default='')

    # Numeric value — order total for purchase events, ad spend for impression events.
    value = models.DecimalField(max_digits=12, decimal_places=2, null=True, blank=True)

    # Event-specific payload (e.g. {'scroll_depth': 80, 'product_id': 42}).
    properties = models.JSONField(default=dict)

    created_at = models.DateTimeField(auto_now_add=True, db_index=True)

    class Meta:
        app_label = 'analytics'
        indexes = [
            models.Index(fields=['store_id', 'event_type', 'created_at']),
        ]

    def __str__(self):
        return f"{self.event_type} store={self.store_id} session={self.session_id[:12]}"


class AggregatedMetric(models.Model):
    """
    Pre-aggregated metric, written by the nightly aggregation job (TICKET-013).

    Retained indefinitely after raw Event rows are purged.

    dimension_key holds a stringified dimension value (e.g. a UTM source or
    product ID) so that multiple dimensions can reuse this single table.
    """

    metric_type = models.CharField(max_length=100, db_index=True)

    # Soft reference to stores.Store.pk — no FK constraint.
    store_id = models.IntegerField(db_index=True)

    period_start = models.DateField()
    period_end = models.DateField()

    # Dimension value — blank means 'total / no breakdown'.
    dimension_key = models.CharField(max_length=255, blank=True, default='')

    value = models.DecimalField(max_digits=20, decimal_places=4)

    class Meta:
        app_label = 'analytics'
        indexes = [
            models.Index(fields=['store_id', 'metric_type', 'period_start']),
        ]

    def __str__(self):
        return f"{self.metric_type} store={self.store_id} {self.period_start}→{self.period_end}"


class BeaconDlq(models.Model):
    """
    Dead-letter queue for analytics beacon payloads that failed to ingest.

    Rows are written by the async Celery task (analytics.tasks.ingest_beacon_event)
    after it exhausts its retry budget (3 retries, 30 s apart).

    payload_json holds the full batch dict: {store_id, session_id, events: [...]}.
    Re-driven by: python manage.py replay_beacon_dlq

    Invariants:
    - Must always use using='analytics' — lives in the analytics DB (ADR-004).
    - No FK constraints; store_id is a soft integer reference.
    - resolved=True means the payload was successfully re-ingested; rows are
      never deleted so operators retain a full audit trail.
    """

    payload_json = models.JSONField()
    error_message = models.TextField()
    # attempt_count starts at 1 (one failed ingest cycle); incremented by replay.
    attempt_count = models.PositiveSmallIntegerField(default=1)
    last_attempted_at = models.DateTimeField(auto_now=True)
    created_at = models.DateTimeField(auto_now_add=True, db_index=True)
    resolved = models.BooleanField(default=False)

    class Meta:
        app_label = 'analytics'
        indexes = [
            models.Index(fields=['resolved', 'created_at']),
        ]

    def __str__(self):
        store_id = self.payload_json.get('store_id', '?') if isinstance(self.payload_json, dict) else '?'
        return f"BeaconDlq store={store_id} resolved={self.resolved} created={self.created_at}"
