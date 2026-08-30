#include "mesh_weight_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iomanip>
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

// ── MAR-176: deterministic automatic weight generation ─────────────────────

using Segment = weights::BoneSetupSegment;

/// A synthetic skeleton carrying only bones. Every other SkeletonData member is
/// empty: the generator reads bone names and parent indices and nothing else.
marrow::runtime::SkeletonData make_bone_skeleton(
    const std::vector<std::pair<std::string, long long>>& bones) {
    std::vector<marrow::runtime::BoneData> data;
    data.reserve(bones.size());
    for (const auto& [name, parent] : bones) {
        marrow::runtime::BoneData bone;
        bone.name = name;
        if (parent >= 0) {
            bone.parent_index = static_cast<std::size_t>(parent);
        }
        data.push_back(std::move(bone));
    }
    return marrow::runtime::SkeletonData(
        marrow::runtime::SkeletonInfo{"synthetic", 0.0, 0.0},
        std::move(data),
        {}, {}, {}, {}, {}, {}, {}, {},
        0.0,
        {});
}

marrow::runtime::BoneWorldTransform world_at(double x, double y) {
    marrow::runtime::BoneWorldTransform transform;
    transform.world_x = static_cast<float>(x);
    transform.world_y = static_cast<float>(y);
    return transform;
}

/// The fixture's candidate indices, by name, so a test never hard-codes an
/// index the fixture could renumber.
std::vector<std::size_t> candidates_by_name(
    TestSuite& suite,
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& names) {
    std::vector<std::size_t> indices;
    for (const std::string& name : names) {
        const auto index = skeleton.find_bone_index(name);
        suite.expect(index.has_value(), "candidate bone '" + name + "' must resolve");
        indices.push_back(index.value_or(0U));
    }
    return indices;
}

struct GenerateHarness {
    std::vector<marrow::runtime::BoneWorldTransform> transforms;
    std::vector<Segment> segments;
};

GenerateHarness fixture_harness(TestSuite& suite) {
    const auto& loaded = fixture_project(suite);
    GenerateHarness harness;
    harness.transforms = weights::setup_pose_bone_world_transforms(loaded.skeleton_data);
    harness.segments = weights::bone_setup_segments(*loaded.skeleton_data, harness.transforms);
    return harness;
}

/// The fixture's four weighted vertices, exactly as `player_idle.marrow`
/// authors them. Kept literal so a test failure names the input that produced
/// it rather than sending the reader to the fixture.
std::vector<Vertex> fixture_weight_vertices() {
    return {
        vertex({{"spine", -64.0, -80.0, 1.0}}),
        vertex({{"spine", 64.0, -80.0, 0.6}, {"arm_l", 94.0, -90.0, 0.2}}),
        vertex({{"spine", 64.0, 80.0, 0.2}, {"arm_l", 94.0, 70.0, 0.6}}),
        vertex({{"spine", -64.0, 80.0, 0.6}, {"arm_l", -34.0, 70.0, 0.2}}),
    };
}

/// Generates, then asserts the two determinism properties every accepted case
/// must have: a repeat run is bit-identical, and so is a run whose candidate
/// list was supplied in the reverse order.
Vertex generate_or_report(
    TestSuite& suite,
    const marrow::runtime::SkeletonData& skeleton,
    const GenerateHarness& harness,
    const std::vector<std::size_t>& candidates,
    Vertex subject,
    std::string_view label) {
    Vertex first = subject;
    const std::string error = weights::generate_mesh_weight_vertex(
        skeleton, harness.transforms, harness.segments, candidates, &first);
    suite.expect(error.empty(), std::string(label) + ": unexpected rejection \"" + error + "\"");
    if (!error.empty()) {
        return first;
    }

    Vertex again = subject;
    weights::generate_mesh_weight_vertex(
        skeleton, harness.transforms, harness.segments, candidates, &again);
    suite.expect(
        identical(again, first),
        std::string(label) + ": two runs from the same input must be bit-identical");

    std::vector<std::size_t> reversed(candidates.rbegin(), candidates.rend());
    Vertex flipped = subject;
    weights::generate_mesh_weight_vertex(
        skeleton, harness.transforms, harness.segments, reversed, &flipped);
    suite.expect(
        identical(flipped, first),
        std::string(label) + ": the result must not depend on the caller's candidate order");
    return first;
}

void expect_generate_rejected(
    TestSuite& suite,
    const marrow::runtime::SkeletonData& skeleton,
    const GenerateHarness& harness,
    const std::vector<std::size_t>& candidates,
    Vertex subject,
    std::string_view expected_message,
    std::string_view label) {
    const Vertex before = subject;
    const std::string error = weights::generate_mesh_weight_vertex(
        skeleton, harness.transforms, harness.segments, candidates, &subject);
    suite.expect(
        error == expected_message,
        std::string(label) + ": message was \"" + error + "\"");
    suite.expect(
        identical(subject, before),
        std::string(label) + ": rejection must leave the vertex byte-unchanged");
}

void test_bone_setup_segments(TestSuite& suite) {
    // `late` forward-references `tail` at index 4, which `SkeletonData` accepts
    // -- it topologically sorts, and only rejects a parent index outside
    // `bones()`. Supplying four transforms for five bones is therefore the only
    // way an unresolvable parent is reachable at all, and it is exactly what the
    // guard defends against.
    const auto skeleton = make_bone_skeleton(
        {{"root", -1}, {"spine", 0}, {"stacked", 1}, {"late", 4}, {"tail", -1}});
    const std::vector<marrow::runtime::BoneWorldTransform> transforms{
        world_at(0.0, 0.0), world_at(0.0, 50.0), world_at(0.0, 50.0), world_at(7.0, 11.0)};
    const auto segments = weights::bone_setup_segments(skeleton, transforms);
    suite.expect(
        segments.size() == 4U,
        "one segment per bone that has a setup transform, never more");
    if (segments.size() != 4U) {
        return;
    }

    suite.expect(
        segments[0].start_x == 0.0 && segments[0].start_y == 0.0 &&
            segments[0].end_x == 0.0 && segments[0].end_y == 0.0,
        "a root bone degenerates to the point at its own origin");
    suite.expect(
        segments[1].start_x == 0.0 && segments[1].start_y == 0.0 &&
            segments[1].end_x == 0.0 && segments[1].end_y == 50.0,
        "a child bone runs from its parent's world origin to its own");
    suite.expect(
        segments[2].start_x == segments[2].end_x && segments[2].start_y == segments[2].end_y,
        "a bone sitting on its parent yields a zero-length segment, not a special case");
    suite.expect(
        segments[3].start_x == 7.0 && segments[3].start_y == 11.0 &&
            segments[3].end_x == 7.0 && segments[3].end_y == 11.0,
        "a parent outside the setup transforms degenerates to a point rather than reading "
        "out of bounds");
}

void test_point_segment_distance(TestSuite& suite) {
    // Exact small integers, so every intermediate is representable and the
    // assertions can use `==` rather than a tolerance.
    const Segment vertical{0.0, 0.0, 0.0, 10.0};
    suite.expect(
        weights::point_segment_distance_squared(3.0, 4.0, vertical) == 9.0,
        "an interior projection measures the perpendicular distance");
    suite.expect(
        weights::point_segment_distance_squared(3.0, -4.0, vertical) == 25.0,
        "a projection before the start clamps to the start");
    suite.expect(
        weights::point_segment_distance_squared(3.0, 14.0, vertical) == 25.0,
        "a projection past the end clamps to the end");
    suite.expect(
        weights::point_segment_distance_squared(0.0, 5.0, vertical) == 0.0,
        "a point on the segment is exactly zero away from it");

    const Segment degenerate{6.0, 8.0, 6.0, 8.0};
    suite.expect(
        weights::point_segment_distance_squared(3.0, 4.0, degenerate) == 25.0,
        "a zero-length segment measures the distance to its point");
    suite.expect(
        weights::point_segment_distance_squared(6.0, 8.0, degenerate) == 0.0,
        "a point on a zero-length segment is exactly zero away and never divides by zero");
}

void test_setup_world_position_extraction(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    const GenerateHarness harness = fixture_harness(suite);

    // Gate C: the runtime composes the setup pose in float32, so these origins
    // are NOT the exact integers the fixture authors. Printed so every tolerance
    // below is visible in the log rather than inferred.
    std::cout << std::setprecision(17)
              << "  MAR-176 measured setup-pose origins (float32-composed, not exact):\n";
    for (const char* name : {"root", "spine", "arm_l", "pivot"}) {
        const auto index = skeleton.find_bone_index(name);
        if (!index.has_value() || *index >= harness.transforms.size()) {
            continue;
        }
        std::cout << "    " << name << " = ("
                  << static_cast<double>(harness.transforms[*index].world_x) << ", "
                  << static_cast<double>(harness.transforms[*index].world_y) << ")\n";
    }

    const std::vector<Vertex> fixture = fixture_weight_vertices();
    const double expected_x[4] = {
        -63.9999951917473, 64.00000607987091, 63.99998939009735, -64.00000965622858};
    const double expected_y[4] = {-30.0, -30.0, 130.0, 130.0};
    for (std::size_t index = 0; index < fixture.size(); ++index) {
        double world_x = 0.0;
        double world_y = 0.0;
        const std::string error = weights::setup_world_position_of_weight_vertex(
            skeleton, harness.transforms, fixture[index], &world_x, &world_y);
        suite.expect(error.empty(), "extraction must accept fixture vertex " + std::to_string(index));
        suite.expect(
            world_x == expected_x[index] && world_y == expected_y[index],
            "fixture vertex " + std::to_string(index) + " setup-world position was (" +
                std::to_string(world_x) + ", " + std::to_string(world_y) + ")");
    }

    // The extracted step is what rebind uses, so rebinding a vertex must land
    // every influence on exactly that point.
    Vertex rebound = fixture[1];
    const std::string rebind_error =
        weights::rebind_mesh_weight_vertex(skeleton, harness.transforms, &rebound);
    suite.expect(rebind_error.empty(), "rebind must still accept the fixture vertex");
    double rebound_x = 0.0;
    double rebound_y = 0.0;
    weights::setup_world_position_of_weight_vertex(
        skeleton, harness.transforms, rebound, &rebound_x, &rebound_y);
    suite.expect(
        std::abs(rebound_x - expected_x[1]) < 1e-9 && std::abs(rebound_y - expected_y[1]) < 1e-9,
        "the extracted derivation must agree with the one rebind performs internally");
}

void test_generate_fixture_table(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    const GenerateHarness harness = fixture_harness(suite);
    const auto candidates = candidates_by_name(suite, skeleton, {"spine", "arm_l"});
    const std::vector<Vertex> fixture = fixture_weight_vertices();

    // These are the values THIS BINARY produces, and they are asserted bit
    // exactly because determinism within one binary is what MAR-176 guarantees.
    // Two separate effects move them off the design document's table:
    //
    //  1. The setup pose is composed in float32 and is NOT the idealised
    //     integer origins the document assumed -- `spine` sits at
    //     x = -2.1855694285477512e-06, not 0, and `arm_l` at
    //     x = -30.000001907348633, not -30. That shifts the eighth significant
    //     digit.
    //  2. Floating-point contraction. Marrow sets no `-ffp-contract`, so the
    //     arm64 default fuses the multiply-adds in
    //     `point_segment_distance_squared()` into `fma`. Verified directly:
    //     compiling that expression at `-ffp-contract=on` yields
    //     `d2 = 10496.001057976433` for vertex 1's `arm_l` and at `=off` yields
    //     `10496.001057976431`, a 1 ULP difference that propagates to 1 ULP on
    //     the smaller weight of the pair.
    //
    // Effect 2 is exactly why cross-compiler and cross-architecture identity is
    // NOT claimed. Both contraction-immune signals -- vertex 2's exact 0.5/0.5
    // and the mutual bit-identity of the three-way tie -- are asserted in their
    // own cases below and hold under either setting.
    struct Expected {
        const char* first_bone;
        double first_weight;
        const char* second_bone;
        double second_weight;
    };
    const Expected expected[4] = {
        {"spine", 0.6494527252054849, "arm_l", 0.35054727479451514},
        {"spine", 0.6775109613949678, "arm_l", 0.32248903860503214},
        {"spine", 0.5, "arm_l", 0.5},
        {"arm_l", 0.63412276557114722, "spine", 0.36587723442885278},
    };

    // Generating with {spine, arm_l} must reproduce the geometry the fixture
    // already authors: every generated bind offset is the offset the fixture
    // carries, and the one offset the fixture lacks -- arm_l on vertex 0 -- is
    // the (-34, -90) MAR-175 recorded as "what setup requires" when it fixed the
    // paint-pose defect. The tolerance is 1e-5, set from Gate C's measurement
    // that the float32 setup pose carries up to 3.9e-6 of error here; AGENTS.md
    // describes that error as "~1e-6", which would not have covered it.
    const auto authored_offset = [](std::size_t vertex_index, const std::string& bone)
        -> std::pair<double, double> {
        if (bone == "spine") {
            const double x[4] = {-64.0, 64.0, 64.0, -64.0};
            const double y[4] = {-80.0, -80.0, 80.0, 80.0};
            return {x[vertex_index], y[vertex_index]};
        }
        const double x[4] = {-34.0, 94.0, 94.0, -34.0};
        const double y[4] = {-90.0, -90.0, 70.0, 70.0};
        return {x[vertex_index], y[vertex_index]};
    };
    double worst_offset_delta = 0.0;

    for (std::size_t index = 0; index < fixture.size(); ++index) {
        const std::string label = "fixture vertex " + std::to_string(index);
        const Vertex result =
            generate_or_report(suite, skeleton, harness, candidates, fixture[index], label);
        suite.expect(
            result.influences.size() == 2U,
            label + ": two candidates must yield two influences");
        if (result.influences.size() != 2U) {
            continue;
        }
        suite.expect(
            result.influences[0].bone_name == expected[index].first_bone &&
                result.influences[1].bone_name == expected[index].second_bone,
            label + ": canonical order was " + bone_order(result));
        suite.expect(
            result.influences[0].weight == expected[index].first_weight &&
                result.influences[1].weight == expected[index].second_weight,
            label + ": weights were " + std::to_string(result.influences[0].weight) + " / " +
                std::to_string(result.influences[1].weight));

        for (const Influence& influence : result.influences) {
            const auto [expected_x, expected_y] = authored_offset(index, influence.bone_name);
            const double delta_x = std::abs(influence.x - expected_x);
            const double delta_y = std::abs(influence.y - expected_y);
            worst_offset_delta = std::max(worst_offset_delta, std::max(delta_x, delta_y));
            suite.expect(
                delta_x < 1e-5 && delta_y < 1e-5,
                label + ": generated bind offset for " + influence.bone_name +
                    " must reproduce the fixture's authored geometry, but moved by (" +
                    std::to_string(delta_x) + ", " + std::to_string(delta_y) + ")");
        }
    }
    std::cout << std::setprecision(3)
              << "  MAR-176 worst generated-vs-authored bind offset delta: "
              << worst_offset_delta << " (tolerance 1e-5, set from the measured float32 "
              << "setup-pose error)\n"
              << std::setprecision(6);
}

void test_generate_exact_half_tie(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    const GenerateHarness harness = fixture_harness(suite);
    const auto candidates = candidates_by_name(suite, skeleton, {"spine", "arm_l"});

    // Vertex 2 sits past `spine`'s segment end and behind `arm_l`'s segment
    // start, and by construction those are the SAME double pair -- `arm_l`'s
    // segment starts at its parent `spine`'s world origin. Both distances
    // therefore evaluate the identical expression on identical operands, so
    // they are equal bit for bit whatever the origin happens to be and whatever
    // the compiler does about contraction. This is the one acceptance value in
    // MAR-176 that is immune to the float32 setup-pose error.
    const Vertex result = generate_or_report(
        suite, skeleton, harness, candidates, fixture_weight_vertices()[2], "exact half tie");
    suite.expect(result.influences.size() == 2U, "the tie must keep both candidates");
    if (result.influences.size() != 2U) {
        return;
    }
    suite.expect(
        result.influences[0].weight == 0.5 && result.influences[1].weight == 0.5,
        "equidistant candidates must weigh exactly 0.5 each, not 0.5 within a tolerance");
    suite.expect(
        result.influences[0].bone_name == "spine" && result.influences[1].bone_name == "arm_l",
        "an exact weight tie must break on ascending skeleton index, so spine precedes arm_l");
}

void test_generate_three_way_tie(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    const GenerateHarness harness = fixture_harness(suite);
    const auto candidates = candidates_by_name(suite, skeleton, {"root", "spine", "arm_l", "pivot"});

    // Vertex 0 lies behind root's degenerate point and behind both spine's and
    // pivot's segment starts, and all three clamp to root's world origin. The
    // fixture therefore contains a genuine three-way exact distance tie; AC3's
    // tie-break needs no synthetic case.
    const Vertex result = generate_or_report(
        suite, skeleton, harness, candidates, fixture_weight_vertices()[0], "three-way tie");
    suite.expect(result.influences.size() == 4U, "four candidates must yield four influences");
    if (result.influences.size() != 4U) {
        return;
    }
    suite.expect(
        bone_order(result) == "root,spine,pivot,arm_l",
        "three tied weights must order on ascending skeleton index (0, 1, 12), then arm_l (2); "
        "order was " + bone_order(result));
    suite.expect(
        result.influences[0].weight == result.influences[1].weight &&
            result.influences[1].weight == result.influences[2].weight,
        "the three tied distances must produce bit-identical weights");
    suite.expect(
        result.influences[0].weight == 0.28250519180307471 &&
            result.influences[3].weight == 0.15248442459077582,
        "three-way tie weights were " + std::to_string(result.influences[0].weight) + " / " +
            std::to_string(result.influences[3].weight));
}

void test_generate_coincident_and_single(TestSuite& suite) {
    const auto skeleton = make_bone_skeleton({{"root", -1}, {"spine", 0}, {"arm", 1}});
    const std::vector<marrow::runtime::BoneWorldTransform> transforms{
        world_at(0.0, 0.0), world_at(0.0, 100.0), world_at(60.0, 100.0)};
    GenerateHarness harness;
    harness.transforms = transforms;
    harness.segments = weights::bone_setup_segments(skeleton, transforms);

    // A vertex bound to spine with a zero offset sits exactly on spine's
    // segment: 1/d^2 is not a weight, so the nearest candidate takes all of it.
    const Vertex coincident = generate_or_report(
        suite, skeleton, harness, {0U, 1U, 2U},
        vertex({{"spine", 0.0, 0.0, 1.0}}), "coincident vertex");
    suite.expect(
        coincident.influences.size() == 1U && coincident.influences[0].bone_name == "spine",
        "a vertex lying on a candidate's segment falls back to that one candidate");
    suite.expect(
        coincident.influences.size() == 1U && coincident.influences[0].weight == 1.0,
        "the nearest-candidate fallback weighs exactly 1.0");

    const Vertex single = generate_or_report(
        suite, skeleton, harness, {1U}, vertex({{"arm", 10.0, 10.0, 1.0}}), "single candidate");
    suite.expect(
        single.influences.size() == 1U && single.influences[0].bone_name == "spine" &&
            single.influences[0].weight == 1.0,
        "a single candidate normalizes to exactly 1.0 whatever its distance");

    // `root` is parentless, so its segment is the point (0,0); a candidate set
    // containing it must measure a point distance rather than divide by zero.
    const Vertex zero_length = generate_or_report(
        suite, skeleton, harness, {0U, 1U}, vertex({{"arm", 0.0, 0.0, 1.0}}), "zero-length candidate");
    suite.expect(
        zero_length.influences.size() == 2U,
        "a zero-length candidate segment must be measured, not rejected");
    for (const Influence& influence : zero_length.influences) {
        suite.expect(
            std::isfinite(influence.weight) && influence.weight > 0.0,
            "a zero-length candidate must not produce a NaN weight");
    }
}

void test_generate_caps_at_the_four_nearest(TestSuite& suite) {
    const auto skeleton = make_bone_skeleton(
        {{"root", -1}, {"b1", 0}, {"b2", 1}, {"b3", 2}, {"b4", 3}, {"b5", 4}});
    const std::vector<marrow::runtime::BoneWorldTransform> transforms{
        world_at(0.0, 0.0),
        world_at(0.0, 10.0),
        world_at(0.0, 30.0),
        world_at(0.0, 60.0),
        world_at(0.0, 100.0),
        world_at(0.0, 150.0)};
    GenerateHarness harness;
    harness.transforms = transforms;
    harness.segments = weights::bone_setup_segments(skeleton, transforms);

    const Vertex subject = vertex({{"b1", 40.0, 0.0, 1.0}});
    const std::vector<std::size_t> ascending{0U, 1U, 2U, 3U, 4U, 5U};
    const Vertex result =
        generate_or_report(suite, skeleton, harness, ascending, subject, "six candidates");
    suite.expect(
        result.influences.size() == weights::kMaxMeshWeightInfluences,
        "six candidates must cap at four influences");

    // Which four survive is a function of the input SET, never of the order it
    // arrived in. `std::sort` is unstable and that is fine: the comparator is a
    // strict total order because bone indices are unique.
    const std::vector<std::vector<std::size_t>> permutations{
        {5U, 4U, 3U, 2U, 1U, 0U},
        {3U, 0U, 5U, 1U, 4U, 2U},
        {2U, 5U, 0U, 4U, 1U, 3U}};
    for (std::size_t index = 0; index < permutations.size(); ++index) {
        Vertex permuted = subject;
        weights::generate_mesh_weight_vertex(
            skeleton, harness.transforms, harness.segments, permutations[index], &permuted);
        suite.expect(
            identical(permuted, result),
            "permutation " + std::to_string(index) +
                " of the candidate list must select and weigh the same four bones");
    }
}

void test_generate_tie_break_decides_the_cap(TestSuite& suite) {
    // AC3's tie-break, asserted where it is actually observable.
    //
    // A distance tie among candidates that all SURVIVE the cap is invisible in
    // the output: their weights come out exactly equal, and
    // `canonicalize_mesh_weight_vertex()` then re-sorts equal weights on
    // ascending skeleton index -- so the canonical order is the same whatever
    // the generator's own tie-break did. The generator's tie-break is only
    // observable where it changes WHICH candidates survive, and that is here: a
    // distance tie straddling the four-influence cap.
    //
    // Every bone is parentless, so every segment is the point at its own origin
    // and each d^2 is a plain point distance on exact integers.
    const auto skeleton = make_bone_skeleton(
        {{"host", -1}, {"near", -1}, {"mid", -1}, {"far", -1}, {"tied_low", -1}, {"tied_high", -1}});
    GenerateHarness harness;
    harness.transforms = {
        world_at(0.0, 0.0),
        world_at(0.0, 10.0),
        world_at(0.0, 20.0),
        world_at(0.0, 30.0),
        world_at(40.0, 0.0),
        world_at(-40.0, 0.0)};
    harness.segments = weights::bone_setup_segments(skeleton, harness.transforms);

    // V = (0,0): d^2 is 100, 400, 900, 1600, 1600 for the five candidates, so
    // `tied_low` (index 4) and `tied_high` (index 5) tie exactly on the cap
    // boundary and only one of them can be kept.
    const Vertex result = generate_or_report(
        suite, skeleton, harness, {1U, 2U, 3U, 4U, 5U},
        vertex({{"host", 0.0, 0.0, 1.0}}), "cap-boundary tie");
    suite.expect(
        result.influences.size() == weights::kMaxMeshWeightInfluences,
        "five candidates must cap at four");
    suite.expect(
        bone_order(result) == "near,mid,far,tied_low",
        "a distance tie straddling the cap must be broken on ASCENDING skeleton index, so "
        "tied_low survives and tied_high does not; order was " + bone_order(result));
}

void test_generate_is_scale_free(TestSuite& suite) {
    // The regression for the design's single most important decision. The
    // canonicalizer drops `weight <= 1e-6` BEFORE it normalizes, so a raw
    // `1/d^2` hands an absolute gate a world-units-squared number: the gate
    // becomes "farther than 1000 units". At 100x the fixture's scale every raw
    // weight here is below 1e-6.
    const auto skeleton = make_bone_skeleton({{"root", -1}, {"spine", 0}, {"arm_l", 1}});
    const auto build = [&](double scale) {
        GenerateHarness harness;
        harness.transforms = {
            world_at(0.0, 0.0), world_at(0.0, 50.0 * scale), world_at(-30.0 * scale, 60.0 * scale)};
        harness.segments = weights::bone_setup_segments(skeleton, harness.transforms);
        return harness;
    };

    const GenerateHarness small = build(1.0);
    const GenerateHarness large = build(100.0);
    const Vertex at_one = generate_or_report(
        suite, skeleton, small, {1U, 2U}, vertex({{"spine", 14.0, -80.0, 1.0}}), "scale 1x");
    const Vertex at_hundred = generate_or_report(
        suite, skeleton, large, {1U, 2U}, vertex({{"spine", 1400.0, -8000.0, 1.0}}), "scale 100x");

    suite.expect(
        at_one.influences.size() == 2U && at_hundred.influences.size() == 2U,
        "this case fails if the generator hands raw 1/d^2 to the canonicalizer; the absolute "
        "1e-6 drop is not scale-free");
    if (at_one.influences.size() != 2U || at_hundred.influences.size() != 2U) {
        return;
    }
    suite.expect(
        at_one.influences[0].bone_name == at_hundred.influences[0].bone_name &&
            at_one.influences[1].bone_name == at_hundred.influences[1].bone_name,
        "a uniform rescale must not change which bones are chosen or their order");
    for (std::size_t index = 0; index < 2U; ++index) {
        const double difference =
            std::abs(at_one.influences[index].weight - at_hundred.influences[index].weight);
        suite.expect(
            difference < 1e-12,
            "a uniform rescale must not change the assignment; influence " +
                std::to_string(index) + " moved by " + std::to_string(difference));
    }
}

void test_generate_rejects_atomically(TestSuite& suite) {
    const auto& skeleton = fixture_skeleton(suite);
    const GenerateHarness harness = fixture_harness(suite);
    const auto candidates = candidates_by_name(suite, skeleton, {"spine", "arm_l"});
    const Vertex healthy = fixture_weight_vertices()[1];

    expect_generate_rejected(
        suite, skeleton, harness, {}, healthy,
        "mesh.generate_weights requires at least one candidate bone.",
        "empty candidate list");
    expect_generate_rejected(
        suite, skeleton, harness, {9999U}, healthy,
        "A candidate bone is outside the setup pose.",
        "out-of-range candidate index");
    // The caller is documented to de-duplicate, but the check is made here too:
    // a repeated index would break the strict total order the determinism
    // argument rests on, and a silently-defended precondition is a precondition
    // no test can prove.
    expect_generate_rejected(
        suite, skeleton, harness, {candidates[0], candidates[0]}, healthy,
        "A candidate bone was listed more than once.",
        "repeated candidate bone");
    expect_generate_rejected(
        suite, skeleton, harness, candidates, vertex({}),
        "A weighted vertex must keep at least one positive influence.",
        "vertex with no influences");
    expect_generate_rejected(
        suite, skeleton, harness, candidates,
        vertex({{"spine", 1.0, 2.0, 1.0}, {"arm_l", 3.0, 4.0, -1.0}}),
        "Weighted vertex influences must sum to a positive weight.",
        "existing weights sum to zero");
    expect_generate_rejected(
        suite, skeleton, harness, candidates, vertex({{"nope", 1.0, 2.0, 1.0}}),
        "Bone not found: nope",
        "unknown bone on the vertex");

    // A singular candidate transform is rejected rather than silently excluded:
    // dropping a bone the user explicitly checked is the mirror image of the
    // silent expansion AC1 forbids.
    const auto singular_skeleton = make_bone_skeleton({{"root", -1}, {"flat", 0}});
    marrow::runtime::BoneWorldTransform flat;
    flat.a = 0.0f;
    flat.b = 0.0f;
    flat.c = 0.0f;
    flat.d = 0.0f;
    flat.world_x = 10.0f;
    flat.world_y = 0.0f;
    GenerateHarness singular;
    singular.transforms = {world_at(0.0, 0.0), flat};
    singular.segments = weights::bone_setup_segments(singular_skeleton, singular.transforms);
    expect_generate_rejected(
        suite, singular_skeleton, singular, {1U}, vertex({{"root", 5.0, 5.0, 1.0}}),
        "Bone 'flat' has a singular setup transform and cannot be used as a weight candidate.",
        "singular candidate transform");
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
    suite.run("builds bone setup segments", [&]() { test_bone_setup_segments(suite); });
    suite.run("measures point-to-segment distance", [&]() { test_point_segment_distance(suite); });
    suite.run("extracts the setup-world position", [&]() {
        test_setup_world_position_extraction(suite);
    });
    suite.run("generates the fixture assignment", [&]() { test_generate_fixture_table(suite); });
    suite.run("generates an exact half tie", [&]() { test_generate_exact_half_tie(suite); });
    suite.run("breaks a three-way distance tie", [&]() { test_generate_three_way_tie(suite); });
    suite.run("falls back to the nearest candidate", [&]() {
        test_generate_coincident_and_single(suite);
    });
    suite.run("caps at the four nearest", [&]() {
        test_generate_caps_at_the_four_nearest(suite);
    });
    suite.run("breaks a cap-boundary tie on skeleton order", [&]() {
        test_generate_tie_break_decides_the_cap(suite);
    });
    suite.run("generates scale-free weights", [&]() { test_generate_is_scale_free(suite); });
    suite.run("generate rejects atomically", [&]() { test_generate_rejects_atomically(suite); });
    return suite.finish();
}
