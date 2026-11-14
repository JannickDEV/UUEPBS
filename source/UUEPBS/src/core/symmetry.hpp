// UUEPBS - whole-body symmetry tools: copy one side onto the other, and randomize
//
// Both work on an EditSnapshot (bones + morphs) and return the new one; the window applies
// it in one go, so each action is a single undo step.
//
// Mirror: every bone (and morph) on the `from` side is copied onto its partner on the other
// side, with the rotation/move converted through the skeleton's measured MirrorTable. A
// partner whose source has no edit goes back to the game's own value, so the result is
// exactly symmetric. Bones without a side (spine, head) are left as they are.
//
// Randomize: each Body tab part gets a random overall size, plus a little independent
// variation per axis, inside the given limits. Morph groups get a random weight. Every
// bone of a part gets the same values, and parts list both sides together, so the result
// stays symmetric. Rotation, move and Children settings of the bones are kept.
#pragma once

#include "body_groups.hpp"
#include "history.hpp"
#include "mirror.hpp"
#include "rig_names.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace uuepbs
{
    struct MirrorCount
    {
        size_t bones{0};  // partner bones written or cleared
        size_t morphs{0}; // partner morphs written or cleared
    };

    // `bones` / `morphs` are the names the current character actually has (partners that are
    // missing are skipped). `from` must be Side::Left or Side::Right.
    EditSnapshot mirror_sides(const EditSnapshot& now, const std::vector<std::string>& bones, const std::vector<std::string>& morphs,
                              Side from, const MirrorTable& table, MirrorCount* count = nullptr);

    struct RandomLimits
    {
        double size{0.15};      // overall size of a part: 1 +/- size (0.15 = +/-15 %)
        double shape{0.05};     // extra per-axis variation on top: axis = size * (1 +/- shape)
        double morph_lo{0.0};   // morph group weights are picked in [morph_lo, morph_hi]
        double morph_hi{0.6};
        bool parts{true};       // randomize Body tab parts
        bool morphs{true};      // randomize morph groups
        void clamp();           // keeps the limits sane (size/shape 0..0.9, morphs within kMorphMin..kMorphMax)
    };

    struct RandomPart
    {
        std::vector<std::string> bones; // every bone of the part (both sides)
        Spread spread{Spread::Chain};   // Children setting for bones that have no edit yet
    };

    // Deterministic for a given seed.
    EditSnapshot randomize_parts(const EditSnapshot& now, const std::vector<RandomPart>& parts,
                                 const std::vector<std::vector<std::string>>& morph_groups, const RandomLimits& limits, uint64_t seed);
} // namespace uuepbs
