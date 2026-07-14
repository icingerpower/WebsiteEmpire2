"""
Signal consumer for slug changes and product/collection creation (TICKET-016, ADR-005 §6).

Listens to the `slug_changed` signal from catalog.signals and:
    1. Updates the existing Permalink row to the new slug (queryset .update() to avoid
       IsolationError with the StoreScopedManager in signal context).
    2. Delegates loop-rejection / chain-collapse / SlugRedirect creation to the shared
       permalinks.redirects.create_slug_redirect() helper (§XV-4) — the same helper used
       by catalog.signals._sync_translation_permalink for translated-slug changes, so
       both paths behave identically (STATIC_PAGES_SEO_REVIEW.md H2).

Draft product gating (ADR-005):
    on_product_created fires only when instance.status == Product.STATUS_ACTIVE.
    on_collection_created fires only when instance.is_published == True.
    Draft objects get no Permalink at creation time; the Permalink is registered when
    they are published (requires a separate publish signal or view — Phase 2).

URL-path conventions (ADR-005, ADR-014 §10-G1, ADR-018 D1):
    Product     slug 'my-shirt'    → path 'my-shirt'          (root-level, no prefix)
    Collection  slug 'bestsellers' → path 'collections/bestsellers'
    StaticPage  slug 'about-us'    → path 'about-us'          (root-level, no prefix)

StaticPage creation/publish permalink lifecycle lives in pages/signals.py, not here —
this module only extends handle_slug_change (slug-rename case) with a StaticPage branch.
"""

from django.db import transaction
from django.db.models.signals import post_save
from django.dispatch import receiver

from catalog.models import Collection, Product
from catalog.signals import slug_changed
from pages.models import StaticPage
from permalinks.models import Permalink
from permalinks.redirects import create_slug_redirect
from permalinks.registry import register_collection, register_product


@receiver(slug_changed)
def handle_slug_change(sender, instance, old_slug, new_slug, **kwargs):
    """
    Respond to a slug change on a Product, Collection, or StaticPage (default language).

    All writes happen inside a single atomic transaction:
    1. Update the canonical Permalink to the new slug path.
    2. Delegate to permalinks.redirects.create_slug_redirect() for loop rejection,
       chain-collapse, and SlugRedirect creation (see that function's docstring).

    Step 1 fetches the row via .for_store(store) (a real, filtered queryset — the
    StoreScopedManager isolation guard only blocks the UNSCOPED base manager) and
    mutates+saves the INSTANCE rather than issuing a queryset .update(). This is
    required, not cosmetic (security audit F1): a bare .update(slug=new_path) writes
    straight to the DB with zero model validation, so a rename to a reserved slug
    (e.g. 'cart', 'fr') would bypass Permalink.save()'s reserved-slug check entirely.
    Routing through .save() means a rename to a reserved slug raises ValidationError
    here, inside the pre_save-triggered slug_changed signal — which aborts the
    surrounding save_base() transaction, so neither the Permalink nor the renamed
    StaticPage/Product/Collection row is persisted.
    """
    if sender is Product:
        old_path = old_slug
        new_path = new_slug
    elif sender is Collection:
        old_path = f"collections/{old_slug}"
        new_path = f"collections/{new_slug}"
    elif sender is StaticPage:
        # ADR-018 D1: StaticPage is root-level, like Product — no path prefix.
        old_path = old_slug
        new_path = new_slug
    else:
        # Unknown sender — not a slug-bearing model managed here.
        return

    store = instance.store
    lang = store.primary_language

    with transaction.atomic():
        # 1. Update the canonical Permalink to the new slug — via .save() (not
        #    queryset .update()) so Permalink.save()'s reserved-slug validation
        #    (security audit F1) actually runs; see the docstring above.
        permalink = (
            Permalink.objects.for_store(store).filter(slug=old_path, lang=lang).first()
        )
        if permalink is not None:
            permalink.slug = new_path
            permalink.save()

        # 2-5. Loop rejection, chain-collapse, stale-row cleanup, and SlugRedirect
        # creation — delegated to the shared helper (§XV-4) so this path and the
        # translated-slug path (catalog.signals._sync_translation_permalink) can never
        # drift. skip_redirect is set on the instance by ProductAdmin.save_model
        # before calling obj.save() (Finding #12).
        create_slug_redirect(
            store,
            old_path,
            new_path,
            lang,
            skip_redirect=getattr(instance, "_skip_redirect", False),
        )

        # TICKET-042 / ADR-028 §2: a Product slug rename must also rename its
        # ProductPageVersion permalinks in the SAME language, with their own
        # 301 SlugRedirect, so a Pinterest pin at the old version URL still
        # resolves.  Collections/StaticPages never have page versions.
        if sender is Product:
            from permalinks.registry import rename_page_version_permalinks_for_language
            rename_page_version_permalinks_for_language(
                store, instance.pk, lang, old_path, new_path,
            )


@receiver(post_save, sender=Product)
def on_product_created(sender, instance, created, **kwargs):
    """
    Register a canonical Permalink for a newly created Product.

    Only fires when created=True AND the product is active (not draft or archived).
    Draft products get no Permalink at creation time — the Permalink is registered
    when the product is published.
    """
    if created and instance.status == Product.STATUS_ACTIVE:
        register_product(instance)


@receiver(post_save, sender=Collection)
def on_collection_created(sender, instance, created, **kwargs):
    """
    Register a canonical Permalink for a newly created Collection.

    Only fires when created=True AND the collection is published (is_published=True).
    Unpublished collections get no Permalink at creation time.
    """
    if created and instance.is_published:
        register_collection(instance)


@receiver(post_save, sender=Product)
def sync_permalink_on_status_change(sender, instance, created, **kwargs):
    """
    Keep Permalink.is_active in sync with Product.status (Finding #11).

    Rules:
    - status != STATUS_ACTIVE  → deactivate all permalinks for this product so
      the URL resolves to 404 rather than serving a draft/archived page.
    - status == STATUS_ACTIVE and not created → reactivate any inactive permalinks
      (handles the draft-to-active transition).  If no permalink exists at all
      (product was originally created as draft and never had one), register one now.
    - created=True → skip; on_product_created handles the initial active-product case.

    Uses cross_store_unsafe() to bypass the StoreScopedManager isolation guard,
    which is not available in signal context.  The store filter is applied explicitly.
    """
    if created:
        return  # on_product_created handles newly created active products

    from django.contrib.contenttypes.models import ContentType

    ct = ContentType.objects.get_for_model(Product)

    if instance.status != Product.STATUS_ACTIVE:
        # Deactivate all permalinks for this product (draft or archived).
        Permalink.objects.cross_store_unsafe().filter(
            store=instance.store,
            content_type=ct,
            object_id=instance.pk,
        ).update(is_active=False)
    else:
        # Product is active — reactivate any previously deactivated permalinks.
        updated = Permalink.objects.cross_store_unsafe().filter(
            store=instance.store,
            content_type=ct,
            object_id=instance.pk,
            is_active=False,
        ).update(is_active=True)
        # If no permalink exists at all (e.g. product was created as draft and
        # has never been published before), register a fresh one now.
        if updated == 0 and not Permalink.objects.cross_store_unsafe().filter(
            store=instance.store,
            content_type=ct,
            object_id=instance.pk,
        ).exists():
            register_product(instance)
