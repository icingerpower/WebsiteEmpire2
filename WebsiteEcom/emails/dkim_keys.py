"""
RSA DKIM keypair generation (TICKET-040, ADR-034 "Keypair + DNS records shown
in admin").

Uses `cryptography` (already a project dependency, ADR-006 §7 — reused as-is,
no new crypto library added for this ticket) to generate a 2048-bit RSA
keypair once, at SenderDomain creation time (see SenderDomain.save() in
emails/models.py).
"""

import base64

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa

# 2048 bits per ADR-034 — the standard DKIM key size (RFC 6376 recommends at
# least 1024, 2048 is the current best-practice minimum most providers require).
DKIM_KEY_SIZE_BITS = 2048
DKIM_PUBLIC_EXPONENT = 65537


def generate_dkim_keypair() -> tuple[str, str]:
    """
    Generate an RSA keypair for DKIM signing.

    Returns (private_key_pem, public_key_b64_body):
    - private_key_pem: PKCS8 PEM string. Callers store this in
      SenderDomain.dkim_private_key (EncryptedCharField — encrypted at rest,
      never shown in admin).
    - public_key_b64_body: base64-encoded DER SubjectPublicKeyInfo, with no
      PEM headers/footers or embedded newlines. This is exactly the string
      published in the 'p=' tag of the DKIM TXT record
      (SenderDomain.expected_dkim_record_value).
    """
    private_key = rsa.generate_private_key(
        public_exponent=DKIM_PUBLIC_EXPONENT,
        key_size=DKIM_KEY_SIZE_BITS,
    )
    private_pem = private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    ).decode()

    public_der = private_key.public_key().public_bytes(
        encoding=serialization.Encoding.DER,
        format=serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    public_b64 = base64.b64encode(public_der).decode()

    return private_pem, public_b64
