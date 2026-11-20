import gzip
import hashlib
from pathlib import Path

Import("env")

source = Path(env.subst("$PROJECT_DIR")) / "src" / "web" / "Faucet.css"
build = Path(env.subst("$BUILD_DIR"))
build.mkdir(parents=True, exist_ok=True)
css = source.read_bytes()
compressed = gzip.compress(css, compresslevel=9, mtime=0)
version = hashlib.sha256(css).hexdigest()[:12]
lines = [
    "#pragma once",
    f'#define FAUCET_WEB_CSS_VERSION "{version}"',
    "static constexpr unsigned char kFaucetCss[] = {",
]
for offset in range(0, len(compressed), 16):
    lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in compressed[offset:offset + 16]) + ",")
lines.extend(["};", ""])
header = "\n".join(lines)
target = build / "FaucetCss.generated.h"
if not target.exists() or target.read_text() != header:
    target.write_text(header)
env.Append(CPPPATH=[str(build)])
