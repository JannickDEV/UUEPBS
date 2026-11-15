# WARNING:
<p><b>Use UUEPBS at your own discretion! This is solely made for single player games, I AM NOT responsible for any damage or bans caused to users attempting to install UUEPBS on multiplayer/competitive games or any games with online elements for that matter.</b></p>

# About:
Universal Unreal Engine Player Body Sliders (UUEPBS) is a near universal player character body slider mod for Unreal Engine based games, leveraging the powerful UE4/5SS tool. UUEPBS gives players the freedom to customise their character's body proportions, such as hips, spine, shoulders, arms, legs etc. With capabilities such as saving and loading user-defined UUEPBS presets, as well as being able to change NPC's body proportions and save their UUEPBS presets.


https://github.com/user-attachments/assets/4e4372bc-8a64-4255-b8ae-a33645ce187e
### V2.8.0 - Changelogs
* **Before / After** switch in the character banner: **Before** shows every character's original body, **After** your sliders. Nothing is lost, and moving a slider or closing the window goes back to After
* Hold-to-compare hotkey: hold **F8** (`CompareKey` in `config.lua`, `""` turns it off) in game to see the original body, let go to get your sliders back. Console: `ubs compare [on|off]`
* **Next / previous preset hotkeys**: `NextPresetKey` and `PrevPresetKey` in `config.lua` (off by default) load the next or previous saved preset onto the character picked in the window, without opening it. Each character keeps its own place in the list. Console: `ubs next`, `ubs prev`
* **Share codes** in the Presets tab: **Copy current sliders** or **Copy picked preset** puts the preset on the clipboard as one line of text (`UUEPBS1.…`); **Paste code** reads one back, shows what it holds, and only changes anything when you press **Apply** or **Save as preset**. Codes are checksummed and size-limited, and values are clamped like preset files. Preset files are unchanged
* New **Character Scale** tab (between Body and Presets): **Height** under *Overall*, and *Proportions*: **Head size**, **Neck length**, **Shoulder width**, **Torso length**, **Arm length**, **Hand size**, **Leg length**, **Foot size** (0.5× to 2×)
* Height scales the whole pose from the mesh origin (the feet on most characters), so the character stays on the ground; every mesh of the character scales with it
* Proportions sit on top of the Body and Advanced sliders (they multiply, nothing is overwritten). Lengths stretch the limb bones only, so hands, feet and the head keep their size
* Saved per character like the other sliders (session, NPC memory) and in presets as an optional `"proportions"` section; older builds ignore it
* **Looks** tab: **Skin** (skin tone, sweat) and **Hair** (hue, saturation, lightness), per character like the sliders: saved with the session, in an NPC's memory and in presets (`"looks"`). The outfit's own materials are recoloured, nothing is replaced
* Character Scale > **Muscle**: **Overall muscle**, **Upper arm**, **Forearm**, **Waist**, **Neck**, **Thigh** and **Calf thickness**, **Shoulder bulk** and **Chest breadth**. They make limbs thicker without making them longer; hands, feet and the head keep their size and stay where they were. Muscle helper bones (biceps, quads, pecs...) grow along when the rig has them
* Length and thickness sliders now work out which way each bone runs from the live pose instead of assuming X, so they also fit rigs whose bones run along Y or Z
* **Bone-built Body sliders** ("universal morphs"): a mesh with anatomy bones but no morph target for them gets **Penis size / length / girth**, **Glans size**, **Testicle size** and **Vulva size** under Groin, found by bone name in any game. A real morph target for the same thing (`mGen_Balls`, `Penis_Length`...) always wins and hides the bone slider. Saved like the scale sliders (`"proportions"` in presets)
* **RaceMenu-style shape sliders**, built from the bones on any rig: **Breast width / projection / lift / cleavage**, **Nipple length**, **Waist width**, **Back width**, **Chest depth**, **Belly projection / lift**, **Hip width**, **Butt width / roundness / lift** and **Neck width**. Which way is up, forward and sideways is measured from the character's pose, so they work whatever way a rig's bones point; lifts and cleavage move the bones by a share of the character's height
* One source per slider, in this order: a clearly named morph target of the mesh (`Morph_HipsWide` replaces *Hip width*, `Morph_BreastEnl` replaces the bone *Breast size*), then the bone-built slider, then morph targets with cryptic names (still under **Other**). A bone row you already edited stays visible
* Body tab cleanup: sliders have character-creator names (*Thigh size*, *Breast size*), each category lists **Size** rows first and **Shape** rows (morphs) under them, every track lines up, and the bone / morph name lists moved out of the tooltips (Advanced lists them)

### V2.7.0 - Changelogs
* New window look: flat slate-navy theme with sharp corners, flush text tabs (**Body**, **Presets**, **Advanced**, **Status**) and 1px bordered groups
* Body tab (was the Simplified Panel): body-part categories on the left, the picked category's sliders on the right under a section bar with **Reset**
* Thin sliders whose value sits in a box next to them (click it to type), so the grab never covers the number
* Character banner at the top (monogram, name, engine name, live state) with **Change character**, which opens a grid of every loaded character
* **Rename** a character: your name for it is shown everywhere in the window and saved in `UUEPBS Presets\_characters\_names.txt`; the game and engine names stay in tooltips and on the Status tab
* Character portraits in the banner and the grid: your own PNG/JPG in `UUEPBS Presets\_portraits`, or taken automatically in the game (the game's own portrait when the profile names it, else a head shot). **Retake portrait** in the Rename window. `AutoPortraits = false` in `config.lua` turns the automatic part off
* Bone tree and morph list moved to the **Advanced** tab (was the Detailed Panel). Skins keep working; tabs no longer show icons
* Added Undo / Redo for slider edits (buttons in the character banner, **Ctrl+Z** / **Ctrl+Y** or **Ctrl+Shift+Z**), kept per character
* Added a "Changed only" filter to the Body tab
* Added preset details: the Presets tab shows which body parts, bones and morphs a preset changes before you load it
* Added a preset browser: thumbnails in a grid (or a compact list), search by name or `#tag`, and tag chips to filter by. Tags are added and removed next to the preset details
* Saving a preset takes a whole-body thumbnail of the character in the game (**Retake picture** takes a new one). Thumbnails and tags live in `UUEPBS Presets\_library`, so preset files stay exactly as before and share as before. `PresetThumbnails = false` in `config.lua` turns the pictures off
* Added symmetry tools above the sliders: **Symmetry lock** (replaces the per-bone and per-morph mirror checkboxes), **Mirror L to R** / **Mirror R to L** for the whole character, and **Randomize** with adjustable **Limits**. Each action is one undo step
* Readable character names: the Character list, the window header and the mesh labels show "Eve" or "Captain Vey" instead of engine names like `BP_Rokuv3_C_0`, `00_EVE_01_Body` or `SkeletalMeshComponent_2147...`. The name comes from the game itself when it has one (`DisplayName` / `CharacterName` on the character or its character data), else from the new `"Names"` table in the game profile, else from the engine name cleaned up. Saved sliders, remembered NPCs and presets are unaffected. `FriendlyNames = false` in `config.lua` brings the engine names back.

### V2.6.1 - Changelogs
* Added full support for player and NPC that works simultaneously
* Improved performance
* Changed scanning method to drop full world rescan for actors
* Renamed Body and Bones Tab to Simplified and Detailed Panel
* Added UI Customisability

## Tested and confirmed working games so far
* GON God of Nothing
* Wuchang Fallen Feathers
* Beast of Reincarnation
* Clair Obscur Expedition 33
* The Killing Antidote
* Stellar Blade
* VanySlash
* Mortal Shell 2
* Lies of P
* Dawn Break
* Lunar Eclipse
## Confirmed incompatible games
* Ninja Gaiden 2 Black - Hybrid propietary/UE game engine

## Installing
1. Install UE4SS for the game as usual, use experimental-latest releases.
2. Extract UUEPBS folder to `Game-Name\<Win64>\ue4ss\Mods`, where Game-Name is the name of the game located inside its root folder.

### Latest build
Every push to `main` and every pull request is built by GitHub Actions (**Actions** tab > **Build** > pick a run > **Artifacts**).
`UUEPBS-mod-<commit>` is the ready-to-drop mod: unzip it and copy the `UUEPBS` folder into `ue4ss\Mods` (it already has `native\UUEPBS.dll`).
`UUEPBS-dll-<commit>` is the DLL alone. Downloading artifacts needs a GitHub login; they are kept for 90 days.

## Using it

* **F6** opens and closes the window while the game or the window has focus. `MenuKey` in `config.lua` changes the key.
  The window is a separate top-level window, so it works with borderless or windowed fullscreen. With *exclusive* fullscreen, switch the game to borderless.
* **F7** (`RefreshKey` in `config.lua`, `""` turns it off) does the same as the **Refresh** button, in game or in the window.
  It rescans the character and its meshes, relists the characters, and immediately retries a pose hook that was refused, instead of waiting out the retry delay.
* **Before / After** (in the character banner) flips between the game's own body and your edits on every character, without changing any slider.
  Moving a slider or closing the window switches back to After. **F8** (`CompareKey` in `config.lua`, `""` turns it off) shows the original only while you hold it, in game or in the window.
* **Character banner** at the top: who the sliders edit, with the engine name and whether the pose hook is live.
  **Change character** opens a grid of every `Character` loaded in the level (nearest first). *Automatic* picks the game profile's character, or otherwise the pawn you control.
  A small square marks characters with edited sliders. The list is only built while the window is open. **Refresh** (in the grid) rescans all actors within the current level.
  **Rename** gives the character a name of your own. It is saved in `UUEPBS Presets\_characters\_names.txt` (for the player and for NPCs the mod recognises after a reload; other characters keep it for the visit).
* **Portraits:** the banner and the grid show a picture of each character when there is one, else three letters of its name.
  * Your own: put a PNG or JPG in **`UUEPBS Presets\_portraits\`** named after the character: its remembered name (identity), the name you gave it, its game name or its engine name, e.g. `Anca.png` or `BP_Rokuv3_C_0.jpg`. Case does not matter. Your images always win.
  * Automatic (while the window is open, one every 1.5 s at most): the script saves `<name>.auto.png` in the same folder. If the game profile names the character's own portrait texture, that one is used, e.g. `"PortraitTexture": ["CharacterData.FaceIcon"]` (property path from the character actor; a list tries each). Otherwise a small camera takes a head shot of only that character, tuned with `"PortraitBone"` (default: `head` and common variants), `"PortraitDistance"` (cm, default 60) and `"PortraitFov"` (degrees, default 30).
  * **Retake portrait** (in the Rename window) takes a new one. Delete a `.auto.png` to have it taken again next time. `AutoPortraits = false` in `config.lua` keeps only your own images.
* **Preset browser** (Presets tab): every saved preset as a tile with its thumbnail, name and tags. Click to see what it changes, double-click (or **Load**) to load it. **Grid** / **List** switches the view.
  * **Search** matches every word against the name and the tags (`hips roku`); `#word` only matches tags starting with it. The tag chips under the search box filter too (click again, or **Clear**, to drop them).
  * **Tags**: type into **Add tag...** next to the preset details and press Enter (commas add several); click a tag's **x** to remove it. Saved in `UUEPBS Presets\_library\presets.json`, which you can also edit by hand.
  * **Thumbnails**: saving a preset takes a whole-body picture of the current character (one every 1.5 s at most, while the window is open), saved as `_library\<preset>.auto.png`. Your own PNG/JPG named after the preset (`_library\Curvy.png`) always wins. **Retake picture** takes a new one of the character as it looks right now. The framing comes from the character's bounds; `"ThumbnailDistance"` (cm) and `"ThumbnailFov"` (degrees, default 40) in the game profile override it. Deleting a preset deletes its thumbnail and tags.
* **Body** tab (called the Simplified Panel in 2.2–2.6): pick a category on the left (Head & Neck, Torso, Chest, Hips & Legs, Arms...), then use its curated sliders for the head, neck, rib cage, waist, hips, shoulders, breasts, butt, thighs, calves, arms and so on.
  A slider only appears when the skeleton has matching bones. Hover a slider's name for what it does and how many bones it drives (Advanced > Bones lists them).
  Left/right pairs and twist/roll/jiggle helper bones move together. **XYZ** splits the size into Length (X, along the bone), Width (Y) and Depth (Z).
  Scale sliders run from 0.1× to 10× on a log scale; the tick marks 1.0×. **Ctrl + mouse wheel** nudges a slider by 0.01, and **Ctrl + Shift + wheel** by 0.001.
  Click the number box next to a slider to type a value. **Reset** on the section bar resets the whole category.
  The mesh's morph targets get sliders here too, sorted by name into the body categories plus **Face**, **Body shape**, **Groin** and **Other** (`Morph_BreastEnl` becomes *Breast enlarge* under Chest, `Exp_BrowUpL` + `Exp_BrowUpR` become one *Brow up* slider under Face, names nothing matches land in Other).
  Correctives and clothing helpers (`Crctve_*`, `*Correct*`, `Morph_Helmet`...) stay under Advanced > Morphs only, because the game drives them. Morph groups from the game profile (`MorphGroups`, see below) come first and take their morphs out of the automatic sorting.
  Under Advanced > Morphs the tooltip says where each morph sits on the Body tab, and the search also finds the readable names.
  Bone-built shape sliders fill in what the mesh has no morph for (*Breast cleavage*, *Butt lift*, *Hip width*...). A clearly named morph target always takes a slider's place; cryptic morph names stay under Other.
  Anatomy bones without a morph target get bone-built sliders under Groin (*Penis size*, *Penis length*, *Penis girth*, *Glans size*, *Testicle size*, *Vulva size*), matched by bone name (`penis_01`, `Cock2`, `glans`, `testicle_l`, `scrotum`, `labia_l`...; `ball_l` toes are left alone). A morph target that does the same job hides the bone slider.
  **Changed only** hides every body part and morph group that is still at the game's own shape.
* **Undo / Redo** (buttons in the character banner, **Ctrl+Z** and **Ctrl+Y** / **Ctrl+Shift+Z** while the window has focus) step back and forth through slider changes.
  A whole drag, a burst of Ctrl+wheel nudges, a preset load or a reset counts as one step. Each character has its own history (up to 100 steps), so undoing never touches another character's sliders.
* **Symmetry tools** (row above the sliders on the Body and Advanced tabs):
  * **Symmetry lock** (on by default): editing a left or right bone, or a `…_L`/`…_R` morph, under Advanced also gives its partner the mirror image. Body tab parts always move both sides together.
  * **Mirror L to R** / **Mirror R to L** copy every edit on one side of the character onto the other side as a mirror image (bones and morphs). Edits on the receiving side are replaced, and a bone there whose partner is unedited goes back to the game's shape. Bones in the middle (spine, head) are not touched.
  * **Randomize** (Body tab) gives every body part a random size, and morph groups a random weight (automatic Face, Groin and Other sliders, forms and pregnancy are left alone), inside the **Limits**: *Size ±* (default 15 %), *Shape ±* (extra per-axis variation, default 5 %), the morph weight range (default 0–0.6), which of the two to touch, and *Only <category>* to leave the other categories alone. Both sides of a part always get the same values; rotations, moves and Children settings are kept.
  * Each Mirror or Randomize click is exactly one undo step, so **Ctrl+Z** takes it back.
* **Advanced** tab (called the Detailed Panel in 2.2–2.6, the Bones tab before). When the meshes have morph targets it has two sub-tabs, **Bones** and **Morphs**.
  * **Bones:** every bone of the primary mesh, with search and "edited only".
    * **Scale**, **Rotate** (roll/pitch/yaw in degrees, pivoting on the joint) and **Move** (cm), all in the bone's own axes (X runs along the bone).
    * **Children** mode:
      * *Scale children too* is the engine's behaviour.
      * *Keep children size* makes children follow the new shape without growing.
      * *This bone only* changes nothing below the bone.
    * With **Symmetry lock** on, the opposite bone gets the true mirror image. The per-bone axis conversion is measured from the skeleton's reference pose, or from the first animated frame if the game doesn't expose the reference pose.
      The Status tab shows which one was used.
  * **Morphs:** every morph target on the tracked meshes, with search and "edited only". Sliders run 0–1; the box takes any value from −2 to 2 (empty = the game's value).
    * A morph you move **replaces** the game's own value, even at 0. **Reset** gives it back, and the value the game had before comes back.
    * With **Symmetry lock** on, `…_L`/`…_R` twins move together.
    * A **!** next to a name means the game keeps setting that morph itself (an animation or its own code). UUEPBS puts your value back each time it notices, but it may flicker or not stick.
* **Character Scale** tab: **Height** (whole character, scaled from the feet), broad proportions (head size, neck, shoulders, torso, arm and leg length, hands, feet) and **Muscle** (thickness of the arms, legs, neck, waist, chest and shoulders without changing their length, plus *Overall muscle* for all of them at once).
  They apply on top of the Body and Advanced sliders and use the Body tab's bone groups, so a slider only appears when the skeleton has those bones. Collision and the camera don't change.
* **Looks** tab: pick **Skin** or **Hair** on the left, then move the sliders.
  * **Skin tone** (below 0 tans, above 0 pales) and **Sweat** (a wet sheen) change every skin slot of the outfit (body, head, arms...), so the seams match. Eyes, mouths and clothes are left alone.
  * **Hue**, **Saturation** and **Lightness** shift the hair's own colour (hair, brows, lashes), so strands keep their detail. MetaHuman-style hair (Melanin / Redness) works too.
  * Looks belong to the character like its sliders: saved with the session, remembered for NPCs, and stored in presets. Loading a preset that has no looks keeps the current ones.
  * How it works: each skin or hair material slot gets a dynamic material instance with shifted parameter values (tint colour, roughness, wetness...). Reset gives the slot its original material back. The line under the sliders says what was changed.
  * If nothing changes in a game, its materials use other names. Set `Verbose = true` in `config.lua`: the log lists every skin/hair slot and its parameters. Then name them in the game profile:
    ```json
    "Looks": {
      "Skin": { "Slots": ["*body*", "*head*"], "ExcludeSlots": ["*eye*"], "Tint": ["SkinColor"], "Wet": ["Wetness"], "Roughness": ["Roughness*"] },
      "Hair": { "Slots": ["*hair*"], "Tint": ["RootColor", "TipColor"], "Melanin": ["Melanin"], "Redness": ["Redness"] }
    }
    ```
    Slots match the slot or material name; parameter names without `*` are used even if the material does not list them. `Looks = false` in `config.lua` turns the feature off.
* **Presets** tab: save, load (or double-click), delete and open the folder. Clicking a preset once shows what it changes next to the list:
  body parts with their size (and whether they are rotated or moved), other bones, morphs with their weights, and anything the current character doesn't have. Presets are JSON files in **`<Win64>\UUEPBS Presets`** and hold bones and morphs.
  The current sliders auto-save to `_last_session.json` and come back next time.
  Bone and morph names aren't case-sensitive, and names a character doesn't have are skipped, so one preset can be shared between characters on the same rig.
  * **Share codes:** **Copy current sliders** (or **Copy picked preset**) copies a preset as one line starting with `UUEPBS1.` that you can paste into chat or a mod page. **Paste code** reads a code from the clipboard and shows its name and size; **Apply to <character>** loads it, **Save as preset** writes it to the folder. Line breaks or spaces added by chat apps are ignored. A code holds only bone and morph values (a compact preset JSON with a checksum), never files or paths.
  * **Preset hotkeys:** set `NextPresetKey` / `PrevPresetKey` in `config.lua` (e.g. `"PageDown"` / `"PageUp"`) to step through your saved presets in alphabetical order, wrapping around, on the character picked in the window. Each character remembers where it is in the list. A key already used for the window, Refresh or CompareKey is ignored.
* **Status** tab: hook details, the Lua link, mirroring, morph counts, the **window skin** picker, tracked meshes, **Rescan**, and **Write diagnostics**, which dumps what the hook finder sees into the log for bug reports.

UE4SS console: `uuepbs` (toggle window), `uuepbs status`, `uuepbs rescan`, `uuepbs list`, `uuepbs load <name>`, `uuepbs save <name>`, `uuepbs next|prev` (cycle presets), `uuepbs on|off`, `uuepbs compare [on|off]` (before/after; without on/off it switches), `uuepbs reset`, `uuepbs diag`,
`uuepbs skin` (list skins), `uuepbs skin <name>` (switch; `Default` = built-in look), `uuepbs morph <name> <weight>` (set a morph; without a weight it goes back to the game).
`ubs …` is the short form.

### Window skins

Skins live in **`<Win64>\UUEPBS Skins\<name>\`**: a `skin.json` plus PNG or JPG images. The folder is created the first time the window opens, with a `README.txt` that lists every key and the example skin **Example - Midnight Rose**.
Pick a skin in **Status > Window skin** or with `ubs skin <name>`; the choice is remembered. `Skin` in `config.lua` sets the one used until you pick another.

* **What a skin can change:** every colour (accent, text, window, panels, frames, buttons, tabs, headers, scrollbars; any Dear ImGui colour by name), sizes (rounding, padding, spacing, borders, font scale), a TTF/OTF font, and images for the
  window background, the title strip, a logo in place of the title, panels, buttons (normal/hover/pressed), slider track/fill/grab, checkboxes and tabs, plus icons for buttons and each Body tab category (`"section.Hips & Legs"`).
  A skin with slider images keeps its own sliders instead of the thin built-in ones.
* **What it can't change:** the layout. Skins are looks only.
* **Images** can stretch, cover, contain, centre, tile, or nine-slice (corners keep their size, so one small frame image fits any button). Anything a skin leaves out keeps the built-in look, and an image that can't be loaded falls back to the normal widget; the Status tab lists the problem.
* **Editing:** the window reloads the active skin as soon as you save `skin.json` or one of its images, so you can tweak it with the game running. Start by copying the example folder.
* Limits: PNG or JPG, at most 4096×4096 and 32 MB per file (bigger than 2048 is scaled down); files must be inside the skin's folder. Images are decoded inside the game process, so only use skins from people you trust.
* With the CPU renderer, large images are sampled once and reused, so a skinned window costs about the same per frame as the built-in look; only the first frame after a skin loads or a tab opens is slower.

### Performance settings (`config.lua`)
* `WindowFps` (default 30): frame cap for the slider window while you drag sliders. 15–60.
* `PollIntervalMs` (default 600): how often the script exchanges messages with the DLL (console commands, picker, rescan requests).
* `MorphPollMs` (default 100): while the window is open, morph slider changes are applied this often, so dragging feels live. Nothing runs at this rate while the window is closed. `0` = only every `PollIntervalMs`.
* `WatchIntervalMs` (default 5000): how often the script checks that the character and its meshes are still valid. Only cheap validity checks run on this timer.
  A full scan (which does walk the object list once) only happens when something changed: a new pawn, a destroyed mesh, a level change, F7 or **Refresh**.
* After the pose hook first goes in, the mod rescans the character once by itself a few seconds later, so games that set the character up late no longer need F7.

### Game profiles (set up automatically)

The first time the mod runs in a game, it writes a small profile for that game to **`Scripts\GameProfiles\<project>.json`**, then reads it on every start.
`<project>` is the Unreal project folder, the one that contains `Binaries`. For GON it is `Roku3`, and for Wuchang it is that game's project folder.
Install the mod in as many games as you like; each one gets its own file.

```json
{
  "Project": "Roku3",
  "Game": "GON - God Of Nothing",
  "Target": "BP_Rokuv3_C",
  "TargetClassPath": "/Game/ROKUv3/BP_Rokuv3.BP_Rokuv3_C",
  "PrimaryComponent": "CharacterMesh0",
  "IncludeComponents": [],
  "ExcludeComponents": [],
  "SameSkeletonOnly": true
}
```
* A new game's profile starts from `Default` in `config.lua`, which means: edit the pawn you control, list its `Mesh`, and include meshes on the same skeleton.
  GON ships with the profile above.
* **`Target`**: an actor class (the UE4SS object dump or Live View shows it), or `""` for the pawn you control.
* **`PrimaryComponent`**: the mesh whose bones are listed. **`Include/ExcludeComponents`**: mesh names to force in or out.
* Changes take effect the next time the game starts.
  * If the file has a mistake, the console shows the line and column, the mod uses the defaults, and your file isn't overwritten.
  * Delete a profile to have it regenerated.
* Profiles from an older `config.lua` (`Profiles = { ... }`), or from an XTGBodySlider install next to this one, are carried over automatically the first time.

**Morph targets in the profile** (both optional, applied while the game runs):
```json
"MorphGroups": {
  "Breasts": ["BreastSize*"],
  "Belly":   { "Morphs": ["Belly", "Tummy_Round"], "Section": "Body shape", "Hint": "Belly size" }
},
"ExcludeMorphs": ["*_corrective*", "Viseme_*"],
"AutoMorphGroups": true
```
* **`MorphGroups`** adds sliders to the Simplified Panel. Each drives every morph whose name matches (names from Detailed Panel > Morphs; `*` = anything, `#` = digits, not case-sensitive).
* **`ExcludeMorphs`** hides morphs from the window, e.g. corrective shapes and face visemes. Hidden morphs are never touched.
* **`AutoMorphGroups`** (default `true`): morphs no `MorphGroups` entry claims are sorted onto the Body tab by name. `false` keeps only your own groups there.

**Character names in the window** (optional):
```json
"Names": {
  "BP_Rokuv3_C": "Roku",
  "00_EVE_*":    "Eve"
}
```
* The window shows each character by a readable name. It uses, in this order: the game's own name for the character (`DisplayName`, `CharacterName` and similar, on the character or on its character data), then this table, then the engine name cleaned up (`BP_Guard_C_3` becomes "Guard", `00_EVE_01_Body` becomes "Eve").
* **`Names`** maps engine names to the name you want. A key can be the character's object name (`BP_Eve_C_0`), its class (`BP_Eve_C`), or one of its mesh assets (`00_EVE_01_Body`). The engine names are in the tooltip of the Character list and on the Status tab. `*` matches anything, a plain key also matches every name that starts with it, and matching is not case-sensitive.
* Only the labels change. Saved sliders, remembered NPCs and presets still go by the engine names, so renaming here loses nothing. Set `FriendlyNames = false` in `config.lua` to see the engine names in the list again.

### Simplified Panel mapping (bone dictionary)

The Simplified Panel sliders (Thighs, Waist, Breasts, Biceps…) find their bones through **`Scripts\BoneDictionary.json`**.
You can edit it, and the window picks up changes within a couple of seconds. A copy is built into the DLL, so a missing or broken file never breaks anything.

* **Supported out of the box:** UE4/UE5 mannequin, MetaHuman (including `upperarm_bicep`, `latissimus` and `clavicle_pec` helpers), Mixamo, Daz Genesis, Blender Rigify and Auto-Rig Pro (`thigh_stretch.l`, `.x` centre bones), Character Creator, VRoid, 3ds Max Biped and **Advanced Skeleton** (Wuchang: `Hip_L` = thigh, `Shoulder_L` = upper arm, `Root_M`, `SDtui`/`SDabi` helpers).
  Category prefixes like BoR's `MOT_`/`SUP_`/`PRG_` are stripped, and some pinyin and romaji words are included (`datui`, `dabi`, `xiaotui`, `pigu`, `momo`, `shiri`…).
* **Muscles section:** biceps, triceps, deltoids, pecs, lats, traps, serratus, abs, obliques, quads, hamstrings and calf muscles. It appears only when the rig has such helper bones.
* **How matching works:** bone names are cleaned (prefixes, the left/right marker and `_M`-style suffixes removed), then compared with each group's patterns (`*` = anything, `#` = digits, `=Name` = the exact original name).
  A group's first matching pattern wins, and both sides are taken together. Twist, roll, stretch and "thicker" helpers of limbs come along automatically.
  Rig **Styles** (such as Advanced Skeleton) are detected from the bone names and re-point the groups whose names mean something different in that rig.
* The **Detailed Panel** shows each bone's group next to its name, e.g. `Hip_L [Thighs]`, and the **Status** tab shows the detected rig style.

**Per-game remapping, no dictionary editing needed.** Add `BodyGroups` to the game's `Scripts\GameProfiles\<project>.json`, using real bone names from the Detailed Panel:
```json
"BodyGroups": {
  "Thighs": ["Hip_L", "Hip_R", "SDtui*"],
  "Waist":  ["Spine1_M"],
  "Eyes":   [],
  "Tail":   { "Bones": ["tail_01"], "Section": "Extras", "Children": "scale_too" }
}
```
* **Overriding a slider:** a listed group replaces the automatic match for that slider.
* **Hiding a slider:** `[]` hides it.
* **Adding a slider:** a new name adds a slider. It can take `Section`, `Hint` and `Children` (`scale_too`, `keep_size` or `this_bone_only`).
* **When it applies:** changes apply while the game runs. If the JSON has a mistake, the Status tab says so and the automatic mapping is used.

### Pinning the pose hook for a game

The hook finder normally works everything out by itself. When it succeeds, the log (`native\UUEPBS.log`) prints a ready-made line like:
```
"Hook": { "Slot": 330, "Buffers": "0x610", "ReadIndex": "0x658" }
```
Paste it into the game's `Scripts\GameProfiles\<project>.json` to pin those values. The search is then skipped, and only the pinned values are checked against the live character.
Game profiles can be shared, so this works as a per-game hook database, and a profile shared by someone else fixes a game whose automatic search fails.
If a game update moves things, the check fails, the log says so, and you delete the line to search again.

### Preset format
```json
{
  "format": "UUEPBS preset",
  "version": 3,
  "bones": {
    "boob_l":  { "length": 1.25, "width": 1.25, "depth": 1.25, "children": "scale_too" },
    "thigh_l": { "length": 1,    "width": 1.12, "depth": 1.12, "children": "keep_size" },
    "head":    { "length": 1, "width": 1, "depth": 1, "rotate": [0, 0, 10], "move": [0, 0, 1.5], "children": "scale_too" }
  },
  "morphs": { "BreastSize": 0.8, "Belly": 0 },
  "proportions": { "height": 1.05, "leg_length": 1.1 },
  "looks": { "skin.tone": -1.5, "skin.sweat": 1, "hair.lightness": 0.5 }
}
```
* `proportions` (optional) holds the Character Scale sliders, 1 = unchanged: `height`, `head`, `neck`, `shoulders`, `torso`, `arm_length`, `hands`, `leg_length`, `feet`.
  A preset without it resets them when loaded. The file version does not change, and older builds ignore the section.
* `looks` (optional) holds the Looks tab: `skin.tone`, `skin.sweat`, `hair.hue`, `hair.saturation`, `hair.lightness`. Older builds ignore it.
* `length`/`width`/`depth` are X/Y/Z scale. `rotate` is in degrees and `move` in cm, both optional.
* `morphs` (optional) holds morph weights. A listed morph replaces the game's value, even at 0. Loading a preset without `morphs` gives every morph back to the game. Presets without morphs are still written as version 2, so older builds read them unchanged.
* `children` is `scale_too`, `keep_size` or `this_bone_only`.
* `"scale": 1.2` and `"scale": [1, 1.2, 1.2]` shortcuts work. Older `.rbs` files still load.
* Thumbnails and tags are not part of the preset file; they live in `UUEPBS Presets\_library\` (see the preset browser above).
### Limits
* The change is visual only. Collision capsules, physics bodies and hit boxes keep their original size.
* <b>UUEPBS presets are generally not interchangeable between games, that is to say a preset made for Stellar Blade will not work with a preset made for Wuchang, and that won't work with a preset made for Expedition 33. This is simply because every UE game developer uses different practices and character rigs for the player. And thus there is no possibility to generalise the naming scheme of body models.</b>
* Generally, it should work for most UE4.2x–5.x game that runs UE4SS Lua mods and animates characters with skeletal meshes, however, there maybe edge cases where if the game uses its own custom UE build, it may not work until a new config for it is made.
  The following aren't supported, so do not even try it:
  * games with anti-cheat (don't try online games);
  * heavily modified engines;
  * characters deformed by morph targets or vertex animation instead of bones.
## Tips & Tricks
* If the simplified Body tab only manages to show very few bones, then you need to use the detailed Bones tab.
* If UUEPBS is not having any effects on your character upon initial loading of the game, press F7 to rescan and refresh the player character.
* Share your UUEPBS with fellow gamers!
## Credits
* Claude
* Uses [Dear ImGui](https://github.com/ocornut/imgui)
* UUEPBS links third-party code under this license.
(It runs alongside UE4SS but contains and links no UE4SS code.)

=== Dear ImGui (https://github.com/ocornut/imgui) ===
The MIT License (MIT)

Copyright (c) 2014-2025 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
