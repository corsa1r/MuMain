#pragma once

// Minimal store-only (uncompressed) ZIP writer used to produce .bmap map packages
// from the in-game editor. .bmap payloads are already encrypted/JPEG-encoded
// binaries that compress poorly, so the simplest, dependency-free implementation
// is fine — we just stitch local file headers, file data, central directory and
// EOCD record. No DEFLATE, no encryption, no Zip64.
//
// Server-side reader (MUnique.OpenMU.CustomMaps.MapPackageReader) handles
// stored-mode entries identically to deflated ones.

#include <cstdint>
#include <string>
#include <vector>

namespace MuEditor::CustomMap
{
    // Result of MapPackageWriter::Save.
    struct ExportResult
    {
        bool   success{false};
        std::wstring outputPath;
        std::string error;     // populated when !success
    };

    // Builds a .bmap (ZIP) file for the editor-authored map at the given mapId.
    // Reads every required asset from <slot>/ on disk, computes SHA-256 of each
    // and writes them into a ZIP with a manifest.json describing the package.
    // Returns the export path or an error. outputPath is where the user wants
    // the file written.
    ExportResult ExportMapPackage(int mapId,
                                  const std::wstring& mapDisplayName,
                                  const std::wstring& outputPath,
                                  // Optional warp/spawn seeds — present on first export,
                                  // ignored by the server on re-import per the conflict policy.
                                  const std::string& warpName = "",
                                  int   levelRequirement = 0,
                                  int   costs = 0,
                                  uint8_t spawnX = 0,
                                  uint8_t spawnY = 0);
}
