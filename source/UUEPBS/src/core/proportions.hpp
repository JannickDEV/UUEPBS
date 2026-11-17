// UUEPBS - Character Scale tab: overall height and broad proportions
//
// A layer on top of the bone edits. Each slider multiplies the bones of one or
// more Body tab groups (Head, Upper arms + Forearms, Thighs + Calves, ...), so a
// Body tab "Thighs 1.2" and a Character Scale "Leg length 1.1" both apply.
// Height is not a bone edit: it scales the whole pose about the mesh origin
// (the feet on most characters), so the character stays on the ground.
//
// The values live in the character's EditBook as pseudo entries keyed
// "@scale:<id>" (uniform axis = the value), so they travel with everything that
// already carries a book: per-character memory, the session, undo and presets.
// Presets write them as a separate "proportions" object, never under "bones";
// older builds ignore that key.
//
// The same mechanism carries the "Muscle" sliders (girth only: thicker limbs, same length)
// and the Body tab's bone-built "universal morphs" (Penis length, Testicle size... for
// meshes that have the bones but no morph target; see bone_parts.hpp).
#pragma once

#include "body_groups.hpp"
#include "sculpt.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace uuepbs
{
    // Which bone axes a slider scales. X runs along the bone (sculpt.hpp).
    enum class ScaleAxes : uint8_t
    {
        Uniform, // size: X, Y and Z
        Length,  // X only: longer, same thickness
        Girth,   // Y and Z only: thicker, same length (muscle / bulk sliders)
        // Directional, using the measured rig directions (measure_long_axes): the local axis
        // that points sideways / forward / up on this bone.
        Width,    // scale sideways
        Depth,    // scale front to back
        Vertical, // scale top to bottom (a taller, stretched butt)
        // Moves and turns go on a group's top bones only (GroupBones::tops), so a chain of breast or ear bones moves and
        // turns once.
        Lift,     // offset slider: move up (+) or down (-)
        Push,     // offset slider: move forward (+) or back (-)
        Together, // offset slider: move towards the middle (+) or apart (-)
        // Turn offset sliders, `amount` = degrees at 1.0, about axes taken from the measured directions:
        Tilt,  // the forward-pointing end turns up (+) or down (-): breast perkiness, nipple angle
        Splay, // the forward-pointing end turns outward (+) or inward (-): breasts pointing apart
        Flare, // the upward-pointing end turns outward (+) or inward (-): ears and horns spreading
        // Character Scale > Camera (camera.hpp): no bones, the Lua script moves the game's camera.
        CameraFollow,   // toggle (offset kind, 0 = off, 1 = on): the camera follows Height
        CameraLift,     // offset slider in centimetres: camera pivot up (+) or down (-)
        CameraDistance, // factor: how far the camera sits from the character
    };
    // True for the axes kinds whose value is an offset (neutral 0, -1..1) rather than a factor.
    bool is_offset_axes(ScaleAxes axes);
    // True for the Camera sliders: they never touch a bone.
    bool is_camera_axes(ScaleAxes axes);

    // Which bones of a slider's groups it scales (GroupBones::chain).
    enum class Reach : uint8_t
    {
        Bones, // the group's own bones (a tail's root, a hand)
        Chain, // the group's bones and everything hanging below them: a whole tail, an ear and its tip
        Below, // only what hangs below the group's bones: the fingers of a hand, the toes of a foot
        Own,   // the group's own bones, while a "scale children too" size set on the group still reaches what hangs
               // below (Foot length: a longer foot, same toes, but Foot size still scales the toes)
    };

    // A further effect of the same slider value on other groups, of the same kind as the slider (offsets and turns
    // only): Breast gravity lowers the breasts and tilts them down, Lower back arch turns the waist back and the rib
    // cage forward again.
    struct SliderEffect
    {
        std::vector<const char*> groups;
        ScaleAxes axes;
        double amount; // as ScaleSlider::amount; a negative amount works the other way
    };

    struct ScaleSlider
    {
        const char* id;      // preset key, e.g. "leg_length"
        const char* section; // Character Scale: "Overall", "Proportions", "Muscle"; Body tab: its category
        const char* title;
        const char* hint;
        std::vector<const char*> groups; // Body tab group titles; empty (and no part) = the whole character (height)
        ScaleAxes axes;
        Spread spread; // for bones the Body tab or Advanced tab has not touched
        double lo;
        double hi;
        // Body tab groups scaled uniformly along with `groups` (muscle helper bones: biceps, quads...).
        std::vector<const char*> helpers = {};
        // Bone parts (bone_parts.hpp) the slider drives instead of / besides Body tab groups.
        std::vector<const char*> parts = {};
        // true = a Body tab slider ("universal morph"), false = Character Scale tab.
        bool body = false;
        // Body tab only: a real morph target naming one of `morph_words` (and one of
        // `morph_dims`, see part_covered_by_morph) replaces this slider.
        std::vector<const char*> morph_words = {};
        std::vector<const char*> morph_dims = {};
        // Offset sliders: how far 1.0 moves the bones, as a fraction of the character's height
        // (Lift / Push / Together), or how far it turns them in degrees (Tilt / Splay / Flare).
        double amount = 0.0;
        // Groups only: which of their bones (see Reach). Anything but Bones turns the group's chain into "keep
        // children size" bones that carry the group's own size explicitly, so a longer tail does not compound
        // its length down the chain.
        Reach reach = Reach::Bones;
        // More effects of the same value (see SliderEffect).
        std::vector<SliderEffect> also = {};

        bool offset() const { return is_offset_axes(axes); }
        double neutral() const { return offset() ? 0.0 : 1.0; }
    };

    // Body tab size rows (Body tab groups by title) and the morph words that replace them:
    // a mesh with "Morph_BreastEnl" shows that morph instead of the bone-built "Breast size".
    // Empty when the group has no entry (custom groups, horns...).
    const std::vector<const char*>& group_morph_words(std::string_view group_title);
    bool group_covered(std::string_view group_title, const std::vector<std::string>& morphs);

    constexpr const char* kScaleKeyPrefix = "@scale:";
    constexpr double kScaleNeutralEps = 1.0e-4;

    const std::vector<ScaleSlider>& scale_sliders();
    const ScaleSlider* find_scale_slider(std::string_view id);

    // The slider's range. Use these instead of `lo` / `hi`: Height's range comes from config.lua
    // (HeightMin / HeightMax, sent by the script), the others are fixed.
    double scale_lo(const ScaleSlider& s);
    double scale_hi(const ScaleSlider& s);
    // What a typed value may reach: the track's range is only where dragging goes. Like RaceMenu, a body
    // slider takes values far past its track (factors 0.02..20, offsets -10..10); Height and the camera
    // keep their own range.
    double scale_typed_lo(const ScaleSlider& s);
    double scale_typed_hi(const ScaleSlider& s);
    constexpr double kTypedFactorMin = 0.02; // BoneEdit::clamp keeps any scale within 0.02..20
    constexpr double kTypedFactorMax = 20.0;
    constexpr double kTypedOffsetMax = 10.0;
    // Sets Height's range (config.lua HeightMin / HeightMax). Kept within kHeightRangeMin..kHeightRangeMax,
    // below 1 for the minimum and above 1 for the maximum; non-numbers fall back to the default 0.5..2.
    void set_height_range(double lo, double hi);
    constexpr double kHeightDefaultMin = 0.5;
    constexpr double kHeightDefaultMax = 2.0;
    constexpr double kHeightRangeMin = 0.05; // BoneEdit::clamp keeps any scale within 0.02..20
    constexpr double kHeightRangeMax = 20.0;

    // "@scale:<id>" (already case-folded).
    std::string scale_key(std::string_view id);
    // True for a pseudo entry key ("@scale:..."), compared case-insensitively.
    bool is_scale_key(std::string_view key);
    // The id of a pseudo entry key, empty when it is not one.
    std::string scale_id_of(std::string_view key);

    // The slider's neutral value (1.0, or 0.0 for offset sliders) when it is not set. Offset
    // sliders keep their value in the pseudo entry's shift[0], the others in axis[0].
    double scale_value(const EditBook& book, std::string_view id);
    // Writes (or, at 1.0, removes) the pseudo entry. Values are clamped to scale_typed_lo..scale_typed_hi.
    void set_scale_value(EditBook& book, std::string_view id, double value);
    // The pseudo entry for `id` set to `value` (what Registry::set_bone stores).
    EditEntry scale_entry(std::string_view id, double value);
    bool has_proportions(const EditBook& book);
    // Number of real bone entries (pseudo entries left out).
    size_t bone_entry_count(const EditBook& book);

    // The book the sculptor should bind: real bone edits with the proportion multipliers
    // folded in, pseudo entries removed. `world` receives the height factor (1 = unchanged).
    // `map` must be resolved against the same skeleton the result is bound to.
    // A Girth slider turns an edited bone's "scale children too" into "keep children size", so
    // a thicker forearm never inflates the hand.
    EditBook apply_proportions(const EditBook& book, const BodyMap& map, double& world);

    // Works out, for every bone, which of its local axes runs along it: the one pointing at its
    // farthest child joint (a bone with no children: the direction it came from). Rigs disagree
    // (UE mannequin: X, Blender exports: often Y, some rigs: Z), so Length and Girth sliders use
    // this instead of assuming X. `component_pose` = component-space transforms, any pose.
    void measure_long_axes(BodyMap& map, const std::vector<std::string>& names, const std::vector<int32_t>& parents,
                           const std::vector<Xform>& component_pose);
    // Part of measure_long_axes(): the up / forward / outward axes of every bone and the height.
    // Left comes from left/right bone pairs, forward = up x left, up = component +Z.
    void measure_rig_frames(BodyMap& map, const std::vector<std::string>& names, const std::vector<Xform>& component_pose);
    // The measured axis of `bone` (0 when unknown).
    int long_axis_of(const BodyMap& map, std::string_view bone);
    // Signed local axis (+1..+3 = +X..+Z, -1..-3 = -X..-Z, 0 = unknown) of `bone` that points
    // up / forward / outward (away from the middle; sideways for middle bones).
    BoneFrame frame_of(const BodyMap& map, std::string_view bone);

    // The bones of `gb` a slider with this reach scales.
    std::vector<std::string> reach_bones(const GroupBones& gb, Reach reach);
    // Bones a slider drives on this skeleton (groups, helpers and parts), display spelling.
    std::vector<std::string> scale_slider_bones(const ScaleSlider& s, const BodyMap& map);
    // A Body tab slider whose job a real morph target in `morphs` already does.
    bool scale_slider_covered(const ScaleSlider& s, const std::vector<std::string>& morphs);
} // namespace uuepbs
