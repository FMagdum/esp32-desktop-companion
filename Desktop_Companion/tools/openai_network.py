"""Connection setup for the official OpenAI API."""
import http.client
import socket
import ssl
import urllib.request


def prefer_ipv4(address, timeout, source_address=None):
    # Keep DNS, IPv6 fallback, and per-family address order. On a network
    # with broken IPv6, avoid spending the whole API timeout on each address.
    addresses = socket.getaddrinfo(*address, type=socket.SOCK_STREAM)
    addresses.sort(key=lambda entry: entry[0] != socket.AF_INET)
    failure = OSError("No API address found")
    for family, kind, proto, _, target in addresses:
        connection = socket.socket(family, kind, proto)
        try:
            connection.settimeout(min(timeout, 3))
            if source_address:
                connection.bind(source_address)
            connection.connect(target)
            connection.settimeout(timeout)
            return connection
        except OSError as error:
            failure = error
            connection.close()
    raise failure


class OpenAIHTTPSConnection(http.client.HTTPSConnection):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._create_connection = prefer_ipv4


class OpenAIHTTPSHandler(urllib.request.HTTPSHandler):
    def https_open(self, request):
        return self.do_open(OpenAIHTTPSConnection, request, context=self._context)


def openai_urlopen(request, timeout):
    opener = urllib.request.build_opener(OpenAIHTTPSHandler())
    return opener.open(request, timeout=timeout)


def openai_websocket(websocket, url, key, timeout):
    options = {"header": ["Authorization: Bearer " + key],
               "timeout": timeout, "suppress_origin": True}
    if urllib.request.getproxies().get("https"):
        return websocket.create_connection(url, **options)
    # The WebSocket client accepts a preconnected socket. Use the original
    # hostname for SNI, certificate validation, and the WebSocket handshake.
    with_socket = prefer_ipv4(("api.openai.com", 443), timeout)
    try:
        tls = ssl.create_default_context().wrap_socket(with_socket, server_hostname="api.openai.com")
    except BaseException:
        with_socket.close()
        raise
    try:
        return websocket.create_connection(url, socket=tls, **options)
    except BaseException:
        tls.close()
        raise
