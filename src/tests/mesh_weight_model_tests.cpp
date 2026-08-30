#include "mesh_weight_model.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "marrow/editor/project.hpp"

namespace weights = marrow::editor::mesh_weight_model;

namespace {

class TestSuite {
public:
    template <typename Function>
    void run(std::string name, Function&& function) {
        current_case_ = std::move(name);
        const int failures_before = failures_;
        std::forward<Function>(function)();
        if (failures_ == failures_before) {
            std::cout << "PASS: " << current_case_ << '\n';
        } else {
            std::cout << "FAIL: " << current_case_ << '\n';
        }
        ++case_count_;
    }

    void expect(bool condition, std::string_view message) {
        if (!condition) {
            ++failures_;
            std::cerr << current_case_ << ": " << message << '\n';
        }
    }

    int finish() const {
        if (failures_ == 0) {
            std::cout << "Mesh weight model: " << case_count_ << " cases passed\n";
            return 0;
        }
        std::cerr << "Mesh weight model: " << failures_ << " failure(s) across "
                  << case_count_ << " cases\n";
        return 1;
    }

private:
    std::string current_case_;
    int failures_{0};
    int case_count_{0};
};

using Influence = marrow::editor::MeshWeightInfluenceEdit;
using Vertex = marrow::editor::MeshWeightVertexEdit;

Vertex vertex(std::vector<Influence> influences) {
    Vertex result;
    result.influences = std::move(influences);
    return result;
}

/// Bit-exact structural equality. Identity comparisons in this suite never use
/// a tolerance: `mesh_weight_vertex_equal()`'s 1e-6 brush-jitter tolerance
/// answers a different question.
bool same_bits(double left, double right) {
    // Byte comparison, not `==`: a NaN never equals itself, and "byte-unchanged"
    // is the property the rejection cases are actually asserting.
    return std::memcmp(&left, &right, sizeof(double)) == 0;
}

bool identical(const Vertex& left, const Vertex& right) {
    if (left.influences.size() != right.influences.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.influences.size(); ++index) {
        const Influence& lhs = left.influences[index];
        const Influence& rhs = right.influences[index];
        if (lhs.bone_name != rhs.bone_name || !same_bits(lhs.x, rhs.x) ||
            !same_bits(lhs.y, rhs.y) || !same_bits(lhs.weight, rhs.weight)) {
            return false;
        }
    }
    return true;
}

double left_to_right_sum(const Vertex& subject) {
    double total = 0.0;
    for (const Influence& influence : subject.influences) {
        total += influence.weight;
    }
    return total;
}

std::string bone_order(const Vertex& subject) {
    std::string names;
    for (const Influence& influence : subject.influences) {
        if (!names.empty()) {
            names += ',';
        }
        names += influence.bone_name;
    }
    return names;
}

const marrow::editor::ProjectLoadResult& fixture_project(TestSuite& suite) {
    static const marrow::editor::ProjectLoadResult loaded =
        marrow::editor::load_project("assets/fixtures/player_idle.marrow");
    static bool reported = false;
    if (!reported) {
        reported = true;
        suite.expect(static_cast<bool>(loaded), "fixture project must load");
    }
    return loaded;
}

const marrow::runtime::SkeletonData& fixture_skeleton(TestSuite& suite) {
    // player_idle's bone order is the skeleton order every tie-break below
    // relies on: root=0, spine=1, arm_l=2, ik_upper=3, ik_lower=4, ik_tip=5.
    return *fixture_project(suite).skeleton_data;
}

void expect_rejected(
    TestSuite& suite,
    const marrow::runtime::SkeletonData& skeleton,
    Vertex subject,
    std::string_view expected_message,
    std::string_view label) {
    const Vertex before = subject;
    const std::string error = weights::canonicalize_mesh_weight_vertex(skeleton, &subject);
    suite.expect(error == expected_message, std::string(label) + ": message was \"" + error + "\"");
    suite.expect(identical(subject, before), std::string(label) + ": rejection must leave the vertex byte-unchanged");
}

/// Canonicalization is a fixed point of itself, bit-exactly. Asserted on every
/// accepted case rather than on one hand-picked witness.
void expect_idempotent(
    TestSuite& suite,
    const marrow::runtime::SkeletonData& skeleton,
    const Vertex& canonical,
    std::string_view label) {
    Vertex again = canonical;
    const std::string error = weights::canonicalize_mesh_weight_vertex(skeleton, &again);
    suite.expect(error.empty(), std::string(label) + ": re-canonicalizing canonical input must succeed");
    suite.expect(
        identical(again, canonical),
        std::string(label) + ": canonicalize(canonicalize(v)) must be bit-identical to canonicalize(v)");
}

Vertex canonicalize_or_report(
    TestSuite& suite,
    const marrow::runtime::SkeletonData& skeleton,
    Vertex subject,
    std::string_view label) {
    const std::string error = weights::canonicalize_mesh_weight_vertex(skeleton, &subject);
    suite.expect(error.empty(), std::string(label) + ": unexpected rejection \"" + error + "\"");
    if (error.empty()) {
        expect_idempotent(suite, skeleton, subject, label);
    }
    return subject;
}

void test_drops_non_positive(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    const Vertex result = canonicalize_or_report(
        suite,
        skeleton,
        vertex({
            {"spine", 1.0, 2.0, 0.5},
            {"arm_l", 3.0, 4.0, 0.0},
            {"root", 5.0, 6.0, -0.5},
            {"ik_upper", 7.0, 8.0, 1e-9},
        }),
        "drop non-positive");
    suite.expect(
        result.influences.size() == 1U && result.influences[0].bone_name == "spine",
        "zero, negative, and sub-epsilon influences must all be removed");
    suite.expect(
        result.influences.size() == 1U && result.influences[0].weight == 1.0,
        "the sole survivor must normalize to exactly 1.0");
    suite.expect(
        result.influences.size() == 1U && result.influences[0].x == 1.0 &&
            result.influences[0].y == 2.0,
        "dropping must not disturb the survivor's bind offset");
}

void test_merges_duplicate_bones(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    const Vertex halves = canonicalize_or_report(
        suite,
        skeleton,
        vertex({{"spine", 10.0, 0.0, 0.5}, {"spine", 20.0, 0.0, 0.5}}),
        "merge equal halves");
    suite.expect(
        halves.influences.size() == 1U && halves.influences[0].bone_name == "spine" &&
            halves.influences[0].weight == 1.0,
        "duplicate bones must merge into one influence carrying the summed weight");
    suite.expect(
        halves.influences.size() == 1U && halves.influences[0].x == 15.0 &&
            halves.influences[0].y == 0.0,
        "the merged bind offset must be the weight-weighted mean");

    const Vertex uneven = canonicalize_or_report(
        suite,
        skeleton,
        vertex({{"spine", 10.0, 0.0, 0.25}, {"spine", 20.0, 0.0, 0.75}}),
        "merge uneven halves");
    suite.expect(
        uneven.influences.size() == 1U && uneven.influences[0].x == 17.5,
        "an uneven merge must weight the mean by each part's weight");
}

void test_total_order_sort(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    const Vertex ascending = canonicalize_or_report(
        suite,
        skeleton,
        vertex({
            {"ik_upper", 0.0, 0.0, 0.1},
            {"arm_l", 0.0, 0.0, 0.2},
            {"spine", 0.0, 0.0, 0.3},
            {"root", 0.0, 0.0, 0.4},
        }),
        "sort descending");
    suite.expect(
        bone_order(ascending) == "root,spine,arm_l,ik_upper",
        "influences must come back sorted by descending weight");

    // Three entries: the shipped shell normalizer sorted only above four.
    const Vertex three = canonicalize_or_report(
        suite,
        skeleton,
        vertex({
            {"root", 0.0, 0.0, 1.0},
            {"spine", 0.0, 0.0, 2.0},
            {"arm_l", 0.0, 0.0, 3.0},
        }),
        "sort a short list");
    suite.expect(
        bone_order(three) == "arm_l,spine,root",
        "a list of four or fewer must be sorted too, not left in insertion order");
}

void test_tie_break_is_skeleton_order(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    // arm_l is skeleton index 2, spine is 1: equal weights must order spine first.
    const Vertex forward = canonicalize_or_report(
        suite,
        skeleton,
        vertex({{"arm_l", 1.0, 0.0, 0.3}, {"spine", 2.0, 0.0, 0.3}}),
        "tie-break forward");
    const Vertex reversed = canonicalize_or_report(
        suite,
        skeleton,
        vertex({{"spine", 2.0, 0.0, 0.3}, {"arm_l", 1.0, 0.0, 0.3}}),
        "tie-break reversed");
    suite.expect(
        bone_order(forward) == "spine,arm_l",
        "equal weights must break the tie on ascending skeleton index, not insertion order");
    suite.expect(
        identical(forward, reversed),
        "the same influence set supplied in either order must produce a bit-identical result");
}

void test_sort_runs_on_normalized_weights(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    // arm_l (skeleton index 2) carries a weight exactly one ULP above spine's
    // (index 1), so the pre-normalization sort puts arm_l first. Dividing by
    // the summed weight rounds both quotients onto the *same* double, which
    // makes them a tie the comparator must break on skeleton index -- so the
    // canonical order is spine, arm_l. Without a second sort on the normalized
    // weights the output keeps arm_l first, violating its own comparator, and
    // a second canonicalization reorders it.
    const Vertex result = canonicalize_or_report(
        suite,
        skeleton,
        vertex({
            {"arm_l", 0.0, 0.0, 0x1.fd37fbdb0742cp+1},
            {"spine", 0.0, 0.0, 0x1.fd37fbdb0742bp+1},
            {"root", 0.0, 0.0, 0x1.0a54c576887dcp+1},
        }),
        "sort after normalizing");
    suite.expect(
        result.influences.size() == 3U &&
            result.influences[0].weight == result.influences[1].weight,
        "the two near-tied weights must collapse onto one value when normalized");
    suite.expect(
        bone_order(result) == "spine,arm_l,root",
        "normalization can create a tie, so the sort must run again on the normalized weights");
}

void test_caps_at_four(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    const Vertex six = canonicalize_or_report(
        suite,
        skeleton,
        vertex({
            {"root", 0.0, 0.0, 0.1},
            {"spine", 0.0, 0.0, 0.6},
            {"arm_l", 0.0, 0.0, 0.5},
            {"ik_upper", 0.0, 0.0, 0.4},
            {"ik_lower", 0.0, 0.0, 0.3},
            {"ik_tip", 0.0, 0.0, 0.2},
        }),
        "cap at four");
    suite.expect(
        bone_order(six) == "spine,arm_l,ik_upper,ik_lower",
        "capping must keep the four largest influences");

    const Vertex shuffled = canonicalize_or_report(
        suite,
        skeleton,
        vertex({
            {"ik_tip", 0.0, 0.0, 0.2},
            {"ik_lower", 0.0, 0.0, 0.3},
            {"root", 0.0, 0.0, 0.1},
            {"ik_upper", 0.0, 0.0, 0.4},
            {"spine", 0.0, 0.0, 0.6},
            {"arm_l", 0.0, 0.0, 0.5},
        }),
        "cap independent of input order");
    suite.expect(
        identical(six, shuffled),
        "which four survive must depend only on the input set, never on its order");

    // Five entries that merge down to four must be kept, not truncated to three.
    const Vertex merged = canonicalize_or_report(
        suite,
        skeleton,
        vertex({
            {"root", 0.0, 0.0, 0.4},
            {"spine", 0.0, 0.0, 0.3},
            {"arm_l", 0.0, 0.0, 0.2},
            {"ik_upper", 0.0, 0.0, 0.1},
            {"root", 0.0, 0.0, 0.4},
        }),
        "cap after merge");
    suite.expect(
        merged.influences.size() == 4U,
        "merging must happen before capping so a five-entry list that collapses to four keeps four");
}

void test_normalizes_and_is_idempotent(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    // The fixture's own vertex 1: spine 0.6, arm_l 0.2. This is the case the
    // design's "skip only when the sum is exactly 1.0" rule cannot make
    // idempotent -- 0.6/0.8 + 0.2/0.8 is 0.9999999999999999, not 1.0.
    const Vertex fixture_like = canonicalize_or_report(
        suite,
        skeleton,
        vertex({{"spine", 64.0, -80.0, 0.6}, {"arm_l", 94.0, -90.0, 0.2}}),
        "normalize the fixture's own weights");
    suite.expect(
        fixture_like.influences.size() == 2U &&
            fixture_like.influences[0].weight == 0.6 / 0.8 &&
            fixture_like.influences[1].weight == 0.2 / 0.8,
        "normalization must divide by the summed weight");
    suite.expect(
        std::abs(left_to_right_sum(fixture_like) - 1.0) <=
            weights::kMeshWeightSumTolerance,
        "a canonical vertex must sum to 1.0 within the documented tolerance");

    // Weights that are not exactly representable.
    const Vertex thirds = canonicalize_or_report(
        suite,
        skeleton,
        vertex({
            {"root", 0.0, 0.0, 1.0},
            {"spine", 0.0, 0.0, 1.0},
            {"arm_l", 0.0, 0.0, 1.0},
        }),
        "normalize thirds");
    suite.expect(
        thirds.influences.size() == 3U && thirds.influences[0].weight == 1.0 / 3.0,
        "three equal influences must each normalize to one third");

    // An input that already sums to 1.0 must be left completely alone.
    const Vertex already = canonicalize_or_report(
        suite,
        skeleton,
        vertex({{"spine", 1.0, 2.0, 0.75}, {"arm_l", 3.0, 4.0, 0.25}}),
        "already normalized");
    suite.expect(
        already.influences[0].weight == 0.75 && already.influences[1].weight == 0.25,
        "an input already summing to 1.0 must not be perturbed by a division");
}

void test_rejects_non_finite(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    const double nan_value = std::numeric_limits<double>::quiet_NaN();
    const double inf_value = std::numeric_limits<double>::infinity();

    // NaN <= 1e-6 is false, so the shipped normalizers let a NaN through both
    // guards and then divide by it, leaving every influence on the vertex NaN.
    expect_rejected(
        suite,
        skeleton,
        vertex({{"arm_l", 0.0, 0.0, nan_value}, {"spine", 0.0, 0.0, 1.0}}),
        "Bone 'arm_l' has a non-finite weight.",
        "NaN weight");
    expect_rejected(
        suite,
        skeleton,
        vertex({{"arm_l", 0.0, 0.0, inf_value}, {"spine", 0.0, 0.0, 1.0}}),
        "Bone 'arm_l' has a non-finite weight.",
        "positive infinity weight");
    expect_rejected(
        suite,
        skeleton,
        vertex({{"arm_l", 0.0, 0.0, -inf_value}, {"spine", 0.0, 0.0, 1.0}}),
        "Bone 'arm_l' has a non-finite weight.",
        "negative infinity weight");
    expect_rejected(
        suite,
        skeleton,
        vertex({{"spine", nan_value, 0.0, 1.0}}),
        "Bone 'spine' has a non-finite bind offset.",
        "NaN bind x");
    expect_rejected(
        suite,
        skeleton,
        vertex({{"spine", 0.0, inf_value, 1.0}}),
        "Bone 'spine' has a non-finite bind offset.",
        "infinite bind y");
}

void test_rejects_unknown_and_empty(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    expect_rejected(
        suite,
        skeleton,
        vertex({{"spine", 0.0, 0.0, 0.5}, {"<bone 17>", 0.0, 0.0, 0.5}}),
        "Bone not found: <bone 17>",
        "unknown bone");
    expect_rejected(
        suite,
        skeleton,
        vertex({{"", 0.0, 0.0, 1.0}}),
        "A weighted influence requires a non-empty bone name.",
        "empty bone name");
}

void test_rejects_empty_result(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    expect_rejected(
        suite,
        skeleton,
        vertex({{"spine", 0.0, 0.0, 0.0}, {"arm_l", 0.0, 0.0, -1.0}}),
        "A weighted vertex must keep at least one positive influence.",
        "all-zero list");
    expect_rejected(
        suite,
        skeleton,
        vertex({}),
        "A weighted vertex must keep at least one positive influence.",
        "empty influence list");
    // Duplicates that cancel each other out are an empty result too.
    expect_rejected(
        suite,
        skeleton,
        vertex({{"spine", 0.0, 0.0, 0.5}, {"spine", 0.0, 0.0, -0.5}}),
        "A weighted vertex must keep at least one positive influence.",
        "duplicates that cancel");
}

void test_canonical_output_satisfies_save_validation(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    // The canonicalizer's post-condition is strictly stronger than every
    // precondition validate_project_for_save() imposes on a weight vertex.
    const Vertex result = canonicalize_or_report(
        suite,
        skeleton,
        vertex({
            {"spine", 0.0, 0.0, 0.5},
            {"spine", 1.0, 0.0, 0.5},
            {"arm_l", 0.0, 0.0, 0.0},
            {"root", 0.0, 0.0, 0.25},
        }),
        "save-validation post-condition");
    suite.expect(!result.influences.empty(), "a canonical vertex keeps at least one influence");
    suite.expect(
        result.influences.size() <= weights::kMaxMeshWeightInfluences,
        "a canonical vertex never exceeds the influence cap");
    std::vector<std::string> seen;
    bool all_positive = true;
    bool no_duplicates = true;
    bool named = true;
    for (const Influence& influence : result.influences) {
        all_positive = all_positive && influence.weight > 0.0;
        named = named && !influence.bone_name.empty();
        for (const std::string& previous : seen) {
            if (previous == influence.bone_name) {
                no_duplicates = false;
            }
        }
        seen.push_back(influence.bone_name);
    }
    suite.expect(all_positive, "every canonical weight is strictly positive");
    suite.expect(no_duplicates, "a canonical vertex never repeats a bone");
    suite.expect(named, "every canonical influence carries a bone name");
    suite.expect(left_to_right_sum(result) > 0.0, "a canonical vertex sums to a positive weight");
}


marrow::runtime::AttachmentVertex skin_point(
    const marrow::runtime::BoneWorldTransform& transform,
    double x,
    double y) {
    return marrow::runtime::AttachmentVertex{
        (x * static_cast<double>(transform.a)) + (y * static_cast<double>(transform.b)) +
            static_cast<double>(transform.world_x),
        (x * static_cast<double>(transform.c)) + (y * static_cast<double>(transform.d)) +
            static_cast<double>(transform.world_y)};
}

void test_setup_pose_transforms(TestSuite& suite) {
    const auto& loaded = fixture_project(suite);
    const auto transforms = weights::setup_pose_bone_world_transforms(loaded.skeleton_data);
    suite.expect(
        transforms.size() == loaded.skeleton_data->bones().size(),
        "setup-pose transforms must cover every bone");

    // A live skeleton posed away from setup must be unaffected: the scratch
    // instance the primitive builds never touches it.
    marrow::runtime::Skeleton live(loaded.skeleton_data);
    live.set_to_setup_pose();
    if (!live.bone_poses().empty()) {
        live.bone_poses()[1].local_pose.rotation += 37.5;
    }
    live.update_world_transforms();
    const auto posed_before = live.bone_world_transforms()[1];
    const auto again = weights::setup_pose_bone_world_transforms(loaded.skeleton_data);
    const auto posed_after = live.bone_world_transforms()[1];
    suite.expect(
        posed_before.a == posed_after.a && posed_before.world_x == posed_after.world_x &&
            posed_before.world_y == posed_after.world_y,
        "building setup transforms must not disturb a live posed skeleton");
    suite.expect(
        again.size() == transforms.size() && !again.empty() &&
            again[1].a == transforms[1].a && again[1].world_x == transforms[1].world_x,
        "repeat calls must return bit-identical transforms");
}

void test_rebind_makes_offsets_consistent(TestSuite& suite) {
    const auto& loaded = fixture_project(suite);
    const auto& skeleton = *loaded.skeleton_data;
    const auto setup = weights::setup_pose_bone_world_transforms(loaded.skeleton_data);
    const auto spine = skeleton.find_bone_index("spine");
    const auto arm = skeleton.find_bone_index("arm_l");
    suite.expect(spine.has_value() && arm.has_value(), "fixture bones must resolve");
    if (!spine.has_value() || !arm.has_value()) {
        return;
    }

    // A single influence must round-trip: the vertex's setup-world position is
    // exactly that one bone's transform applied to its own offset.
    Vertex single = vertex({{"spine", 64.0, -80.0, 1.0}});
    const Vertex single_before = single;
    suite.expect(
        weights::rebind_mesh_weight_vertex(skeleton, setup, &single).empty(),
        "a single-influence rebind must succeed");
    suite.expect(
        std::abs(single.influences[0].x - single_before.influences[0].x) <= 1e-9 &&
            std::abs(single.influences[0].y - single_before.influences[0].y) <= 1e-9,
        "a single-influence rebind must round-trip its bind offset within 1e-9");

    // Two influences whose offsets disagree about where the vertex is -- the
    // shape the paint path produced when it inverted the *current* pose for a
    // newly added bone. After rebind both offsets must name one setup-world
    // point.
    Vertex inconsistent = vertex({
        {"spine", 64.0, -80.0, 0.75},
        {"arm_l", 300.0, 250.0, 0.25},
    });
    const double weight_before_a = inconsistent.influences[0].weight;
    const double weight_before_b = inconsistent.influences[1].weight;
    const std::string error =
        weights::rebind_mesh_weight_vertex(skeleton, setup, &inconsistent);
    suite.expect(error.empty(), "a two-influence rebind must succeed: " + error);
    if (!error.empty()) {
        return;
    }
    const auto point_a = skin_point(
        setup[*spine], inconsistent.influences[0].x, inconsistent.influences[0].y);
    const auto point_b = skin_point(
        setup[*arm], inconsistent.influences[1].x, inconsistent.influences[1].y);
    suite.expect(
        std::abs(point_a.x - point_b.x) <= 1e-9 && std::abs(point_a.y - point_b.y) <= 1e-9,
        "after rebind every influence's offset must name the same setup-world point");
    suite.expect(
        inconsistent.influences[0].weight == weight_before_a &&
            inconsistent.influences[1].weight == weight_before_b,
        "rebind must not change any weight");
    suite.expect(
        inconsistent.influences[1].x != 300.0 || inconsistent.influences[1].y != 250.0,
        "rebind must actually move an inconsistent offset");

    // Determinism versus idempotence. BoneWorldTransform packs to six float32s
    // (skeleton.hpp:1009) while bind offsets are double (project.hpp:196-197),
    // so S(S^-1(V)) does not reproduce V bit-for-bit: the narrowing enters
    // through the transform, not through the weight. Repeat *runs* are
    // therefore bit-identical, while a second *application* is only stable to
    // 1e-9. Asserting bit-exact idempotence here would assert something false.
    Vertex repeat = vertex({{"spine", 64.0, -80.0, 0.75}, {"arm_l", 300.0, 250.0, 0.25}});
    suite.expect(
        weights::rebind_mesh_weight_vertex(skeleton, setup, &repeat).empty(),
        "the repeat rebind must succeed");
    suite.expect(
        identical(repeat, inconsistent),
        "the same input rebound again from scratch must be bit-identical");

    Vertex second = inconsistent;
    suite.expect(
        weights::rebind_mesh_weight_vertex(skeleton, setup, &second).empty(),
        "a second rebind application must succeed");
    bool stable = true;
    bool bit_exact = true;
    for (std::size_t index = 0; index < second.influences.size(); ++index) {
        stable = stable &&
            std::abs(second.influences[index].x - inconsistent.influences[index].x) <= 1e-9 &&
            std::abs(second.influences[index].y - inconsistent.influences[index].y) <= 1e-9;
        bit_exact = bit_exact &&
            same_bits(second.influences[index].x, inconsistent.influences[index].x) &&
            same_bits(second.influences[index].y, inconsistent.influences[index].y);
    }
    suite.expect(stable, "a second rebind application must be stable to 1e-9");
    std::cout << "  note: rebind is deterministic (repeat runs bit-identical) but not\n"
                 "        bit-exactly idempotent -- BoneWorldTransform is float32 while\n"
                 "        bind offsets are double, so S(S^-1(V)) != V bit-for-bit.\n"
                 "        Second application bit-exact on this fixture: "
              << (bit_exact ? "yes" : "no") << " (not asserted either way).\n";
}

void test_rebind_rejects_atomically(TestSuite& suite) {
    const auto& loaded = fixture_project(suite);
    const auto& skeleton = *loaded.skeleton_data;
    auto setup = weights::setup_pose_bone_world_transforms(loaded.skeleton_data);
    const auto arm = skeleton.find_bone_index("arm_l");
    suite.expect(arm.has_value(), "arm_l must resolve");
    if (!arm.has_value()) {
        return;
    }

    // |det| == 0 is inside inverse_transform_point_safe's 1e-8 threshold.
    setup[*arm].a = 0.0f;
    setup[*arm].b = 0.0f;
    setup[*arm].c = 0.0f;
    setup[*arm].d = 0.0f;
    Vertex subject = vertex({{"spine", 64.0, -80.0, 0.75}, {"arm_l", 94.0, -90.0, 0.25}});
    const Vertex before = subject;
    const std::string error =
        weights::rebind_mesh_weight_vertex(skeleton, setup, &subject);
    suite.expect(
        error == "Bone 'arm_l' has a singular setup transform and cannot be rebound.",
        "a singular setup transform must reject by name: was \"" + error + "\"");
    suite.expect(
        identical(subject, before),
        "a rejected rebind must leave the vertex byte-unchanged");

    Vertex unknown = vertex({{"nonexistent_bone", 0.0, 0.0, 1.0}});
    const Vertex unknown_before = unknown;
    const auto clean_setup = weights::setup_pose_bone_world_transforms(loaded.skeleton_data);
    suite.expect(
        weights::rebind_mesh_weight_vertex(skeleton, clean_setup, &unknown) ==
            "Bone not found: nonexistent_bone",
        "an unresolvable bone must reject");
    suite.expect(
        identical(unknown, unknown_before),
        "an unresolvable-bone rejection must leave the vertex byte-unchanged");
}

} // namespace

int main() {
    TestSuite suite;
    suite.run("drops non-positive influences", [&]() { test_drops_non_positive(suite); });
    suite.run("merges duplicate bones", [&]() { test_merges_duplicate_bones(suite); });
    suite.run("sorts with a total order", [&]() { test_total_order_sort(suite); });
    suite.run("breaks ties on skeleton order", [&]() { test_tie_break_is_skeleton_order(suite); });
    suite.run("sorts again after normalizing", [&]() {
        test_sort_runs_on_normalized_weights(suite);
    });
    suite.run("caps at four influences", [&]() { test_caps_at_four(suite); });
    suite.run("normalizes idempotently", [&]() { test_normalizes_and_is_idempotent(suite); });
    suite.run("rejects non-finite input", [&]() { test_rejects_non_finite(suite); });
    suite.run("rejects unknown and empty bones", [&]() { test_rejects_unknown_and_empty(suite); });
    suite.run("rejects an empty result", [&]() { test_rejects_empty_result(suite); });
    suite.run("output satisfies save validation", [&]() {
        test_canonical_output_satisfies_save_validation(suite);
    });
    suite.run("builds setup-pose transforms", [&]() { test_setup_pose_transforms(suite); });
    suite.run("rebind makes offsets consistent", [&]() {
        test_rebind_makes_offsets_consistent(suite);
    });
    suite.run("rebind rejects atomically", [&]() { test_rebind_rejects_atomically(suite); });
    return suite.finish();
}
