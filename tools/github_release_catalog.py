#!/usr/bin/env python3
"""Build a universal PS5 Payload Manager X catalog including GitHub pre-releases.

Input is the upstream X payload mirror. This tool never installs or executes
payloads, never changes autoload, and excludes Payload Manager self-updates.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import re
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from collections import defaultdict
from pathlib import Path

SOURCE = "https://raw.githubusercontent.com/bsk193/ps5-payloads-mirror-x/main/payloads_v2.json"
EXTS = {".elf", ".bin", ".exfat", ".ffpkg", ".ffpfs", ".ffpfsc", ".zip"}
AGENT = "FeromonDE-PS5-Payload-Manager-X-Release-Catalog/1.0"


def github_repo(url):
    try:
        parsed = urllib.parse.urlparse(url)
        if parsed.scheme != "https" or parsed.netloc.lower() not in ("github.com", "www.github.com"):
            return None
        parts = [urllib.parse.unquote(p) for p in parsed.path.split("/") if p]
        if len(parts) < 2 or not re.fullmatch(r"[A-Za-z0-9-]{1,39}", parts[0]):
            return None
        name = re.sub(r"\.git$", "", parts[1], flags=re.I)
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,100}", name):
            return None
        return parts[0] + "/" + name
    except (TypeError, ValueError):
        return None


def stem(filename):
    name = Path(filename or "").stem.lower()
    name = re.sub(r"[_\-.]v?\d+[a-z0-9._+-]*$", "", name)
    return re.sub(r"[^a-z0-9]", "", name)


def extension(filename):
    return Path(filename or "").suffix.lower()


def repo_of(item):
    for field in ("source_direct", "source", "details_url", "repository", "repo_url"):
        val = item.get(field)
        if isinstance(val, str):
            repo = github_repo(val)
            if repo:
                return repo
    return None


def template_for(items):
    # Prefer the newest usable direct ELF asset; older catalog entries can have
    # unrelated filenames or ZIP links despite a newer ELF being available.
    def score(e):
        direct = e.get("source_direct", "")
        dn = Path(urllib.parse.urlparse(direct).path).name
        return (int(bool(direct) and extension(dn) == extension(e.get("filename"))),
                e.get("last_update", ""), e.get("version", ""))
    return max(items, key=score)


def match_asset(asset, template, allow_single):
    name = asset.get("name", "")
    if extension(name) not in EXTS or extension(name) != extension(template.get("filename")):
        return -1
    direct = template.get("source_direct") or ""
    direct_name = Path(urllib.parse.urlparse(direct).path).name.lower()
    if direct_name and extension(direct_name) == extension(name) and name.lower() == direct_name:
        return 100
    original = stem(template.get("filename"))
    got = stem(name)
    if original and got and original == got:
        return 80
    if min(len(original), len(got)) >= 6 and (original in got or got in original):
        return 40
    return 10 if allow_single else -1


def filename_for(item, tag):
    base = re.sub(r"[^A-Za-z0-9_.-]", "_", item.get("name") or item.get("id") or "payload")
    ver = re.sub(r"[^A-Za-z0-9_.-]", "_", tag)
    return base[:100].strip("._-") + "_" + ver[:80].strip("._-") + extension(item["filename"])


class GitHub:
    def __init__(self, token=None):
        self.token = token

    def request(self, url):
        headers = {"Accept": "application/vnd.github+json", "User-Agent": AGENT}
        if self.token and url.startswith("https://api.github.com/"):
            headers["Authorization"] = "Bearer " + self.token
        return urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=45)

    def releases(self, repo):
        url = "https://api.github.com/repos/{}/releases?per_page=30".format(repo)
        with self.request(url) as response:
            releases = json.load(response)
        if not isinstance(releases, list):
            raise ValueError("GitHub returned no release list")
        return [r for r in releases if isinstance(r, dict) and not r.get("draft")]

    def sha256(self, asset, limit):
        value = asset.get("digest")
        if isinstance(value, str) and re.fullmatch(r"sha256:[a-fA-F0-9]{64}", value):
            return value[7:].lower()
        size = asset.get("size")
        url = asset.get("browser_download_url", "")
        if not isinstance(size, int) or size < 1 or size > limit:
            return None
        if not isinstance(url, str) or not url.startswith("https://github.com/"):
            return None
        digest = hashlib.sha256()
        read = 0
        with self.request(url) as response:
            while True:
                block = response.read(1024 * 1024)
                if not block:
                    break
                read += len(block)
                if read > limit:
                    return None
                digest.update(block)
        return digest.hexdigest() if read == size else None


def generate(base, github, max_versions=8, size_limit=512 * 1024 * 1024):
    groups = defaultdict(list)
    for item in base.get("payloads", []):
        if not isinstance(item, dict) or item.get("id", "").lower() in ("pldmgr", "pldmgrx"):
            continue
        repo = repo_of(item)
        if repo and item.get("id") and extension(item.get("filename")) in EXTS:
            groups[(repo.lower(), item["id"])].append(item)

    by_repo = defaultdict(dict)
    for (repo, identity), versions in groups.items():
        by_repo[repo][identity] = template_for(versions)
    known = {(str(p.get("id", "")).lower(), str(p.get("version", "")).lower())
             for p in base["payloads"] if isinstance(p, dict)}
    discovered, failed = [], []
    successful = 0
    for repo, identities in sorted(by_repo.items()):
        try:
            releases = github.releases(repo)
            successful += 1
        except (urllib.error.URLError, OSError, ValueError) as exc:
            failed.append(repo + ": " + str(exc))
            continue
        counters = defaultdict(int)
        for release in releases:
            tag = release.get("tag_name")
            if not isinstance(tag, str) or not tag or len(tag) > 80:
                continue
            assets = [a for a in release.get("assets", [])
                      if isinstance(a, dict) and a.get("state") == "uploaded"
                      and isinstance(a.get("name"), str)
                      and isinstance(a.get("size"), int) and 0 < a["size"] <= size_limit]
            for identity, template in identities.items():
                if counters[identity] >= max_versions or (identity.lower(), tag.lower()) in known:
                    continue
                same_type = [a for a in assets if extension(a["name"]) == extension(template["filename"])]
                allow_single = len(identities) == 1 and len(same_type) == 1
                ranked = []
                for asset in same_type:
                    rating = match_asset(asset, template, allow_single)
                    other = max((match_asset(asset, t, False) for k, t in identities.items()
                                 if k != identity), default=-1)
                    if rating >= 0 and rating > other:
                        ranked.append((rating, asset))
                if not ranked:
                    continue
                ranked.sort(key=lambda pair: (-pair[0], len(pair[1]["name"])))
                chosen = ranked[0][1]
                url = chosen.get("browser_download_url")
                if not isinstance(url, str) or not url.startswith("https://github.com/"):
                    continue
                digest = github.sha256(chosen, size_limit)
                if digest is None:
                    failed.append(repo + " " + tag + ": SHA-256 unavailable")
                    continue
                entry = copy.deepcopy(template)
                entry.update({
                    "filename": filename_for(template, tag),
                    "version": tag,
                    "url": url,
                    "source_direct": url,
                    "source": "https://github.com/" + repo + "/releases",
                    "channel": "experimental" if release.get("prerelease") else "stable",
                    "last_update": (release.get("published_at") or "")[:10],
                    "checksum": digest,
                })
                # The X manifest rejects unknown fields; only retain compatible keys.
                discovered.append(entry)
                known.add((identity.lower(), tag.lower()))
                counters[identity] += 1
    merged = copy.deepcopy(base)
    merged["name"] = "FeromonDE - GitHub Releases (Stable + Pre-release)"
    merged["payloads"] = base["payloads"] + discovered
    return merged, failed, successful, len(by_repo), len(discovered)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default=SOURCE)
    parser.add_argument("--output", default="payloads_prereleases.json")
    parser.add_argument("--max-versions", type=int, default=8)
    args = parser.parse_args()
    if not 1 <= args.max_versions <= 30:
        parser.error("--max-versions must be between 1 and 30")
    with urllib.request.urlopen(
        urllib.request.Request(args.input, headers={"User-Agent": AGENT}), timeout=45
    ) if args.input.startswith("https://") else open(args.input, encoding="utf-8") as f:
        base = json.load(f)
    if not isinstance(base, dict) or not isinstance(base.get("payloads"), list):
        raise SystemExit("Invalid source manifest")
    result, errors, ok, total, added = generate(
        base, GitHub(os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")),
        max_versions=args.max_versions
    )
    if total and ok == 0:
        raise SystemExit("All GitHub lookups failed; previous output was preserved")
    target = Path(args.output)
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=target.parent,
                                    prefix=".catalog-", delete=False) as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
        f.write("\n")
        tmp = f.name
    os.replace(tmp, target)
    print("Repos: {}, succeeded: {}, new versions: {}, total entries: {}".format(
        total, ok, added, len(result["payloads"])))
    for message in errors[:20]:
        print("WARN:", message, file=sys.stderr)


if __name__ == "__main__":
    main()
