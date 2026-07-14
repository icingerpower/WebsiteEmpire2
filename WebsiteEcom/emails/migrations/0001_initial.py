# Generated migration for TICKET-022: transactional email system.

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    initial = True

    dependencies = [
        ('orders', '0007_order_model_gaps'),
        ('stores', '0005_store_password_protection'),
    ]

    operations = [
        migrations.CreateModel(
            name='EmailTemplate',
            fields=[
                (
                    'id',
                    models.BigAutoField(
                        auto_created=True,
                        primary_key=True,
                        serialize=False,
                        verbose_name='ID',
                    ),
                ),
                (
                    'template_id',
                    models.CharField(
                        max_length=100,
                        help_text=(
                            "Stable identifier matching the trigger (e.g. 'order_confirmation'). "
                            "Never change once data exists."
                        ),
                    ),
                ),
                (
                    'subject',
                    models.CharField(
                        max_length=255,
                        help_text='Subject line; may contain Django template variables.',
                    ),
                ),
                (
                    'body_html',
                    models.TextField(
                        help_text='HTML email body; rendered as a Django template.',
                    ),
                ),
                (
                    'body_text',
                    models.TextField(
                        blank=True,
                        help_text=(
                            'Plain-text email body (optional). When blank, falls back to the '
                            'file-based .txt template or the HTML body is sent only.'
                        ),
                    ),
                ),
                (
                    'is_active',
                    models.BooleanField(
                        default=True,
                        help_text='When False, the platform default template is used instead.',
                    ),
                ),
                (
                    'store',
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.PROTECT,
                        to='stores.store',
                    ),
                ),
            ],
            options={
                'verbose_name': 'email template',
                'verbose_name_plural': 'email templates',
            },
        ),
        migrations.CreateModel(
            name='SentEmail',
            fields=[
                (
                    'id',
                    models.BigAutoField(
                        auto_created=True,
                        primary_key=True,
                        serialize=False,
                        verbose_name='ID',
                    ),
                ),
                (
                    'template_id',
                    models.CharField(
                        max_length=100,
                        help_text='Template identifier matching the trigger.',
                    ),
                ),
                ('recipient_email', models.EmailField(max_length=254)),
                (
                    'subject',
                    models.CharField(
                        max_length=255,
                        help_text='Final rendered subject (not the template string).',
                    ),
                ),
                (
                    'sent_at',
                    models.DateTimeField(auto_now_add=True),
                ),
                (
                    'status',
                    models.CharField(
                        choices=[
                            ('sent', 'Sent'),
                            ('failed', 'Failed'),
                            ('bounced', 'Bounced'),
                        ],
                        default='sent',
                        max_length=20,
                    ),
                ),
                (
                    'error_message',
                    models.TextField(
                        blank=True,
                        help_text='Exception message when status=failed.',
                    ),
                ),
                (
                    'order',
                    models.ForeignKey(
                        blank=True,
                        null=True,
                        on_delete=django.db.models.deletion.SET_NULL,
                        related_name='sent_emails',
                        to='orders.order',
                        help_text='Associated order (null for non-order emails like welcome).',
                    ),
                ),
                (
                    'store',
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.PROTECT,
                        to='stores.store',
                    ),
                ),
            ],
            options={
                'verbose_name': 'sent email',
                'verbose_name_plural': 'sent emails',
            },
        ),
        migrations.AddIndex(
            model_name='sentemail',
            index=models.Index(
                fields=['store', 'sent_at'],
                name='emails_sent_store_i_e01234_idx',
            ),
        ),
        migrations.AddIndex(
            model_name='sentemail',
            index=models.Index(
                fields=['order', 'template_id'],
                name='emails_sent_order_i_e01235_idx',
            ),
        ),
        migrations.AlterUniqueTogether(
            name='emailtemplate',
            unique_together={('store', 'template_id')},
        ),
    ]
