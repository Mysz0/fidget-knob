#!/usr/bin/env python3
"""One-time DIRIGERA pairing.

The hub only issues a token to a client that proves physical presence: it
returns one just once, within 60 seconds of its action button being pressed.
That is impractical to do on a headless device with no keyboard, so pairing
happens here on a PC and the resulting token is compiled into the firmware.

Usage:
    python3 tools/pair.py 192.168.1.50

Prints the token and certificate ready to paste into include/config.h, and
offers to write them there directly.

Only dependency is `requests`:
    pip3 install requests
"""

import argparse
import base64
from pathlib import Path
import hashlib
import random
import re
import socket
import string
import subprocess
import sys
import time
import urllib3

try:
    import requests
except ImportError:
    sys.exit("This needs `requests`. Install it with:  pip3 install requests")

# The hub's certificate is self-signed, so verification necessarily fails
# during pairing - there is no way to know the certificate before fetching it.
# We fetch it here and pin it in the firmware, which is where it matters.
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

PORT = 8443
AUDIENCE = "homesmart.local"


def generate_code_verifier() -> str:
    alphabet = string.ascii_letters + string.digits + "-._~"
    return "".join(random.choice(alphabet) for _ in range(128))


def code_challenge_for(verifier: str) -> str:
    digest = hashlib.sha256(verifier.encode()).digest()
    return base64.urlsafe_b64encode(digest).decode().rstrip("=")


def fetch_certificate(host: str) -> str:
    """Retrieves the CA chain so the firmware can pin it.

    The hub's own device certificate is NOT self-signed - it sits at the
    bottom of a 4-certificate chain, each link issued by the next, ending in
    a self-signed root. Pinning only the device leaf fails outright: mbedTLS
    needs an issuer's public key to verify a signature, and a leaf cannot
    verify itself.

    Pinning just the self-signed root is the textbook-correct fix, but this
    Arduino core's TLS stack does not reliably walk multiple intermediate
    hops using what the peer sends during the handshake - confirmed by
    testing against a real hub. Returning every non-leaf certificate in the
    chain, so the complete path to the root already exists in the firmware's
    own trust store, is what actually works.

    Python's ssl module only exposes the peer's own leaf certificate, not the
    intermediates it sent, so this shells out to openssl instead.
    """
    result = subprocess.run(
        ["openssl", "s_client", "-connect", f"{host}:{PORT}", "-showcerts"],
        input="",
        capture_output=True,
        text=True,
        timeout=15,
    )

    pem_blocks = re.findall(
        r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
        result.stdout,
        re.DOTALL,
    )
    if len(pem_blocks) < 2:
        sys.exit(
            "Could not fetch a certificate chain from the hub (need at least "
            "a device leaf plus one issuer).\n"
            "Is openssl installed, and is the hub reachable at "
            f"{host}:{PORT}?"
        )

    # Certificate 0 is the hub's own device leaf, unique per hub and not a
    # CA - skip it. Every certificate after that is a shared, long-lived
    # issuer, ending in the self-signed root.
    return "\n".join(pem_blocks[1:]) + "\n"


def request_auth_code(host: str, challenge: str) -> str:
    response = requests.get(
        f"https://{host}:{PORT}/v1/oauth/authorize",
        params={
            "audience": AUDIENCE,
            "response_type": "code",
            "code_challenge": challenge,
            "code_challenge_method": "S256",
        },
        verify=False,
        timeout=10,
    )
    response.raise_for_status()
    return response.json()["code"]


def exchange_for_token(host: str, code: str, verifier: str) -> str:
    response = requests.post(
        f"https://{host}:{PORT}/v1/oauth/token",
        data={
            "code": code,
            "code_verifier": verifier,
            "name": socket.gethostname(),
            "grant_type": "authorization_code",
        },
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        verify=False,
        timeout=10,
    )

    if response.status_code == 403:
        sys.exit(
            "Hub refused: the action button was not pressed in time.\n"
            "Run this again and press it as soon as you are prompted."
        )
    response.raise_for_status()
    return response.json()["access_token"]


def as_c_string_literal(pem: str) -> str:
    """Formats a PEM block as a multi-line C string literal."""
    lines = [line for line in pem.strip().splitlines() if line]
    body = " \\\n".join(f'  "{line}\\n"' for line in lines)
    return f"#define DIRIGERA_CERT_PEM \\\n{body}\n"


def patch_config(path: str, host: str, token: str, pem: str) -> bool:
    try:
        with open(path, "r") as handle:
            source = handle.read()
    except FileNotFoundError:
        return False

    source = re.sub(
        r'#define DIRIGERA_HOST\s+"[^"]*"',
        f'#define DIRIGERA_HOST "{host}"',
        source,
    )
    source = re.sub(
        r'#define DIRIGERA_TOKEN\s+"[^"]*"',
        f'#define DIRIGERA_TOKEN "{token}"',
        source,
    )
    # The certificate define spans continued lines, so it is matched as a block
    # up to the first line that does not end in a backslash.
    source = re.sub(
        r"#define DIRIGERA_CERT_PEM(?:.*\\\n)*.*\n",
        as_c_string_literal(pem),
        source,
    )
    # Pinning is now possible, so the bring-up escape hatch must not be left on.
    source = re.sub(
        r"^(\s*)#define DIRIGERA_INSECURE",
        r"\1// #define DIRIGERA_INSECURE",
        source,
        flags=re.MULTILINE,
    )

    with open(path, "w") as handle:
        handle.write(source)
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description="Pair with an IKEA DIRIGERA hub.")
    parser.add_argument("host", help="Hub IP address on your LAN")
    parser.add_argument(
        "--config",
        default=str(Path(__file__).resolve().parent.parent / "include" / "config.h"),
        help="config.h to update in place (default: ../include/config.h next to this script)",
    )
    args = parser.parse_args()

    print(f"Fetching certificate from {args.host}:{PORT} ...")
    pem = fetch_certificate(args.host)

    verifier = generate_code_verifier()
    code = request_auth_code(args.host, code_challenge_for(verifier))

    print()
    print("  >>> PRESS THE ACTION BUTTON ON THE BOTTOM OF THE HUB NOW <<<")
    print()
    # No interactive prompt here - this runs headless. Give a generous fixed
    # window instead of waiting on a keypress.
    for remaining in range(25, 0, -1):
        print(f"\r  waiting... {remaining:2d}s ", end="", flush=True)
        time.sleep(1)
    print()

    token = exchange_for_token(args.host, code, verifier)

    print("\nPaired.\n")
    print(f'#define DIRIGERA_HOST "{args.host}"')
    print(f'#define DIRIGERA_TOKEN "{token}"')
    print()
    print(as_c_string_literal(pem))

    if patch_config(args.config, args.host, token, pem):
        print(f"Updated {args.config}.")
    else:
        print(
            f"Could not find {args.config}. "
            "Copy include/config.h.example to include/config.h first, "
            "then paste the values above."
        )


if __name__ == "__main__":
    main()
