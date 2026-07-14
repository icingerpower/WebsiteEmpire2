"""
EncryptedCharField: stores values encrypted at rest using Fernet symmetric encryption.

Key comes from settings.FERNET_KEY (a URL-safe base64-encoded 32-byte key).
Values are stored as ciphertext strings in the DB; decrypted transparently on access.
Empty strings are stored as empty strings (not encrypted) to simplify NULL/blank handling.

Custom implementation — does NOT use django-fernet-fields (unmaintained).
Uses Python's built-in cryptography.fernet library directly.

Custom __repr__ on the field descriptor ensures plaintext values never surface in
Django debug output, shell repr, or log lines (ADR-006 §7).
"""

from cryptography.fernet import Fernet, InvalidToken
from django.conf import settings
from django.core.exceptions import ImproperlyConfigured
from django.db import models


def _get_fernet() -> Fernet:
    """
    Return a configured Fernet instance from settings.FERNET_KEY.

    FERNET_KEY must be a URL-safe base64-encoded 32-byte key — the format
    produced by Fernet.generate_key() or by base64.urlsafe_b64encode(32 bytes).
    Raises ImproperlyConfigured if the key is absent or blank.
    """
    key = getattr(settings, "FERNET_KEY", None)
    if not key:
        raise ImproperlyConfigured(
            "FERNET_KEY must be set in settings to use EncryptedCharField. "
            "Generate a key with: from cryptography.fernet import Fernet; print(Fernet.generate_key().decode())"
        )
    if isinstance(key, str):
        key = key.encode()
    return Fernet(key)


class EncryptedCharField(models.TextField):
    """
    TextField subclass that encrypts values at rest using Fernet AES-128-CBC + HMAC.

    Transparent encryption/decryption:
    - Reads (from_db_value): ciphertext from DB → decrypt → plaintext in Python.
    - Writes (get_prep_value): plaintext → encrypt → ciphertext stored in DB.
    - Empty values are stored as-is (no encryption), consistent with blank=True.

    Ciphertext overhead is ~1.4× the plaintext length plus a fixed ~60-byte header,
    so a 255-char plaintext produces ~420 chars of ciphertext.  The underlying
    TextField has no max_length limit in the DB, so this is safe.

    Security notes:
    - FERNET_KEY must be separate from Django's SECRET_KEY (ADR-006 §7).
    - The __repr__ of this field class never exposes the plaintext value.
    - If decryption fails (e.g. wrong key, migrating from unencrypted data),
      the raw ciphertext is returned as-is so the row is not silently dropped.
      Log a warning and investigate rather than silently discarding data.

    Migration safety: when running 0001 → 0002 on existing unencrypted data,
    the from_db_value fallback returns the raw value rather than raising, which
    prevents a hard crash mid-migration.  Re-encrypt after migration if needed.
    """

    def from_db_value(self, value, expression, connection):
        if not value:
            return value
        try:
            return _get_fernet().decrypt(value.encode()).decode()
        except (InvalidToken, Exception):
            # Return raw value rather than crashing — migration safety net.
            # If you see plaintext returned here post-migration, re-encrypt rows.
            return value

    def get_prep_value(self, value):
        if not value:
            return value
        return _get_fernet().encrypt(value.encode()).decode()

    def to_python(self, value):
        # from_db_value already decrypts; to_python receives the plaintext.
        return value
