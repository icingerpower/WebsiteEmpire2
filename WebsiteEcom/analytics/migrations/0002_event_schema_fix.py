# Generated migration for T012 — fix EventType choices and add new Event columns.

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('analytics', '0001_initial'),
    ]

    operations = [
        # Replace the closed EventType enum choices (removes add_to_cart, begin_checkout,
        # pixel_fire, scroll, click; adds product_impression, product_click,
        # collection_impression, collection_scroll_depth, checkout_start, search).
        migrations.AlterField(
            model_name='event',
            name='event_type',
            field=models.CharField(
                choices=[
                    ('page_view', 'Page view'),
                    ('product_view', 'Product view'),
                    ('product_impression', 'Product impression'),
                    ('product_click', 'Product click'),
                    ('collection_view', 'Collection view'),
                    ('collection_impression', 'Collection impression'),
                    ('collection_scroll_depth', 'Collection scroll depth'),
                    ('checkout_start', 'Checkout start'),
                    ('purchase', 'Purchase'),
                    ('search', 'Search'),
                ],
                db_index=True,
                max_length=50,
            ),
        ),
        # Page URL where the event occurred.
        migrations.AddField(
            model_name='event',
            name='url',
            field=models.CharField(blank=True, default='', max_length=2048),
        ),
        # Client device category (mobile/tablet/desktop).
        migrations.AddField(
            model_name='event',
            name='device_type',
            field=models.CharField(blank=True, default='', max_length=20),
        ),
        # ISO 3166-1 alpha-2 country code.
        migrations.AddField(
            model_name='event',
            name='country',
            field=models.CharField(blank=True, default='', max_length=2),
        ),
        # Numeric value: order total for purchase, ad spend for impression events.
        migrations.AddField(
            model_name='event',
            name='value',
            field=models.DecimalField(blank=True, decimal_places=2, max_digits=12, null=True),
        ),
    ]
