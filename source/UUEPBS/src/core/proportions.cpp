#include "proportions.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace uuepbs
{
    namespace
    {
        const std::vector<const char*> kBreast = {"breast", "boob", "bust", "tit"};
        const std::vector<const char*> kButt = {"butt", "ass", "glute", "buttock", "bum", "booty"};
    } // namespace

    const std::vector<const char*>& group_morph_words(std::string_view group_title)
    {
        static const std::vector<std::pair<const char*, std::vector<const char*>>> table = {
            {"head", {"head"}},          {"neck", {"neck"}},           {"horns", {"horn"}},           {"ears", {"ear"}},
            {"eyes", {"eye", "eyeball"}}, {"rib cage", {"ribcage", "rib"}}, {"waist", {"waist"}},     {"belly", {"belly", "stomach", "tummy"}},
            {"hips", {"hip", "pelvis"}},  {"shoulders", {"shoulder"}},  {"breasts", kBreast},          {"nipples", {"nipple", "areola"}},
            {"butt", kButt},              {"thighs", {"thigh"}},        {"calves", {"calf", "calve"}}, {"feet", {"foot", "feet"}},
            {"upper arms", {"upperarm"}}, {"forearms", {"forearm"}},    {"hands", {"hand"}},           {"tail", {"tail"}},           {"wings", {"wing"}},
        };
        static const std::vector<const char*> none;
        const std::string want = fold_case(group_title);
        for (const auto& [title, words] : table)
        {
            if (want == title)
            {
                return words;
            }
        }
        return none;
    }

    bool group_covered(std::string_view group_title, const std::vector<std::string>& morphs)
    {
        const auto& words = group_morph_words(group_title);
        if (words.empty())
        {
            return false;
        }
        return std::any_of(morphs.begin(), morphs.end(), [&](const std::string& m) { return part_covered_by_morph(m, words, {}); });
    }

    const std::vector<ScaleSlider>& scale_sliders()
    {
        static const std::vector<ScaleSlider> sliders = {
            {"height", "Overall", "Height",
             "The whole character, scaled from the mesh origin (the feet on most characters), so it stays on the ground.\n"
             "Every mesh of the character scales with it, and so does its collision capsule (switch below). The camera can follow it "
             "(Camera section).",
             {}, ScaleAxes::Uniform, Spread::Chain, kHeightDefaultMin, kHeightDefaultMax},
            {"head", "Proportions", "Head size", "The head and everything on it (face, hair bones).", {"Head"}, ScaleAxes::Uniform, Spread::Chain,
             0.2, 5.0},
            {"neck", "Proportions", "Neck length", "Longer or shorter neck; the head keeps its size.", {"Neck"}, ScaleAxes::Length, Spread::Keep,
             0.2, 5.0},
            {"shoulders", "Proportions", "Shoulder width", "Collarbone length: moves the arms in or out.", {"Shoulders"}, ScaleAxes::Length,
             Spread::Keep, 0.2, 5.0},
            {"torso", "Proportions", "Torso length", "Waist and rib cage length; the chest and arms keep their size.", {"Waist", "Rib cage"},
             ScaleAxes::Length, Spread::Keep, 0.2, 5.0},
            {"arm_length", "Proportions", "Arm length", "Upper arm and forearm length; the hands keep their size.", {"Upper arms", "Forearms"},
             ScaleAxes::Length, Spread::Keep, 0.2, 5.0},
            {"hands", "Proportions", "Hand size", "Hands and fingers.", {"Hands"}, ScaleAxes::Uniform, Spread::Chain, 0.2, 5.0},
            {"leg_length", "Proportions", "Leg length", "Thigh and calf length; the feet keep their size.", {"Thighs", "Calves"}, ScaleAxes::Length,
             Spread::Keep, 0.2, 5.0},
            {"feet", "Proportions", "Foot size", "Feet and toes.", {"Feet"}, ScaleAxes::Uniform, Spread::Chain, 0.2, 5.0},

            // Muscle: thickness only (the two axes across the bone), so limbs get bulkier without
            // getting longer. Children keep their size: hands, feet and the head never balloon.
            {"muscle", "Muscle", "Overall muscle",
             "Every muscle slider below at once: thicker arms, legs, neck and chest, same length. "
             "Multiplies with the single sliders.",
             {"Upper arms", "Forearms", "Thighs", "Calves", "Neck", "Rib cage", "Shoulders"}, ScaleAxes::Girth, Spread::Keep, 0.2, 5.0,
             {"Deltoids", "Biceps", "Triceps", "Pecs", "Lats", "Traps", "Serratus", "Abs", "Obliques", "Quads", "Hamstrings", "Calf muscles"}},
            {"upper_arm_girth", "Muscle", "Upper arm thickness", "Biceps and triceps: thicker upper arms, same length.", {"Upper arms"},
             ScaleAxes::Girth, Spread::Keep, 0.2, 5.0, {"Biceps", "Triceps"}},
            {"forearm_girth", "Muscle", "Forearm thickness", "Thicker forearms, same length; the hands keep their size.", {"Forearms"},
             ScaleAxes::Girth, Spread::Keep, 0.2, 5.0},
            {"shoulder_bulk", "Muscle", "Shoulder bulk", "Deltoids and traps: rounder, heavier shoulders. The arms keep their length.",
             {"Shoulders"}, ScaleAxes::Girth, Spread::Keep, 0.2, 5.0, {"Deltoids", "Traps"}},
            {"chest_breadth", "Muscle", "Chest breadth", "A broader, deeper rib cage (pecs and lats along); arms, neck and breasts keep their size.",
             {"Rib cage"}, ScaleAxes::Girth, Spread::Keep, 0.2, 5.0, {"Pecs", "Lats", "Serratus"}},
            {"waist_girth", "Muscle", "Waist thickness", "Thicker or slimmer waist (abs and obliques along), same torso length.", {"Waist"},
             ScaleAxes::Girth, Spread::Keep, 0.2, 5.0, {"Abs", "Obliques"}},
            {"neck_girth", "Muscle", "Neck thickness", "Thicker neck, same length; the head keeps its size.", {"Neck"}, ScaleAxes::Girth,
             Spread::Keep, 0.2, 5.0},
            {"thigh_girth", "Muscle", "Thigh thickness", "Quads and hamstrings: thicker thighs, same leg length.", {"Thighs"}, ScaleAxes::Girth,
             Spread::Keep, 0.2, 5.0, {"Quads", "Hamstrings"}},
            {"calf_girth", "Muscle", "Calf thickness", "Thicker calves, same leg length; the feet keep their size.", {"Calves"}, ScaleAxes::Girth,
             Spread::Keep, 0.2, 5.0, {"Calf muscles"}},

            // Camera: the game's third-person camera (spring arm / camera boom), moved by the Lua script.
            {"camera_follow", "Camera", "Follow height",
             "Raise or lower the camera with Height, and move it back as far as the character grew, so a taller "
             "character is framed like the original one.",
             {}, ScaleAxes::CameraFollow, Spread::Keep, 0.0, 1.0},
            {"camera_height", "Camera", "Camera height",
             "Moves the camera up (+) or down (-), in centimetres, on top of Follow height.", {}, ScaleAxes::CameraLift, Spread::Keep, -200.0,
             200.0},
            {"camera_distance", "Camera", "Camera distance", "How far behind the character the camera sits (1 = the game's own distance).", {},
             ScaleAxes::CameraDistance, Spread::Keep, 0.3, 3.0},

            // Body tab "universal morphs": built from anatomy bones when the mesh has no morph for it.
            {"penis_size", "Groin", "Penis size", "The whole shaft and glans, built from the bones (no morph target on this mesh).", {},
             ScaleAxes::Uniform, Spread::Keep, 0.2, 5.0, {}, {"penis", "glans"}, true, {"penis", "cock", "dick", "phallus"}},
            {"penis_length", "Groin", "Penis length", "Longer or shorter shaft, same thickness; the glans keeps its size. Built from the bones.", {},
             ScaleAxes::Length, Spread::Keep, 0.2, 5.0, {}, {"penis"}, true, {"penis", "cock", "dick", "phallus", "shaft"},
             {"length", "long", "longer", "short", "shorter"}},
            {"penis_girth", "Groin", "Penis girth", "Thicker or thinner shaft, same length. Built from the bones.", {}, ScaleAxes::Girth,
             Spread::Keep, 0.2, 5.0, {}, {"penis"}, true, {"penis", "cock", "dick", "phallus", "shaft"},
             {"girth", "thick", "thicker", "thickness", "thin", "wide", "width", "fat"}},
            {"glans_size", "Groin", "Glans size", "The tip on its own. Built from the bones.", {}, ScaleAxes::Uniform, Spread::Keep, 0.2, 5.0, {},
             {"glans"}, true, {"glans"}},
            {"testicle_size", "Groin", "Testicle size", "Both testicles / the scrotum. Built from the bones.", {}, ScaleAxes::Uniform,
             Spread::Keep, 0.2, 5.0, {}, {"testicles"}, true, {"ball", "testicle", "testes", "testis", "scrotum", "nut"}},
            {"vulva_size", "Groin", "Vulva size", "Labia / vulva bones. Built from the bones.", {}, ScaleAxes::Uniform, Spread::Keep, 0.2, 5.0, {},
             {"vulva"}, true, {"vulva", "labia", "labium", "pussy", "vagina"}},

            // Body tab shape sliders in the spirit of RaceMenu / BodySlide body presets, built from
            // the Body tab's bone groups on any rig. Each one steps aside when the mesh has a clearly
            // named morph target for the same thing (morph_words + morph_dims).
            {"breast_width", "Chest", "Breast width", "Wider or narrower breasts.", {"Breasts"}, ScaleAxes::Width, Spread::Chain, 0.2, 5.0, {}, {}, true,
             kBreast, {"width", "wide", "wider", "narrow", "narrower"}},
            {"breast_projection", "Chest", "Breast projection", "How far the breasts stand out from the chest.", {"Breasts"}, ScaleAxes::Depth,
             Spread::Chain, 0.2, 5.0, {}, {}, true, kBreast, {"projection", "perky", "perkiness", "forward", "pointy", "fantasy", "push"}},
            {"breast_lift", "Chest", "Breast lift", "Higher (perkier) or lower (heavier) breasts.", {"Breasts"}, ScaleAxes::Lift, Spread::Chain, -2.0,
             2.0, {}, {}, true, kBreast, {"lift", "height", "up", "high"}, 0.02},
            {"breast_cleavage", "Chest", "Breast cleavage", "Breasts closer together (+) or further apart (-).", {"Breasts"}, ScaleAxes::Together,
             Spread::Chain, -2.0, 2.0, {}, {}, true, kBreast, {"cleavage", "together", "apart", "spacing", "distance", "gap", "spread"}, 0.012},
            {"nipple_length", "Chest", "Nipple length", "Longer or flatter nipples.", {"Nipples"}, ScaleAxes::Depth, Spread::Chain, 0.2, 5.0, {}, {}, true,
             {"nipple", "nip"}, {"length", "long", "longer", "erect", "puffy"}},
            {"waist_width", "Torso", "Waist width", "A narrower or wider waistline, same torso length.", {"Waist"}, ScaleAxes::Width, Spread::Keep, 0.2,
             5.0, {}, {}, true, {"waist", "waistline"}, {"width", "wide", "wider", "narrow", "narrower", "thin", "slim", "line"}},
            {"back_width", "Torso", "Back width", "A broader or narrower upper back and rib cage.", {"Rib cage"}, ScaleAxes::Width, Spread::Keep, 0.2,
             5.0, {}, {}, true, {"back", "rib", "ribcage", "lat"}, {"width", "wide", "wider", "broad", "narrow"}},
            {"chest_depth", "Torso", "Chest depth", "A deeper (barrel) or flatter rib cage.", {"Rib cage"}, ScaleAxes::Depth, Spread::Keep, 0.2, 5.0, {},
             {}, true, {"chest", "rib", "ribcage"}, {"depth", "deep", "barrel", "flat"}},
            {"belly_projection", "Torso", "Belly projection", "A rounder or flatter belly.", {"Belly"}, ScaleAxes::Depth, Spread::Chain, 0.2, 5.0, {},
             {}, true, {"belly", "stomach", "tummy", "gut"}, {"projection", "round", "out", "bloat", "bloated", "flat", "big", "bigger"}},
            {"belly_lift", "Torso", "Belly lift", "A higher or lower, hanging belly.", {"Belly"}, ScaleAxes::Lift, Spread::Chain, -2.0, 2.0, {}, {}, true,
             {"belly", "stomach", "tummy", "gut"}, {"lift", "sag", "saggy", "droop", "hang"}, 0.015},
            {"hip_width", "Hips & Legs", "Hip width", "Wider or narrower hips: the legs move out or in with the pelvis.", {"Hips"}, ScaleAxes::Width,
             Spread::Keep, 0.2, 5.0, {}, {}, true, {"hip", "pelvis"}, {"width", "wide", "wider", "narrow", "narrower"}},
            {"butt_width", "Hips & Legs", "Butt width", "A wider or narrower butt.", {"Butt"}, ScaleAxes::Width, Spread::Chain, 0.2, 5.0, {}, {}, true,
             kButt, {"width", "wide", "wider", "narrow", "narrower"}},
            {"butt_projection", "Hips & Legs", "Butt roundness", "How far the butt stands out (rounder, bubble shape).", {"Butt"},
             ScaleAxes::Depth, Spread::Chain, 0.2, 5.0, {}, {}, true, kButt, {"projection", "round", "roundness", "apple", "bubble", "out", "shape"}},
            {"butt_lift", "Hips & Legs", "Butt lift", "A higher (perkier) or lower butt.", {"Butt"}, ScaleAxes::Lift, Spread::Chain, -2.0, 2.0, {}, {},
             true, kButt, {"lift", "perky", "sag", "saggy", "droop", "up"}, 0.015},
            {"neck_width", "Head & Neck", "Neck width", "A wider or slimmer neck; the head keeps its size.", {"Neck"}, ScaleAxes::Width, Spread::Keep,
             0.2, 5.0, {}, {}, true, {"neck"}, {"width", "wide", "thick", "thickness", "slim", "thin"}},

            // Length and thickness for parts that hang off the body (RaceMenu-style, any rig): they use the whole
            // chain below the part's root bone (GroupBones::chain), so a curled tail gets longer along its curl
            // and a hand's fingers get longer without a longer palm.
            {"head_width", "Head & Neck", "Head width", "A wider or narrower head; eyes, ears and jaw keep their size.", {"Head"},
             ScaleAxes::Width, Spread::Keep, 0.2, 5.0, {}, {}, true, {"head", "skull"}, {"width", "wide", "wider", "narrow", "narrower"}, 0.0,
             Reach::Own},
            {"head_depth", "Head & Neck", "Head depth", "A deeper or flatter head, front to back.", {"Head"}, ScaleAxes::Depth, Spread::Keep, 0.2, 5.0,
             {}, {}, true, {"head", "skull"}, {"depth", "deep", "flat", "long"}, 0.0, Reach::Own},
            {"ear_length", "Head & Neck", "Ear length", "Longer or shorter ears (pointier), same width.", {"Ears"}, ScaleAxes::Length, Spread::Keep,
             0.2, 5.0, {}, {}, true, {"ear"}, {"length", "long", "longer", "short", "shorter", "point", "pointy", "pointed", "elf"}, 0.0,
             Reach::Chain},
            {"ear_width", "Head & Neck", "Ear width", "Wider or narrower ears, same length.", {"Ears"}, ScaleAxes::Girth, Spread::Keep, 0.2, 5.0, {},
             {}, true, {"ear"}, {"width", "wide", "wider", "narrow", "thick", "big", "bigger"}, 0.0, Reach::Chain},
            {"horn_length", "Head & Neck", "Horn length", "Longer or shorter horns, same thickness.", {"Horns"}, ScaleAxes::Length, Spread::Keep, 0.2,
             5.0, {}, {}, true, {"horn"}, {"length", "long", "longer", "short", "shorter"}, 0.0, Reach::Chain},
            {"horn_thickness", "Head & Neck", "Horn thickness", "Thicker or thinner horns, same length.", {"Horns"}, ScaleAxes::Girth, Spread::Keep,
             0.2, 5.0, {}, {}, true, {"horn"}, {"thickness", "thick", "thicker", "thin", "girth", "width", "wide"}, 0.0, Reach::Chain},
            {"hand_width", "Arms", "Hand width", "Broader or narrower hands; the fingers keep their size.", {"Hands"}, ScaleAxes::Width, Spread::Keep,
             0.2, 5.0, {}, {}, true, {"hand", "palm"}, {"width", "wide", "wider", "broad", "narrow"}, 0.0, Reach::Own},
            {"finger_length", "Arms", "Finger length", "Longer or shorter fingers (and claws), same thickness.", {"Hands"}, ScaleAxes::Length,
             Spread::Keep, 0.2, 5.0, {}, {}, true, {"finger", "fingers", "claw", "claws", "nail"}, {"length", "long", "longer", "short"}, 0.0,
             Reach::Below},
            {"finger_thickness", "Arms", "Finger thickness", "Thicker or slimmer fingers, same length.", {"Hands"}, ScaleAxes::Girth, Spread::Keep,
             0.2, 5.0, {}, {}, true, {"finger", "fingers"}, {"thickness", "thick", "thicker", "slim", "thin", "fat"}, 0.0, Reach::Below},
            {"foot_length", "Hips & Legs", "Foot length", "Longer or shorter feet; the toes keep their size.", {"Feet"}, ScaleAxes::Length,
             Spread::Keep, 0.2, 5.0, {}, {}, true, {"foot", "feet"}, {"length", "long", "longer", "short", "shorter"}, 0.0, Reach::Own},
            {"foot_width", "Hips & Legs", "Foot width", "Wider or narrower feet.", {"Feet"}, ScaleAxes::Width, Spread::Keep, 0.2, 5.0, {}, {}, true,
             {"foot", "feet"}, {"width", "wide", "wider", "narrow"}, 0.0, Reach::Own},
            {"toe_length", "Hips & Legs", "Toe length", "Longer or shorter toes (and claws).", {"Feet"}, ScaleAxes::Length, Spread::Keep, 0.2, 5.0,
             {}, {}, true, {"toe", "toes", "claw"}, {"length", "long", "longer", "short"}, 0.0, Reach::Below},
            {"tail_length", "Tail", "Tail length", "A longer or shorter tail along its whole length, same thickness.", {"Tail"}, ScaleAxes::Length,
             Spread::Keep, 0.2, 5.0, {}, {}, true, {"tail"}, {"length", "long", "longer", "short", "shorter"}, 0.0, Reach::Chain},
            {"tail_thickness", "Tail", "Tail thickness", "A thicker (fluffier) or thinner tail, same length.", {"Tail"}, ScaleAxes::Girth,
             Spread::Keep, 0.2, 5.0, {}, {}, true, {"tail"}, {"thickness", "thick", "thicker", "thin", "fluff", "fluffy", "girth", "width", "fat"},
             0.0, Reach::Chain},
            {"tail_root", "Tail", "Tail base", "A thicker or thinner tail root; the rest of the tail keeps its size.", {"Tail"}, ScaleAxes::Girth,
             Spread::Keep, 0.2, 5.0, {}, {}, true, {"tail"}, {"base", "root"}, 0.0, Reach::Own},
            {"wing_length", "Wings", "Wing length", "Longer or shorter wings along every bone, same thickness.", {"Wings"}, ScaleAxes::Length,
             Spread::Keep, 0.2, 5.0, {}, {}, true, {"wing"}, {"length", "long", "longer", "short", "span", "wingspan"}, 0.0, Reach::Chain},
            {"wing_thickness", "Wings", "Wing thickness", "Thicker or thinner wing bones, same length.", {"Wings"}, ScaleAxes::Girth, Spread::Keep,
             0.2, 5.0, {}, {}, true, {"wing"}, {"thickness", "thick", "thicker", "thin", "width", "wide"}, 0.0, Reach::Chain},
            // More RaceMenu / BodySlide (CBBE, 3BA) style shape sliders, built from the bones on any rig.
            {"breast_perkiness", "Chest", "Breast perkiness", "Breasts tilted up (+, perkier) or down (-).", {"Breasts"}, ScaleAxes::Tilt,
             Spread::Chain, -2.0, 2.0, {}, {}, true, kBreast, {"tilt", "angle", "upturn", "upturned"}, 12.0},
            {"breast_angle", "Chest", "Breast angle", "Breasts pointing apart (+) or towards each other (-).", {"Breasts"}, ScaleAxes::Splay,
             Spread::Chain, -2.0, 2.0, {}, {}, true, kBreast, {"angle", "apart", "outward", "side", "sideways", "east", "west"}, 12.0},
            {"nipple_angle", "Chest", "Nipple angle", "Nipples pointing up (+) or down (-).", {"Nipples"}, ScaleAxes::Tilt, Spread::Chain, -2.0, 2.0, {},
             {}, true, {"nipple", "nip"}, {"up", "down", "angle", "tilt"}, 20.0},
            {"nipple_height", "Chest", "Nipple height", "Nipples higher (+) or lower (-) on the breast.", {"Nipples"}, ScaleAxes::Lift, Spread::Chain,
             -2.0, 2.0, {}, {}, true, {"nipple", "nip", "areola"}, {"height", "high", "low", "position"}, 0.004},
            {"nipple_spacing", "Chest", "Nipple spacing", "Nipples closer together (+) or further apart (-).", {"Nipples"}, ScaleAxes::Together,
             Spread::Chain, -2.0, 2.0, {}, {}, true, {"nipple", "nip"}, {"distance", "spacing", "apart", "together"}, 0.004},
            {"waist_depth", "Torso", "Waist depth", "A deeper or flatter waist, front to back.", {"Waist"}, ScaleAxes::Depth, Spread::Keep, 0.2, 5.0, {}, {},
             true, {"waist"}, {"depth", "deep", "flat", "thick"}},
            {"waist_length", "Torso", "Waist length", "A longer or shorter waist; the rib cage keeps its size.", {"Waist"}, ScaleAxes::Length, Spread::Keep,
             0.2, 5.0, {}, {}, true, {"waist", "midriff"}, {"length", "long", "height", "high", "tall"}},
            {"ribcage_length", "Torso", "Rib cage length", "A longer or shorter rib cage; arms, neck and breasts keep their size.", {"Rib cage"},
             ScaleAxes::Length, Spread::Keep, 0.2, 5.0, {}, {}, true, {"rib", "ribcage", "torso", "chest"}, {"length", "long", "tall"}},
            {"belly_width", "Torso", "Belly width", "A wider or narrower belly.", {"Belly"}, ScaleAxes::Width, Spread::Chain, 0.2, 5.0, {}, {}, true,
             {"belly", "stomach", "tummy", "gut"}, {"width", "wide", "wider", "narrow"}},
            {"shoulder_height", "Torso", "Shoulder height", "Shoulders raised (+) or dropped (-); the arms come along.", {"Shoulders"},
             ScaleAxes::Lift, Spread::Keep, -2.0, 2.0, {}, {}, true, {"shoulder", "clavicle", "collarbone"}, {"height", "high", "low", "drop", "raise"},
             0.01},
            {"hip_depth", "Hips & Legs", "Hip depth", "Deeper or flatter hips, front to back.", {"Hips"}, ScaleAxes::Depth, Spread::Keep, 0.2, 5.0, {}, {},
             true, {"hip", "pelvis"}, {"depth", "deep", "flat"}},
            {"butt_spacing", "Hips & Legs", "Butt spacing", "Cheeks closer together (+) or further apart (-).", {"Butt"}, ScaleAxes::Together,
             Spread::Chain, -2.0, 2.0, {}, {}, true, kButt, {"spacing", "apart", "together", "gap", "crack", "cleavage"}, 0.006},
            {"thigh_gap", "Hips & Legs", "Thigh gap", "Legs set closer together (+) or further apart (-) at the hips.", {"Thighs"}, ScaleAxes::Together,
             Spread::Keep, -2.0, 2.0, {}, {}, true, {"thigh", "leg"}, {"gap", "spacing", "apart", "together", "position"}, 0.008},
            {"thigh_length", "Hips & Legs", "Thigh length", "Longer or shorter thighs; calves and feet keep their size.", {"Thighs"},
             ScaleAxes::Length, Spread::Keep, 0.2, 5.0, {}, {}, true, {"thigh", "upperleg"}, {"length", "long", "longer", "short"}},
            {"thigh_width", "Hips & Legs", "Thigh width", "Wider or slimmer thighs (side to side), same length.", {"Thighs"}, ScaleAxes::Width,
             Spread::Keep, 0.2, 5.0, {}, {}, true, {"thigh"}, {"width", "wide", "wider", "slim", "thin", "outer"}},
            {"calf_length", "Hips & Legs", "Calf length", "Longer or shorter lower legs (knee height); the feet keep their size.", {"Calves"},
             ScaleAxes::Length, Spread::Keep, 0.2, 5.0, {}, {}, true, {"calf", "calve", "shin", "lowerleg", "knee"}, {"length", "long", "height"}},
            {"upper_arm_length", "Arms", "Upper arm length", "Longer or shorter upper arms; forearms and hands keep their size.", {"Upper arms"},
             ScaleAxes::Length, Spread::Keep, 0.2, 5.0, {}, {}, true, {"upperarm", "arm"}, {"length", "long", "longer", "short"}},
            {"forearm_length", "Arms", "Forearm length", "Longer or shorter forearms; the hands keep their size.", {"Forearms"}, ScaleAxes::Length,
             Spread::Keep, 0.2, 5.0, {}, {}, true, {"forearm", "lowerarm"}, {"length", "long", "longer", "short"}},
            {"hand_length", "Arms", "Hand length", "Longer or shorter palms; the fingers keep their size.", {"Hands"}, ScaleAxes::Length, Spread::Keep,
             0.2, 5.0, {}, {}, true, {"hand", "palm"}, {"length", "long", "longer", "short"}, 0.0, Reach::Own},
            {"neck_depth", "Head & Neck", "Neck depth", "A deeper or flatter neck, front to back; the head keeps its size.", {"Neck"}, ScaleAxes::Depth,
             Spread::Keep, 0.2, 5.0, {}, {}, true, {"neck"}, {"depth", "deep", "flat"}},
            {"ear_angle", "Head & Neck", "Ear angle", "Ears spread out to the sides (+) or held up straighter (-).", {"Ears"}, ScaleAxes::Flare,
             Spread::Chain, -2.0, 2.0, {}, {}, true, {"ear"}, {"angle", "spread", "out", "side", "droop", "flop"}, 20.0},
            {"horn_angle", "Head & Neck", "Horn angle", "Horns spread out to the sides (+) or closer together (-).", {"Horns"}, ScaleAxes::Flare,
             Spread::Chain, -2.0, 2.0, {}, {}, true, {"horn"}, {"angle", "spread", "out", "side"}, 20.0},
            // From Touched by Dibella's BodySlide / RaceMenu list (Breasts_Gravity, Breast_NippleWidth, Back_LowArch,
            // Butt_Stretch, Torso_Large, Arm_Thick, Thighs_Thick / Calves_Size), built from the bones.
            {"breast_gravity", "Chest", "Breast gravity", "Heavier, lower breasts that point down (+), or lighter, higher ones (-).",
             {"Breasts"}, ScaleAxes::Lift, Spread::Chain, -2.0, 2.0, {}, {}, true, kBreast, {"gravity", "sag", "saggy", "droop", "heavy", "down"},
             -0.012, Reach::Bones, {{{"Breasts"}, ScaleAxes::Tilt, -10.0}}},
            {"nipple_width", "Chest", "Nipple width", "Wider or narrower nipples, same length.", {"Nipples"}, ScaleAxes::Girth, Spread::Keep, 0.2,
             5.0, {}, {}, true, {"nipple", "nip"}, {"width", "wide", "wider", "narrow", "thick"}},
            {"lower_back_arch", "Torso", "Lower back arch", "A deeper curve in the lower back (+) or a straighter back (-); the chest stays upright.",
             {"Waist"}, ScaleAxes::Tilt, Spread::Chain, -2.0, 2.0, {}, {}, true, {"back", "lowerback", "spine"},
             {"arch", "curve", "sway", "lordosis"}, 8.0, Reach::Bones, {{{"Rib cage"}, ScaleAxes::Tilt, -8.0}}},
            {"torso_thickness", "Torso", "Torso thickness", "A thicker or slimmer waist and rib cage together, same torso length.",
             {"Waist", "Rib cage"}, ScaleAxes::Girth, Spread::Keep, 0.2, 5.0, {"Abs", "Obliques", "Lats", "Serratus"}, {}, true,
             {"torso"}, {"large", "big", "small", "thick", "thickness", "size"}},
            {"butt_stretch", "Hips & Legs", "Butt stretch", "A taller (stretched) or shorter butt, top to bottom.", {"Butt"}, ScaleAxes::Vertical,
             Spread::Chain, 0.2, 5.0, {}, {}, true, kButt, {"stretch", "tall", "height", "long"}},
            {"arm_thickness", "Arms", "Arm thickness", "Thicker or slimmer arms along their whole length; the hands keep their size.",
             {"Upper arms", "Forearms"}, ScaleAxes::Girth, Spread::Keep, 0.2, 5.0, {"Biceps", "Triceps"}, {}, true, {"arm", "arms"},
             {"thick", "thickness", "slim", "thin", "chubby", "fat"}},
            {"leg_thickness", "Hips & Legs", "Leg thickness", "Thicker or slimmer legs, thighs and calves together; the feet keep their size.",
             {"Thighs", "Calves"}, ScaleAxes::Girth, Spread::Keep, 0.2, 5.0, {"Quads", "Hamstrings", "Calf muscles"}, {}, true, {"leg", "legs"},
             {"thick", "thickness", "slim", "thin", "chubby", "fat"}},
        };
        return sliders;
    }

    namespace
    {
        // Height's range from config.lua. Written by the bridge thread, read by the window and the pose hook.
        std::atomic<double> g_height_lo{kHeightDefaultMin};
        std::atomic<double> g_height_hi{kHeightDefaultMax};

        bool is_height(const ScaleSlider& s)
        {
            return std::string_view(s.id) == "height";
        }
    } // namespace

    double scale_lo(const ScaleSlider& s)
    {
        return is_height(s) ? g_height_lo.load(std::memory_order_relaxed) : s.lo;
    }

    double scale_hi(const ScaleSlider& s)
    {
        return is_height(s) ? g_height_hi.load(std::memory_order_relaxed) : s.hi;
    }

    double scale_typed_lo(const ScaleSlider& s)
    {
        if (is_height(s) || is_camera_axes(s.axes))
        {
            return scale_lo(s);
        }
        return s.offset() ? -kTypedOffsetMax : kTypedFactorMin;
    }

    double scale_typed_hi(const ScaleSlider& s)
    {
        if (is_height(s) || is_camera_axes(s.axes))
        {
            return scale_hi(s);
        }
        return s.offset() ? kTypedOffsetMax : kTypedFactorMax;
    }

    void set_height_range(double lo, double hi)
    {
        lo = std::isfinite(lo) ? std::clamp(lo, kHeightRangeMin, 0.99) : kHeightDefaultMin;
        hi = std::isfinite(hi) ? std::clamp(hi, 1.01, kHeightRangeMax) : kHeightDefaultMax;
        g_height_lo.store(lo, std::memory_order_relaxed);
        g_height_hi.store(hi, std::memory_order_relaxed);
    }

    const ScaleSlider* find_scale_slider(std::string_view id)
    {
        const std::string folded = fold_case(id);
        for (const ScaleSlider& s : scale_sliders())
        {
            if (folded == s.id)
            {
                return &s;
            }
        }
        return nullptr;
    }

    std::string scale_key(std::string_view id)
    {
        return std::string(kScaleKeyPrefix) + fold_case(id);
    }

    bool is_scale_key(std::string_view key)
    {
        const std::string_view prefix = kScaleKeyPrefix;
        return key.size() > prefix.size() && fold_case(key.substr(0, prefix.size())) == prefix;
    }

    std::string scale_id_of(std::string_view key)
    {
        return is_scale_key(key) ? fold_case(key.substr(std::string_view(kScaleKeyPrefix).size())) : std::string();
    }

    bool is_offset_axes(ScaleAxes axes)
    {
        return axes == ScaleAxes::Lift || axes == ScaleAxes::Push || axes == ScaleAxes::Together || axes == ScaleAxes::Tilt ||
               axes == ScaleAxes::Splay || axes == ScaleAxes::Flare || axes == ScaleAxes::CameraFollow || axes == ScaleAxes::CameraLift;
    }

    bool is_camera_axes(ScaleAxes axes)
    {
        return axes == ScaleAxes::CameraFollow || axes == ScaleAxes::CameraLift || axes == ScaleAxes::CameraDistance;
    }

    double scale_value(const EditBook& book, std::string_view id)
    {
        const ScaleSlider* s = find_scale_slider(id);
        const auto it = book.find(scale_key(id));
        if (s && s->offset())
        {
            if (it == book.end() || !std::isfinite(it->second.edit.shift[0]))
            {
                return 0.0;
            }
            return it->second.edit.shift[0];
        }
        if (it == book.end() || !std::isfinite(it->second.edit.axis[0]) || it->second.edit.axis[0] <= 0.0)
        {
            return 1.0;
        }
        return it->second.edit.axis[0];
    }

    EditEntry scale_entry(std::string_view id, double value)
    {
        const ScaleSlider* s = find_scale_slider(id);
        const double neutral = s ? s->neutral() : 1.0;
        if (!std::isfinite(value))
        {
            value = neutral;
        }
        if (s)
        {
            value = std::clamp(value, scale_typed_lo(*s), scale_typed_hi(*s));
        }
        EditEntry e;
        e.bone = scale_key(id);
        if (s && s->offset())
        {
            e.edit.shift[0] = value;
        }
        else
        {
            e.edit.axis[0] = e.edit.axis[1] = e.edit.axis[2] = value;
        }
        e.edit.spread = Spread::Chain;
        e.edit.clamp();
        return e;
    }

    void set_scale_value(EditBook& book, std::string_view id, double value)
    {
        const ScaleSlider* s = find_scale_slider(id);
        EditEntry e = scale_entry(id, value);
        const double stored = s && s->offset() ? e.edit.shift[0] : e.edit.axis[0];
        if (std::fabs(stored - (s ? s->neutral() : 1.0)) < kScaleNeutralEps)
        {
            book.erase(scale_key(id));
            return;
        }
        book[scale_key(id)] = std::move(e);
    }

    bool has_proportions(const EditBook& book)
    {
        // Pseudo keys start with '@', which sorts before letters and digits but after a few
        // punctuation marks, so look them up rather than relying on the map order.
        const auto it = book.lower_bound(kScaleKeyPrefix);
        return it != book.end() && is_scale_key(it->first);
    }

    size_t bone_entry_count(const EditBook& book)
    {
        size_t n = 0;
        for (const auto& [key, entry] : book)
        {
            n += is_scale_key(key) ? 0 : 1;
        }
        return n;
    }

    EditBook apply_proportions(const EditBook& book, const BodyMap& map, double& world)
    {
        world = 1.0;
        EditBook out;
        for (const auto& [key, entry] : book)
        {
            if (!is_scale_key(key))
            {
                out.emplace(key, entry);
            }
        }
        // Chain sliders (Tail length, Finger length...) scale bones below a group's own bones. A size set on the group
        // with "scale children too" would multiply their factor down the chain (a 1.5x longer tail would grow 1.5^n),
        // so first every bone of such a group's chain is made a "keep children size" bone that carries the size it
        // inherited within the chain itself.
        {
            std::vector<std::string> flattened;
            for (const ScaleSlider& s : scale_sliders())
            {
                if (s.reach == Reach::Bones || std::fabs(scale_value(book, s.id) - s.neutral()) < kScaleNeutralEps)
                {
                    continue;
                }
                for (const char* title : s.groups)
                {
                    const std::string want = fold_case(title);
                    for (const GroupBones& gb : map.groups)
                    {
                        if (fold_case(gb.group.title) != want ||
                            std::find(flattened.begin(), flattened.end(), want) != flattened.end())
                        {
                            continue;
                        }
                        flattened.push_back(want);
                        std::vector<double> inherited(gb.chain.size(), 1.0);
                        for (size_t i = 0; i < gb.chain.size(); ++i)
                        {
                            const int32_t p = i < gb.chain_parent.size() ? gb.chain_parent[i] : -1;
                            if (p >= 0 && static_cast<size_t>(p) < i)
                            {
                                double carried = 1.0;
                                const auto pe = book.find(fold_case(gb.chain[static_cast<size_t>(p)]));
                                const Spread ps = pe != book.end() ? pe->second.edit.spread : gb.group.spread;
                                if (pe != book.end() && ps == Spread::Chain)
                                {
                                    const BoneEdit& pb = pe->second.edit;
                                    carried = std::cbrt(std::max(1.0e-6, pb.axis[0] * pb.axis[1] * pb.axis[2]));
                                }
                                inherited[i] = inherited[static_cast<size_t>(p)] * carried;
                            }
                            const std::string key = fold_case(gb.chain[i]);
                            auto it = out.find(key);
                            if (it == out.end())
                            {
                                if (inherited[i] == 1.0)
                                {
                                    continue; // nothing to carry; the slider adds its own entry
                                }
                                EditEntry fresh;
                                fresh.bone = gb.chain[i];
                                it = out.emplace(key, fresh).first;
                            }
                            BoneEdit& e = it->second.edit;
                            for (int a = 0; a < 3; ++a)
                            {
                                e.axis[a] *= inherited[i];
                                if (e.joint[a] > 0.0)
                                {
                                    e.joint[a] *= inherited[i];
                                }
                            }
                            e.spread = Spread::Keep;
                        }
                    }
                }
            }
        }
        for (const ScaleSlider& s : scale_sliders())
        {
            if (is_camera_axes(s.axes))
            {
                continue; // the camera is moved by the Lua script (camera.hpp)
            }
            const double v = scale_value(book, s.id);
            if (std::fabs(v - s.neutral()) < kScaleNeutralEps)
            {
                continue;
            }
            const double height = map.height > 1.0 ? map.height : 170.0;
            if (s.groups.empty() && s.parts.empty())
            {
                world = std::clamp(v, scale_lo(s), scale_hi(s));
                continue;
            }
            // Chain lengths (a tail, a finger) move the next joint out along the whole offset to it: a curled tail's
            // joints are not on one axis of the bone, and scaling only that axis would lengthen it by less.
            const bool whole_joint = s.reach != Reach::Bones && s.axes == ScaleAxes::Length;
            auto scale_bone = [&](const std::string& bone, ScaleAxes axes, Spread fresh_spread, double amount) {
                const std::string bk = fold_case(bone);
                auto it = out.find(bk);
                if (it == out.end())
                {
                    EditEntry fresh;
                    fresh.bone = bone;
                    fresh.edit.spread = fresh_spread;
                    it = out.emplace(bk, fresh).first;
                }
                BoneEdit& e = it->second.edit;
                const BoneFrame fr = frame_of(map, bk);
                auto axis_of = [](int8_t signed_axis) { return signed_axis == 0 ? -1 : std::abs(signed_axis) - 1; };
                auto sign_of = [](int8_t signed_axis) { return signed_axis < 0 ? -1.0 : 1.0; };
                if (axes == ScaleAxes::Tilt || axes == ScaleAxes::Splay || axes == ScaleAxes::Flare)
                {
                    // Turning `from` towards `to` by +angle is a turn about from x to (both unit, at right angles), the
                    // way BoneEdit::turn goes round its axes (checked on the reference rig in test_body_editor).
                    const int8_t from = axes == ScaleAxes::Flare ? fr.up : fr.forward;
                    const int8_t to = axes == ScaleAxes::Tilt ? fr.up : fr.out;
                    if (from == 0 || to == 0 || std::abs(from) == std::abs(to))
                    {
                        return;
                    }
                    double f[3] = {0.0, 0.0, 0.0}, g[3] = {0.0, 0.0, 0.0};
                    f[std::abs(from) - 1] = sign_of(from);
                    g[std::abs(to) - 1] = sign_of(to);
                    const double c[3] = {f[1] * g[2] - f[2] * g[1], f[2] * g[0] - f[0] * g[2], f[0] * g[1] - f[1] * g[0]};
                    for (int a = 0; a < 3; ++a)
                    {
                        if (std::fabs(c[a]) > 0.5)
                        {
                            e.turn[a] += (c[a] > 0.0 ? 1.0 : -1.0) * v * amount;
                        }
                    }
                    return;
                }
                if (is_offset_axes(axes))
                {
                    const int8_t dir = axes == ScaleAxes::Lift ? fr.up : axes == ScaleAxes::Push ? fr.forward : fr.out;
                    const int a = axis_of(dir);
                    if (a >= 0)
                    {
                        // Together: towards the middle = against "outward".
                        const double towards = axes == ScaleAxes::Together ? -1.0 : 1.0;
                        e.shift[a] += towards * sign_of(dir) * v * amount * height;
                    }
                    return;
                }
                if (axes == ScaleAxes::Width || axes == ScaleAxes::Depth || axes == ScaleAxes::Vertical)
                {
                    const int a = axis_of(axes == ScaleAxes::Width ? fr.out : axes == ScaleAxes::Depth ? fr.forward : fr.up);
                    if (a >= 0)
                    {
                        e.axis[a] *= v;
                        if (e.joint[a] > 0.0)
                        {
                            e.joint[a] *= v;
                        }
                    }
                    return;
                }
                const int along = long_axis_of(map, bk);
                if (whole_joint && axes == ScaleAxes::Length)
                {
                    for (int a = 0; a < 3; ++a)
                    {
                        if (e.joint[a] <= 0.0)
                        {
                            e.joint[a] = e.axis[a];
                        }
                        e.joint[a] *= v;
                    }
                    e.axis[along] *= v;
                    return;
                }
                for (int a = 0; a < 3; ++a)
                {
                    const bool is_length = a == along;
                    if (axes == ScaleAxes::Uniform || (axes == ScaleAxes::Length) == is_length)
                    {
                        if (axes == ScaleAxes::Girth && e.joint[a] <= 0.0)
                        {
                            e.joint[a] = e.axis[a]; // the joints keep following the user's own scale only
                        }
                        else if (axes != ScaleAxes::Girth && e.joint[a] > 0.0)
                        {
                            e.joint[a] *= v;
                        }
                        e.axis[a] *= v;
                    }
                }
                if (axes == ScaleAxes::Girth && e.spread == Spread::Chain)
                {
                    e.spread = Spread::Keep; // thicker forearm, same hand
                }
            };
            auto each_group = [&](const std::vector<const char*>& titles, auto&& fn) {
                for (const char* title : titles)
                {
                    const std::string want = fold_case(title);
                    for (const GroupBones& gb : map.groups)
                    {
                        if (fold_case(gb.group.title) == want)
                        {
                            for (const std::string& bone : gb.bones)
                            {
                                fn(bone, gb);
                            }
                        }
                    }
                }
            };
            // A group's bones for one effect. Moves and turns go on its top bones only: a bone below another one of
            // the group follows it already, so a chain of breast or ear bones moves and turns once instead of once
            // more at every bone down the chain.
            auto effect_on_groups = [&](const std::vector<const char*>& titles, ScaleAxes axes, Reach reach, double amount) {
                const bool turn = is_offset_axes(axes);
                for (const char* title : titles)
                {
                    const std::string want = fold_case(title);
                    for (const GroupBones& gb : map.groups)
                    {
                        if (fold_case(gb.group.title) != want)
                        {
                            continue;
                        }
                        const std::vector<std::string> bones = turn && !gb.tops.empty() ? gb.tops : reach_bones(gb, reach);
                        for (const std::string& bone : bones)
                        {
                            scale_bone(bone, axes, s.spread, amount);
                        }
                    }
                }
            };
            effect_on_groups(s.groups, s.axes, s.reach, s.amount);
            for (const SliderEffect& more : s.also)
            {
                effect_on_groups(more.groups, more.axes, Reach::Bones, more.amount);
            }
            each_group(s.helpers, [&](const std::string& bone, const GroupBones& gb) { scale_bone(bone, ScaleAxes::Uniform, gb.group.spread, 0.0); });
            for (const char* part : s.parts)
            {
                for (const PartBones& pb : map.parts)
                {
                    if (pb.id == part)
                    {
                        for (const std::string& bone : pb.bones)
                        {
                            scale_bone(bone, s.axes, s.spread, s.amount);
                        }
                    }
                }
            }
        }
        return out;
    }
} // namespace uuepbs

namespace uuepbs
{
    void measure_long_axes(BodyMap& map, const std::vector<std::string>& names, const std::vector<int32_t>& parents,
                           const std::vector<Xform>& pose)
    {
        map.long_axis.clear();
        measure_rig_frames(map, names, pose);
        const size_t n = names.size();
        if (parents.size() != n || pose.size() != n)
        {
            return;
        }
        // Farthest child of every bone, as an offset in the bone's own frame.
        std::vector<double> best(n, 0.0);
        std::vector<std::array<double, 3>> dir(n, {0.0, 0.0, 0.0});
        auto length2 = [](const double v[3]) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; };
        for (size_t i = 0; i < n; ++i)
        {
            const int32_t p = parents[i];
            if (p < 0 || static_cast<size_t>(p) >= n || !xf::finite(pose[i]) || !xf::finite(pose[p]))
            {
                continue;
            }
            const Xform local = xf::relative(pose[i], pose[p]);
            const double d = length2(local.pos);
            if (d > best[p])
            {
                best[p] = d;
                dir[p] = {local.pos[0], local.pos[1], local.pos[2]};
            }
        }
        for (size_t i = 0; i < n; ++i)
        {
            double v[3] = {dir[i][0], dir[i][1], dir[i][2]};
            if (best[i] <= 1e-8)
            {
                // A leaf (twist bone, glans...): keep going the way it came, seen from its own frame.
                const int32_t p = parents[i];
                if (p < 0 || static_cast<size_t>(p) >= n || !xf::finite(pose[i]) || !xf::finite(pose[p]))
                {
                    continue;
                }
                const Xform local = xf::relative(pose[i], pose[p]);
                double inv[4];
                xf::qconj(local.rot, inv);
                xf::qrotate(inv, local.pos, v);
                if (length2(v) <= 1e-8)
                {
                    continue;
                }
            }
            int axis = 0;
            for (int a = 1; a < 3; ++a)
            {
                if (std::fabs(v[a]) > std::fabs(v[axis]))
                {
                    axis = a;
                }
            }
            if (axis != 0)
            {
                map.long_axis[fold_case(names[i])] = static_cast<uint8_t>(axis);
            }
        }
    }

    namespace
    {
        // Signed local axis of rotation `q` closest to direction `d` (unit), 0 when `d` is zero.
        int8_t closest_axis(const double q[4], const double d[3])
        {
            if (d[0] == 0.0 && d[1] == 0.0 && d[2] == 0.0)
            {
                return 0;
            }
            int best = 0;
            double best_dot = 0.0;
            for (int k = 0; k < 3; ++k)
            {
                const double unit[3] = {k == 0 ? 1.0 : 0.0, k == 1 ? 1.0 : 0.0, k == 2 ? 1.0 : 0.0};
                double ax[3];
                xf::qrotate(q, unit, ax);
                const double dot = ax[0] * d[0] + ax[1] * d[1] + ax[2] * d[2];
                if (std::fabs(dot) > std::fabs(best_dot))
                {
                    best_dot = dot;
                    best = k;
                }
            }
            return static_cast<int8_t>(best_dot < 0.0 ? -(best + 1) : best + 1);
        }
    } // namespace

    void measure_rig_frames(BodyMap& map, const std::vector<std::string>& names, const std::vector<Xform>& pose)
    {
        map.frames.clear();
        map.height = 0.0;
        const size_t n = names.size();
        if (pose.size() != n || n == 0)
        {
            return;
        }
        // Height: span of the joints along component Z (Unreal is Z-up).
        double lo = 1e300, hi = -1e300;
        std::map<std::string, size_t> index;
        for (size_t i = 0; i < n; ++i)
        {
            if (!xf::finite(pose[i]))
            {
                continue;
            }
            lo = std::min(lo, pose[i].pos[2]);
            hi = std::max(hi, pose[i].pos[2]);
            index[fold_case(names[i])] = i;
        }
        map.height = hi > lo ? hi - lo : 0.0;
        // The character's left: from every left bone to its right partner.
        double left[3] = {0.0, 0.0, 0.0};
        for (size_t i = 0; i < n; ++i)
        {
            if (find_side(names[i]).side != Side::Left)
            {
                continue;
            }
            const auto j = index.find(fold_case(mirror_bone_name(names[i])));
            if (j == index.end() || !xf::finite(pose[i]))
            {
                continue;
            }
            for (int a = 0; a < 2; ++a) // horizontal only
            {
                left[a] += pose[i].pos[a] - pose[j->second].pos[a];
            }
        }
        double len = std::sqrt(left[0] * left[0] + left[1] * left[1]);
        if (len < 1e-6)
        {
            left[0] = 0.0; // Unreal: X forward, Y right
            left[1] = -1.0;
            len = 1.0;
        }
        left[0] /= len;
        left[1] /= len;
        const double up[3] = {0.0, 0.0, 1.0};
        const double forward[3] = {-left[1], left[0], 0.0}; // up x left
        for (size_t i = 0; i < n; ++i)
        {
            if (!xf::finite(pose[i]))
            {
                continue;
            }
            const Side side = find_side(names[i]).side;
            const double sign = side == Side::Right ? -1.0 : 1.0;
            const double out[3] = {left[0] * sign, left[1] * sign, 0.0};
            BoneFrame f;
            f.up = closest_axis(pose[i].rot, up);
            f.forward = closest_axis(pose[i].rot, forward);
            f.out = closest_axis(pose[i].rot, out);
            map.frames[fold_case(names[i])] = f;
        }
    }

    BoneFrame frame_of(const BodyMap& map, std::string_view bone)
    {
        const auto it = map.frames.find(fold_case(bone));
        return it == map.frames.end() ? BoneFrame{} : it->second;
    }

    int long_axis_of(const BodyMap& map, std::string_view bone)
    {
        const auto it = map.long_axis.find(fold_case(bone));
        return it == map.long_axis.end() ? 0 : it->second;
    }

    std::vector<std::string> reach_bones(const GroupBones& gb, Reach reach)
    {
        if (reach == Reach::Bones || reach == Reach::Own || gb.chain.empty())
        {
            return reach == Reach::Below ? std::vector<std::string>{} : gb.bones;
        }
        if (reach == Reach::Chain)
        {
            return gb.chain;
        }
        std::vector<std::string> below;
        for (size_t i = 0; i < gb.chain.size(); ++i)
        {
            if (i < gb.chain_parent.size() && gb.chain_parent[i] >= 0)
            {
                below.push_back(gb.chain[i]);
            }
        }
        return below;
    }

    std::vector<std::string> scale_slider_bones(const ScaleSlider& s, const BodyMap& map)
    {
        std::vector<std::string> out;
        auto add = [&out](const std::string& b) {
            if (std::find(out.begin(), out.end(), b) == out.end())
            {
                out.push_back(b);
            }
        };
        for (const auto* list : {&s.groups, &s.helpers})
        {
            for (const char* title : *list)
            {
                for (const GroupBones& gb : map.groups)
                {
                    if (fold_case(gb.group.title) == fold_case(title))
                    {
                        for (const std::string& b : list == &s.groups ? reach_bones(gb, s.reach) : gb.bones)
                        {
                            add(b);
                        }
                    }
                }
            }
        }
        for (const SliderEffect& more : s.also)
        {
            for (const char* title : more.groups)
            {
                for (const GroupBones& gb : map.groups)
                {
                    if (fold_case(gb.group.title) == fold_case(title))
                    {
                        for (const std::string& b : gb.bones)
                        {
                            add(b);
                        }
                    }
                }
            }
        }
        for (const char* part : s.parts)
        {
            for (const PartBones& pb : map.parts)
            {
                if (pb.id == part)
                {
                    for (const std::string& b : pb.bones)
                    {
                        add(b);
                    }
                }
            }
        }
        return out;
    }

    bool scale_slider_covered(const ScaleSlider& s, const std::vector<std::string>& morphs)
    {
        if (s.morph_words.empty())
        {
            return false;
        }
        return std::any_of(morphs.begin(), morphs.end(),
                           [&](const std::string& m) { return part_covered_by_morph(m, s.morph_words, s.morph_dims); });
    }
} // namespace uuepbs
