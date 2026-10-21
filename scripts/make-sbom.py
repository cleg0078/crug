#!/usr/bin/env python3
# make-sbom.py - write the SPDX 2.3 SBOM for a Crug release.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# The document describes the install tree package.sh staged. The crug package
# is analysed file by file (SHA1/SHA256 per file plus the SPDX package
# verification code); the GCC baseline and libcrug are named as contained
# packages, because they ship as part of the same install tree.
#
# Usage: scripts/make-sbom.py --version V --stage DIR --tarball FILE --out FILE
#                             [--gcc-version V]
#
#   --version      release version, e.g. 0.1.0
#   --stage        the install tree root (the tree packed as crug-<version>/)
#   --tarball      the release tarball; its SHA256 names the document namespace
#   --out          the .spdx.json file to write
#   --gcc-version  the pinned GCC baseline version (default: 16.2.0)
#
# Exit status: 0 written and re-read, 1 a problem.

import argparse
import hashlib
import json
import os
import re
import sys
from datetime import datetime, timezone

CHUNK = 1024 * 1024


def fail(message):
    print("make-sbom: " + message, file=sys.stderr)
    sys.exit(1)


def hash_file(path, sha1, sha256):
    while True:
        chunk = path.read(CHUNK)
        if not chunk:
            break
        sha1.update(chunk)
        sha256.update(chunk)
    return sha1.hexdigest(), sha256.hexdigest()


def file_id(rel):
    cleaned = re.sub(r"[^A-Za-z0-9.-]+", "-", rel).strip("-")
    return "SPDXRef-File-" + (cleaned or "root")


def main():
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--stage", required=True)
    parser.add_argument("--tarball", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--gcc-version", default="16.2.0")
    args = parser.parse_args()

    version = args.version
    stage = os.path.abspath(args.stage)
    tarball = os.path.abspath(args.tarball)
    if not os.path.isdir(stage):
        fail("no install tree at " + stage)
    if not os.path.isfile(tarball):
        fail("no tarball at " + tarball)

    tarball_sha = hashlib.sha256()
    with open(tarball, "rb") as handle:
        for chunk in iter(lambda: handle.read(CHUNK), b""):
            tarball_sha.update(chunk)
    tarball_sha = tarball_sha.hexdigest()

    files = []
    used_ids = {}
    sha1_values = []
    for root, dirs, names in os.walk(stage):
        dirs.sort()
        for name in sorted(names):
            full = os.path.join(root, name)
            if os.path.islink(full):
                continue
            rel = os.path.relpath(full, stage).replace(os.sep, "/")
            spdx_id = file_id(rel)
            if spdx_id in used_ids:
                used_ids[spdx_id] += 1
                spdx_id = "{}x{}".format(spdx_id, used_ids[spdx_id])
            else:
                used_ids[spdx_id] = 1
            try:
                with open(full, "rb") as handle:
                    sha1, sha256 = hash_file(handle, hashlib.sha1(), hashlib.sha256())
            except OSError as error:
                fail("cannot read {}: {}".format(full, error))
            sha1_values.append(sha1)
            files.append((rel, spdx_id, sha1, sha256))

    if not files:
        fail("the install tree " + stage + " contains no regular files")
    files.sort(key=lambda entry: entry[0])
    verification = hashlib.sha1(
        "".join(sorted(sha1_values)).encode("ascii")
    ).hexdigest()

    def spdx_file(entry):
        rel, spdx_id, sha1, sha256 = entry
        return {
            "fileName": "./" + rel,
            "SPDXID": spdx_id,
            "checksums": [
                {"algorithm": "SHA1", "checksumValue": sha1},
                {"algorithm": "SHA256", "checksumValue": sha256},
            ],
            "licenseConcluded": "NOASSERTION",
            "licenseInfoInFiles": ["NOASSERTION"],
            "copyrightText": "NOASSERTION",
        }

    created = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "crug-" + version,
        "documentNamespace": "https://crug.dev/spdx/crug-{}-{}".format(version, tarball_sha),
        "creationInfo": {
            "created": created,
            "creators": ["Tool: crug-make-sbom"],
        },
        "documentDescribes": ["SPDXRef-Package-crug"],
        "packages": [
            {
                "SPDXID": "SPDXRef-Package-crug",
                "name": "crug",
                "versionInfo": version,
                "downloadLocation": "NOASSERTION",
                "filesAnalyzed": True,
                "packageVerificationCode": {
                    "packageVerificationCodeValue": verification,
                },
                "checksums": [
                    {"algorithm": "SHA256", "checksumValue": tarball_sha},
                ],
                "licenseConcluded": "GPL-3.0-or-later",
                "licenseDeclared": "GPL-3.0-or-later",
                "copyrightText": "NOASSERTION",
                "supplier": "NOASSERTION",
                "hasFiles": [entry[1] for entry in files],
            },
            {
                "SPDXID": "SPDXRef-Package-gcc",
                "name": "gcc-" + args.gcc_version,
                "versionInfo": args.gcc_version,
                "downloadLocation": "https://gcc.gnu.org/",
                "filesAnalyzed": False,
                "licenseConcluded": "GPL-3.0-or-later WITH GCC-exception-3.1",
                "licenseDeclared": "GPL-3.0-or-later WITH GCC-exception-3.1",
                "copyrightText": "Copyright (C) 1988-2026 Free Software Foundation, Inc.",
                "supplier": "NOASSERTION",
            },
            {
                "SPDXID": "SPDXRef-Package-libcrug",
                "name": "libcrug",
                "versionInfo": version,
                "downloadLocation": "NOASSERTION",
                "filesAnalyzed": False,
                "licenseConcluded": "GPL-3.0-or-later",
                "licenseDeclared": "GPL-3.0-or-later",
                "copyrightText": "Copyright (C) 2026 the Crug authors.",
                "supplier": "NOASSERTION",
            },
        ],
        "files": [spdx_file(entry) for entry in files],
        "relationships": [
            {
                "spdxElementId": "SPDXRef-DOCUMENT",
                "relationshipType": "DESCRIBES",
                "relatedSpdxElement": "SPDXRef-Package-crug",
            },
            {
                "spdxElementId": "SPDXRef-Package-crug",
                "relationshipType": "CONTAINS",
                "relatedSpdxElement": "SPDXRef-Package-gcc",
            },
            {
                "spdxElementId": "SPDXRef-Package-crug",
                "relationshipType": "CONTAINS",
                "relatedSpdxElement": "SPDXRef-Package-libcrug",
            },
        ],
    }

    out = os.path.abspath(args.out)
    directory = os.path.dirname(out) or "."
    os.makedirs(directory, exist_ok=True)
    temporary = out + ".tmp"
    try:
        with open(temporary, "w", encoding="utf-8") as handle:
            json.dump(document, handle, indent=2, ensure_ascii=False)
            handle.write("\n")
        os.replace(temporary, out)
    except OSError as error:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        fail("cannot write {}: {}".format(out, error))

    # Re-read and assert the shape check-release.sh and the Phase 24 gate rely on.
    try:
        with open(out, encoding="utf-8") as handle:
            written = json.load(handle)
    except (OSError, ValueError) as error:
        fail("the written SBOM does not parse: {}".format(error))

    problems = []
    if written.get("spdxVersion") != "SPDX-2.3":
        problems.append("spdxVersion is not SPDX-2.3")
    if written.get("dataLicense") != "CC0-1.0":
        problems.append("dataLicense is not CC0-1.0")
    if written.get("SPDXID") != "SPDXRef-DOCUMENT":
        problems.append("the document SPDXID is wrong")
    if written.get("name") != "crug-" + version:
        problems.append("the document name is not crug-" + version)
    if tarball_sha not in written.get("documentNamespace", ""):
        problems.append("the document namespace does not embed the tarball SHA256")
    packages = written.get("packages") or []
    named = {package.get("name"): package for package in packages}
    if "crug" not in named or named["crug"].get("versionInfo") != version:
        problems.append("the crug package does not carry version " + version)
    if "gcc-" + args.gcc_version not in named:
        problems.append("the gcc-" + args.gcc_version + " package is missing")
    if "libcrug" not in named:
        problems.append("the libcrug package is missing")
    if named.get("crug", {}).get("filesAnalyzed") is not True:
        problems.append("the crug package does not analyse files")
    if len(named.get("crug", {}).get("hasFiles") or []) != len(files):
        problems.append("the crug package does not list every file")
    code = (named.get("crug", {}).get("packageVerificationCode") or {}).get(
        "packageVerificationCodeValue", "")
    if not re.fullmatch(r"[0-9a-f]{40}", code):
        problems.append("the package verification code is not a SHA1")
    kinds = {
        (rel.get("spdxElementId"), rel.get("relationshipType"), rel.get("relatedSpdxElement"))
        for rel in written.get("relationships") or []
    }
    if ("SPDXRef-DOCUMENT", "DESCRIBES", "SPDXRef-Package-crug") not in kinds:
        problems.append("the document does not DESCRIBE the crug package")
    if ("SPDXRef-Package-crug", "CONTAINS", "SPDXRef-Package-gcc") not in kinds:
        problems.append("the crug package does not CONTAIN gcc")
    if ("SPDXRef-Package-crug", "CONTAINS", "SPDXRef-Package-libcrug") not in kinds:
        problems.append("the crug package does not CONTAIN libcrug")
    if problems:
        for problem in problems:
            print("make-sbom: " + problem, file=sys.stderr)
        sys.exit(1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
