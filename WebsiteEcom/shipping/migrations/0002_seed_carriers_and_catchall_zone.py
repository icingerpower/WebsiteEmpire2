"""
Data migration: seed common carriers and a catch-all zone.

Carriers pre-seeded:
  UPS, FedEx, USPS, DHL — each with their tracking URL template.

Zone pre-seeded:
  "Rest of World" — catch-all zone (country_codes=[]) for any buyer
  country not covered by a more specific zone.
"""

from django.db import migrations


CARRIERS = [
    {
        "name": "UPS",
        "tracking_url_template": (
            "https://wwwapps.ups.com/WebTracking/track"
            "?track=yes&trackNums={tracking_number}"
        ),
    },
    {
        "name": "FedEx",
        "tracking_url_template": (
            "https://www.fedex.com/fedextrack/?trknbr={tracking_number}"
        ),
    },
    {
        "name": "USPS",
        "tracking_url_template": (
            "https://tools.usps.com/go/TrackConfirmAction"
            "?tLabels={tracking_number}"
        ),
    },
    {
        "name": "DHL",
        "tracking_url_template": (
            "https://www.dhl.com/en/express/tracking.html"
            "?AWB={tracking_number}"
        ),
    },
]


def seed_carriers_and_zone(apps, schema_editor):
    ShippingCarrier = apps.get_model("shipping", "ShippingCarrier")
    ShippingZone = apps.get_model("shipping", "ShippingZone")

    for carrier_data in CARRIERS:
        ShippingCarrier.objects.get_or_create(
            name=carrier_data["name"],
            defaults={"tracking_url_template": carrier_data["tracking_url_template"]},
        )

    ShippingZone.objects.get_or_create(
        name="Rest of World",
        defaults={"country_codes": [], "is_active": True},
    )


def reverse_seed(apps, schema_editor):
    ShippingCarrier = apps.get_model("shipping", "ShippingCarrier")
    ShippingZone = apps.get_model("shipping", "ShippingZone")

    carrier_names = [c["name"] for c in CARRIERS]
    ShippingCarrier.objects.filter(name__in=carrier_names).delete()
    ShippingZone.objects.filter(name="Rest of World", country_codes=[]).delete()


class Migration(migrations.Migration):

    dependencies = [
        ("shipping", "0001_initial"),
    ]

    operations = [
        migrations.RunPython(seed_carriers_and_zone, reverse_code=reverse_seed),
    ]
