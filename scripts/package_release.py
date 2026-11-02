# SPDX-FileCopyrightText: 2026 TriM
# SPDX-License-Identifier: AGPL-3.0-only

"""Create offline corresponding-source and optional Windows release archives."""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parent.parent


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--juce-dir", type=Path, default=os.environ.get("JUCE_DIR"))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--validated-release-dir", type=Path, help="Previously validated flat delivery directory")
    parser.add_argument("--include-binaries", action="store_true")
    parser.add_argument("--ref", default="HEAD", help="Tagged commit to package; defaults to HEAD")
    args = parser.parse_args()
    if args.juce_dir is None:
        parser.error("--juce-dir or the JUCE_DIR environment variable is required")
    juce = args.juce_dir.resolve(strict=True)
    if git("status", "--porcelain", "--untracked-files=no").strip():
        parser.error("Commit all tracked changes before packaging")
    commit = git("rev-parse", args.ref + "^{commit}").decode().strip()
    cmake = git("show", commit + ":CMakeLists.txt").decode("utf-8")
    version = re.search(r"project\(SpectralResonatorRE VERSION ([0-9.]+)\)", cmake).group(1)
    expected_tag = "v" + version
    tags = git("tag", "--points-at", commit).decode().splitlines()
    if expected_tag not in tags:
        parser.error(f"Create {expected_tag} at the tested commit before packaging")
    core = (juce / "modules/juce_core/juce_core.h").read_text(encoding="utf-8")
    if not re.search(r"version:\s+8\.0\.4\s", core):
        parser.error("This release requires the actual JUCE 8.0.4 source tree")
    required = ("CMakeLists.txt", "LICENSE.md", "modules", "extras/Build/CMake")
    if any(not (juce / item).exists() for item in required):
        parser.error("JUCE source tree is incomplete")
    out = (args.output_dir or ROOT / "build/releases" / version).resolve()
    out.mkdir(parents=True, exist_ok=True)
    stem = f"SpectralResonatorRE-{version}"
    source_zip = out / (stem + "-source.zip")
    source_prefix = stem + "-source/"
    inventory = {"project": {}, "JUCE": {}}
    archive = zipfile.ZipFile(io.BytesIO(git("archive", "--format=zip", commit)))
    with zipfile.ZipFile(source_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as target:
        for item in archive.infolist():
            if item.is_dir():
                continue
            content = archive.read(item)
            if not item.filename.startswith("licenses/") and item.filename not in ("LICENSE", "README.md"):
                text = content.decode("utf-8")
                if re.search(r"(?i)(?<![a-z0-9])[a-z]:[\\/]|/(?:Users|home)/", text):
                    parser.error("Project sources must not contain machine-specific absolute paths: " + item.filename)
            target.writestr(source_prefix + item.filename, content)
            inventory["project"][item.filename] = sha256(content)
        for file in sorted(juce.rglob("*")):
            relative = file.relative_to(juce)
            if ".git" in relative.parts or not file.is_file() or file.suffix.lower() in {
                ".exe", ".dll", ".pdb", ".obj", ".lib", ".a", ".so", ".dylib"
            }:
                continue
            if not file.resolve().is_relative_to(juce):
                parser.error("JUCE source contains a link outside its root")
            content = file.read_bytes()
            target.writestr(source_prefix + "third_party/JUCE/" + relative.as_posix(), content)
            inventory["JUCE"][relative.as_posix()] = sha256(content)
        manifest = {
            "version": version,
            "git_commit": commit,
            "git_tag": expected_tag,
            "project_license": "AGPL-3.0-only",
            "juce_version": "8.0.4",
            "build_instructions": "docs/RELEASING.md",
            "sha256": inventory,
        }
        target.writestr(source_prefix + "SOURCE_MANIFEST.json", json.dumps(manifest, indent=2) + "\n")
    artifacts = [source_zip]
    if args.include_binaries:
        build = args.build_dir.resolve()
        release = build / "SpectralResonatorRE_artefacts/Release"
        bundle = release / "VST3/Spectral Resonator RE.vst3"
        standalone = release / "Standalone/Spectral Resonator RE.exe"
        if args.validated_release_dir:
            bundle = args.validated_release_dir.resolve() / "Spectral Resonator RE.vst3"
            standalone = args.validated_release_dir.resolve() / "Spectral Resonator RE.exe"
        module_info = bundle / "Contents/Resources/moduleinfo.json"
        if not standalone.is_file() or not module_info.is_file():
            parser.error("Build and validate Release VST3/Standalone before packaging binaries")
        info = module_info.read_text(encoding="utf-8")
        module_version = re.search(r'"Version"\s*:\s*"([^"]+)"', info)
        if module_version is None or module_version.group(1) != version:
            parser.error("VST3 module version does not match the source version")
        cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
        configured_juce = re.search(r"^JUCE_DIR:PATH=(.*)$", cache, re.MULTILINE)
        if configured_juce is None or Path(configured_juce.group(1).strip()).resolve() != juce:
            parser.error("Package the exact JUCE source directory used by the build")
        binary_zip = out / (stem + "-windows.zip")
        binary_inventory = {}
        with zipfile.ZipFile(binary_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as target:
            for file in [*sorted(bundle.rglob("*")), standalone]:
                if not file.is_file():
                    continue
                path = file.relative_to(bundle.parent).as_posix() if file.is_relative_to(bundle) else file.name
                content = file.read_bytes()
                target.writestr(path, content)
                binary_inventory[path] = sha256(content)
            for item in archive.infolist():
                if item.filename in ("LICENSE", "THIRD_PARTY_NOTICES.md") or (
                    item.filename.startswith("licenses/") and not item.is_dir()
                ):
                    target.writestr(item.filename, archive.read(item))
            target.writestr("README.txt", (
                f"Spectral Resonator RE {version}\nCopyright (C) 2026 TriM\nAGPL-3.0-only\n\n"
                "Provided WITHOUT WARRANTY to the extent permitted by law; see LICENSE sections 15 and 16.\n"
                "Commercial music production is allowed. Software redistribution must comply with AGPL.\n\n"
                "Copy the entire .vst3 bundle into your host's VST3 scan directory.\n"
                "The standard system VST3 directory may require administrator permission.\n"
                "The standalone EXE can be run separately. Full license notices are included in this ZIP.\n\n"
                f"Complete corresponding source: {source_zip.name}\n"
                f"Publish that source ZIP alongside this binary ZIP at the same download location.\n"
                f"Source repository: https://github.com/3kwMusic/SpectralResonatorRE/tree/{expected_tag}\n"
                f"Source commit: {commit}\n"
                "Offline build instructions are in the source package's docs/RELEASING.md.\n"
            ))
            target.writestr("BINARY_MANIFEST.json", json.dumps({
                "version": version, "git_commit": commit, "corresponding_source": source_zip.name,
                "sha256": binary_inventory,
            }, indent=2) + "\n")
        artifacts.append(binary_zip)
    sums = "".join(f"{sha256(file.read_bytes())}  {file.name}\n" for file in artifacts)
    (out / "SHA256SUMS.txt").write_text(sums, encoding="utf-8")
    print(sums, end="")
    print(f"Source package: {len(inventory['project'])} project files; {len(inventory['JUCE'])} JUCE files")
    print("Release archives created.")


if __name__ == "__main__":
    main()
