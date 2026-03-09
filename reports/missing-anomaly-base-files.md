# Missing Base Anomaly Files

> **Purpose**: Files that must be copied from a base **S.T.A.L.K.E.R. Anomaly 1.5.x** installation
> and committed to this repository so the coop mod can override them with coop-specific behaviour.
>
> All files listed here belong under `gamedata/` in this repo and are automatically packed
> into `00_modded_exes_gamedata.db0` by the existing CI workflow (the `pack_gamedata` job in
> `.github/workflows/msbuild.yml` copies `./gamedata/*` recursively before running xrCompress).
> **No build-system changes are required** — just place the files at the paths shown below.

---

## Priority 1 — Critical (fixes active crash)

### `gamedata/scripts/ui_main_menu.script`

**Base path**: `<anomaly_install>/gamedata/scripts/ui_main_menu.script`

**Why needed**:  
This is the Lua class (`class "main_menu" (CUIScriptWnd)`) that implements the main menu UI.
Line 281 of this script calls `self:OnButton_multiplayer_clicked()`, which is `nil` in
SP-only Anomaly builds.  The method is absent because Anomaly removed its multiplayer
support years ago.

In the coop engine two code paths reach this call:

| Path | Trigger |
|------|---------|
| Direct button click | User clicks the "Multiplayer" button in the main menu → `AddCallback` → `OnButton_multiplayer_clicked` |
| Error recovery | `Level_start.cpp:277/297/318` → `CMainMenu::SwitchToMultiplayerMenu()` → `m_startDialog->Dispatch(2,1)` → Lua `Dispatch` override → line 281 |

**What to change once added**:  
Define `OnButton_multiplayer_clicked` in this file (or keep the injection in
`axr_main.main_menu_on_init` as a safety net) and replace the placeholder
`main_menu off` body with a proper coop lobby dialog.

---

## Priority 2 — Important (needed for proper coop multiplayer UI)

### `gamedata/configs/ui/ui_mm_main.xml`

**Base path**: `<anomaly_install>/gamedata/configs/ui/ui_mm_main.xml`

**Why needed**:  
XML layout for the main menu dialog.  Contains all button definitions including
`btn_multiplayer` whose name is used by `AddCallback` in `ui_main_menu.script`.
Understanding (and modifying) this file is required to:

* Rename / repurpose the "Multiplayer" button for coop (e.g., "Play Co-op")
* Add new coop-specific buttons (e.g., "Host", "Connect")
* Adjust any UI elements that reference removed SP-only features

---

## Priority 3 — Context (helpful for diagnosis and future work)

### `gamedata/scripts/class_registrator.script`

**Base path**: `<anomaly_install>/gamedata/scripts/class_registrator.script`

**Why needed**:  
This is loaded at engine startup (before any game session) via
`CScriptEngine::register_script_classes()`.  It maps the C++ class tag `MAIN_MNU` to the
`main_menu` Lua class in `ui_main_menu.script`.  Without seeing this file we cannot verify
that the mapping has not changed between Anomaly versions.

### `gamedata/scripts/xrs_dyn_music.script`

**Base path**: `<anomaly_install>/gamedata/scripts/xrs_dyn_music.script`

**Why needed**:  
Called from `axr_main.main_menu_on_init` (our file) as `xrs_dyn_music.main_menu_on(menu)`.
If `xrs_dyn_music.script` is absent or has changed, `main_menu_on_init` will throw before
reaching the `OnButton_multiplayer_clicked` injection.

---

## How the override mechanism works

Anomaly loads file sources in priority order (later = higher priority):

1. Base game archives (`gamedata.db0`, `gamedata.db1`, …) — **lowest**
2. Mod archives loaded after the base game — **higher**
3. Loose files in the override folder (if configured) — **highest**

`00_modded_exes_gamedata.db0` (the archive this repo produces) is loaded **after** the base
game archives because Anomaly's mod loader mounts mod archives with a higher priority.
A file placed in `gamedata/scripts/` in this repo will therefore **override** the
corresponding file from the base Anomaly installation.

### Build pipeline (no changes needed)

```
.github/workflows/msbuild.yml
  └── pack_gamedata job
        new-item -path "./compressor/gamedata"   (create staging dir)
        copy-item "./gamedata/*" → "./compressor/gamedata"   (copy all files)
        cd ./compressor && "! compress_gamedata.cmd"
          └── xrCompress.exe gamedata -ltx build_patch.ltx -pack …
                build_patch.ltx: entry_point = $fs_root$\gamedata\
                                 include_folders: .\ = true
        rename gamedata.db0 → 00_modded_exes_gamedata.db0
```

A file at `gamedata/scripts/ui_main_menu.script` in this repo ends up at
`scripts/ui_main_menu.script` inside the `.db0` — which the VFS resolves as
`$game_data$\scripts\ui_main_menu.script`, correctly overriding the base game version.

---

## Summary table

| Repo path | Source in Anomaly install | Priority | Status |
|-----------|--------------------------|----------|--------|
| `gamedata/scripts/ui_main_menu.script` | `gamedata/scripts/ui_main_menu.script` | 🔴 Critical | **Missing** |
| `gamedata/configs/ui/ui_mm_main.xml` | `gamedata/configs/ui/ui_mm_main.xml` | 🟠 Important | **Missing** |
| `gamedata/scripts/class_registrator.script` | `gamedata/scripts/class_registrator.script` | 🟡 Context | **Missing** |
| `gamedata/scripts/xrs_dyn_music.script` | `gamedata/scripts/xrs_dyn_music.script` | 🟡 Context | **Missing** |
