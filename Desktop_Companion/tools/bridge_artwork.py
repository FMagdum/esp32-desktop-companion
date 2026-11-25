"""Download public music thumbnails and convert them for the Pod's JPEG decoder."""
from functools import lru_cache
from io import BytesIO
import re
import time
from urllib.error import URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

MAX_DOWNLOAD = 2 * 1024 * 1024
SIDE = 140
DOMAINS = ("ytimg.com", "googleusercontent.com", "ggpht.com")


def allowed_source(url):
    try:
        parsed = urlsplit(url)
        host = parsed.hostname or ""
        return (url.startswith("https://") and len(url) <= 767 and bool(parsed.path)
                and parsed.netloc == host
                and any(host == domain or host.endswith("." + domain) for domain in DOMAINS))
    except ValueError:
        return False


class ThumbnailRedirects(HTTPRedirectHandler):
    max_redirections = 2
    max_repeats = 1

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not allowed_source(newurl):
            raise URLError("Unsupported thumbnail redirect")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def download(url):
    request = Request(url, headers={"Accept": "image/*", "Accept-Encoding": "identity"})
    deadline = time.monotonic() + 5
    with build_opener(ThumbnailRedirects()).open(request, timeout=4) as response:
        if int(response.headers.get("Content-Length") or 0) > MAX_DOWNLOAD:
            raise ValueError("Artwork too large")
        data = bytearray()
        while len(data) <= MAX_DOWNLOAD and time.monotonic() < deadline:
            chunk = response.read1(min(16384, MAX_DOWNLOAD + 1 - len(data)))
            if not chunk:
                return bytes(data)
            data.extend(chunk)
    raise ValueError("Artwork download exceeded its limit")


def baseline_jpeg(data):
    from PIL import Image, ImageOps

    Image.init()
    supported = tuple(fmt for fmt in ("JPEG", "PNG", "WEBP", "AVIF") if fmt in Image.OPEN)
    try:
        image_file = Image.open(BytesIO(data), formats=supported)
    except Image.DecompressionBombError as error:
        raise ValueError("Artwork dimensions out of range") from error
    with image_file as image:
        if not 0 < image.width * image.height <= 4_000_000:
            raise ValueError("Artwork dimensions out of range")
        # Match the Pod's square crop, then encode in the decoder's supported format.
        square = ImageOps.fit(image.convert("RGB"), (SIDE, SIDE), Image.Resampling.LANCZOS)
        output = BytesIO()
        square.save(output, format="JPEG", quality=85, progressive=False, optimize=False)
        return output.getvalue()


@lru_cache(maxsize=32)
def artwork(video_id, source):
    if not re.fullmatch(r"[A-Za-z0-9_-]{11}", video_id):
        raise ValueError("Invalid video ID")
    fallback = f"https://i.ytimg.com/vi/{video_id}/mqdefault.jpg"
    candidates = (source, fallback) if source != fallback and allowed_source(source) else (fallback,)
    for url in candidates:
        try:
            return baseline_jpeg(download(url))
        except (OSError, ValueError, SyntaxError):
            continue
    raise ValueError("No usable thumbnail for this track")
