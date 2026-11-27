// Debug code of the custom gun models research (branch gun-model-research,
// docs/research/gun-models.md). Not built; kept only so the code can be recovered.
//
// Removed in: the cleanup commit that added this file
// ("Custom gun models phase 2: TEMP_GUNMODEL logs to the debug bank").
//
// What for (all only while pd_file_override is on):
// - a log line for every packed file the game reads ("[gunmodel] load file 0x... (packed)"):
//   how matches were found to use each gun's LOW-detail file, and the XBLA meshes
//   0x9xx linked from the N64 files;
// - a log line for every texture the texture manager loads ("[gunmodel] texture 0x...
//   loaded"): which texture slots a match never uses (slot takeover, R4);
// - a log line for every XBLA mesh registered (sub_8226A598): its vertex count and size
//   (size limits, R6: up to 65448 vertices loaded fine).
// Scenario tools/debug-bank/gun-models-scenarios/testgun-switch (screenshots every ~100 ms
// through weapon switches): found that the "Unarmed" over a Falcon 2 the user saw came from
// the testgun scenario equipping a Falcon 2 the player had not been given (the HUD name is
// the inventory slot of the held weapon, slot 0 when it is not in the inventory).
// Where it lived: src/pd_file_override.h (Serve, ServeTexture, LogMeshRegister),
// src/perfect_dark_hooks.cpp (PdHookLogMeshRegister) and perfect_dark_config.toml (its
// hook, before "Texture slots").

// ============================================================================
// src/pd_file_override.h, Serve(): when the id has no loose file, before `return false;`
// ============================================================================
    // TEMP_GUNMODEL: which packed files the game reads (e.g. to find a gun's third-person model).
    REXLOG_INFO("[gunmodel] load file 0x{:04X} (packed) to 0x{:08X}", id, dest);

// ============================================================================
// src/pd_file_override.h, ServeTexture(): instead of `if (it == s.textures.end()) return;`
// ============================================================================
  if (it == s.textures.end()) {
    // TEMP_GUNMODEL: every texture the game loads (which slots a match never uses).
    REXLOG_INFO("[gunmodel] texture 0x{:04X} loaded ({} bytes)", number, inflated_size);
    return;
  }

// ============================================================================
// src/pd_file_override.h (before PatchTextureSlots)
// ============================================================================

// TEMP_GUNMODEL: an XBLA mesh being registered (sub_8226A598's entry: r4 = the mesh
// file's data, r5 = its file id): log its id and size, to see which meshes load and
// how big they are (size limits, phase 2 step 6).
inline void LogMeshRegister(uint8_t* base, uint32_t data, uint32_t file_id) {
  if (!Get().enabled || !data) return;
  const uint32_t verts = LoadBE32(base, data), subs = LoadBE32(base, data + 12), bones = LoadBE32(base, data + 20);
  const uint32_t index_offset = LoadBE32(base, data + 8);
  REXLOG_INFO("[gunmodel] mesh 0x{:04X} registered: {} vertices, {} sub-meshes, {} bones, vertex data {} bytes",
              file_id & 0xFFF, verts, subs, bones, index_offset - LoadBE32(base, data + 4));
}

// ============================================================================
// src/perfect_dark_hooks.cpp (before PdHookTextureSlots)
// ============================================================================

// TEMP_GUNMODEL: an XBLA mesh being registered (sub_8226A598: r4 data, r5 file id).
void PdHookLogMeshRegister(PPCRegister& r4, PPCRegister& r5) {
  pd::fileoverride::LogMeshRegister(HookBase(), r4.u32, r5.u32);
}

// ============================================================================
// perfect_dark_config.toml (before "Texture slots")
// ============================================================================

// # TEMP_GUNMODEL: log each XBLA mesh as it is registered (sub_8226A598: r4 data, r5 id).
// [[midasm_hook]]
// address = 0x8226A598
// name = "PdHookLogMeshRegister"
// registers = ["r4", "r5"]
//
