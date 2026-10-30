#!/usr/bin/env python3
# make-sbom.py - write the SPDX 2.3 SBOM for a Crug release.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# The document describes the install tree package.sh staged. The crug package
# is analysed file by file (SHA1/SHA256 per regular file plus the SPDX package
# verification code); symbolic links are listed as files without a checksum,
# which SPDX 2.3 excludes from the verification code; the GCC baseline and
# libcrug are named as contained packages, because they ship as part of the
# same install tree.
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
    for root, dirs, names in os.walk(stage, followlinks=False):
        dirs.sort()
        # os.walk lists a symlink to a directory in dirs and, because
        # followlinks is false, does not descend into it.  The link is still
        # an entry of the shipped tree, so record it and stop treating it as
        # a directory to visit.
        entries = list(names)
        for name in dirs:
            if os.path.islink(os.path.join(root, name)):
                entries.append(name)
        dirs[:] = [name for name in dirs if not os.path.islink(os.path.join(root, name))]
        for name in sorted(entries):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, stage).replace(os.sep, "/")
            spdx_id = file_id(rel)
            if spdx_id in used_ids:
                used_ids[spdx_id] += 1
                spdx_id = "{}x{}".format(spdx_id, used_ids[spdx_id])
            else:
                used_ids[spdx_id] = 1
            if os.path.islink(full):
                try:
                    target = os.readlink(full)
                except OSError as error:
                    fail("cannot read the link {}: {}".format(full, error))
                files.append({"rel": rel, "id": spdx_id, "link": target})
                continue
            try:
                with open(full, "rb") as handle:
                    sha1, sha256 = hash_file(handle, hashlib.sha1(), hashlib.sha256())
            except OSError as error:
                fail("cannot read {}: {}".format(full, error))
            sha1_values.append(sha1)
            files.append({
                "rel": rel,
                "id": spdx_id,
                "link": None,
                "sha1": sha1,
                "sha256": sha256,
            })

    if not files:
        fail("the install tree " + stage + " contains no files")
    files.sort(key=lambda entry: entry["rel"])
    # SPDX 2.3: the package verification code is computed over the files that
    # carry a SHA1 checksum; a symbolic link has no content checksum and is
    # therefore excluded (its target is hashed as a file of its own).
    verification = hashlib.sha1(
        "".join(sorted(sha1_values)).encode("ascii")
    ).hexdigest()

    def spdx_file(entry):
        file_entry = {
            "fileName": "./" + entry["rel"],
            "SPDXID": entry["id"],
        }
        if entry["link"] is not None:
            file_entry["fileTypes"] = ["OTHER"]
            file_entry["comment"] = "symbolic link to " + entry["link"]
        else:
            file_entry["checksums"] = [
                {"algorithm": "SHA1", "checksumValue": entry["sha1"]},
                {"algorithm": "SHA256", "checksumValue": entry["sha256"]},
            ]
        file_entry.update({
            "licenseConcluded": "NOASSERTION",
            "licenseInfoInFiles": ["NOASSERTION"],
            "copyrightText": "NOASSERTION",
        })
        return file_entry

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
                "hasFiles": [entry["id"] for entry in files],
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
    symlinks = [
        entry for entry in written.get("files") or []
        if entry.get("fileTypes") == ["OTHER"]
    ]
    expected_links = sum(1 for entry in files if entry["link"] is not None)
    if len(symlinks) != expected_links:
        problems.append("the crug package does not list every symbolic link")
    for entry in symlinks:
        if not (entry.get("comment") or "").startswith("symbolic link to "):
            problems.append(
                entry.get("fileName", "?") + " does not say what it links to"
            )
        if entry.get("checksums"):
            problems.append(
                entry.get("fileName", "?") + " carries checksums, but a link has none"
            )
    hashed = []
    for entry in written.get("files") or []:
        if entry.get("fileTypes") == ["OTHER"]:
            continue
        sha1 = next(
            (checksum.get("checksumValue")
             for checksum in entry.get("checksums") or []
             if checksum.get("algorithm") == "SHA1"),
            None,
        )
        if not re.fullmatch(r"[0-9a-f]{40}", sha1 or ""):
            problems.append(entry.get("fileName", "?") + " has no SHA1 checksum")
            continue
        hashed.append(sha1)
    if len(hashed) != len(sha1_values):
        problems.append("the crug package does not hash every regular file")
    if hashlib.sha1("".join(sorted(hashed)).encode("ascii")).hexdigest() != code:
        problems.append("the package verification code is not the SHA1 of the file checksums")
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
