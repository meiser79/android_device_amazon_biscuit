#!/usr/bin/env python3
"""Package one image as a TWRP zip that writes it to recovery, expdb or boot.

    make-flash-zip.py {recovery|expdb|boot} IMAGE OUTPUT.zip

boot writes the active slot's boot partition through TWRP's
/dev/block/current-boot, after checking it against amzn_bcbtool.

The zip runs update-binary from this folder: it checks the image's
sha256, backs the partition up to /tmp, writes it and reads it back.
"""

import hashlib
import os
import sys
import zipfile

TARGETS = ("recovery", "expdb", "boot")
UPDATE_BINARY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "update-binary")


def add(archive, name, data, mode):
    info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
    info.external_attr = mode << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    archive.writestr(info, data)


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in TARGETS:
        sys.exit(__doc__.split("\n\n")[1])
    target, image_path, output = sys.argv[1:]
    image = open(image_path, "rb").read()
    with zipfile.ZipFile(output, "w") as archive:
        add(archive, "META-INF/com/google/android/update-binary",
            open(UPDATE_BINARY, "rb").read(), 0o755)
        add(archive, "META-INF/com/google/android/updater-script", b"# dummy\n", 0o644)
        add(archive, "target", (target + "\n").encode(), 0o644)
        add(archive, "image.img", image, 0o644)
        add(archive, "image.sha256",
            ("%s  image.img\n" % hashlib.sha256(image).hexdigest()).encode(), 0o644)
    print("%s: writes %s (%d bytes)" % (output, target, len(image)))


if __name__ == "__main__":
    main()
