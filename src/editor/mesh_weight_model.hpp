#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/project.hpp"
#include "marrow/runtime/skeleton.hpp"

/**
 * @file mesh_weight_model.hpp
 * @brief UI-free canonical rules for mesh vertex weights.
 *
 * This translation unit is deliberately part of the `marrow_editor` static
 * library rather than the shell executable: `src/editor/shell_weight_paint.cpp`
 * is compiled into `marrow_editor_shell`, so nothing under `src/tests/` can
 * link it. Every weight-authoring path — brush, numeric entry, and the agent
 * operations — shares the rules declared here.
 */
namespace marrow::editor::mesh_weight_model {

/**
 * @brief The per-vertex bone influence cap.
 *
 * The runtime, the `.mskl` loader, and the `.marrow` loader all cap a vertex at
 * four influences; this is the editor-side name for that number. Runtime peers:
 * `src/runtime/skeleton_parse.cpp:1575`, `src/editor/project.cpp:2128`
 * and `:5532`.
 */
inline constexpr std::size_t kMaxMeshWeightInfluences = 4U;

/**
 * @brief Weights at or below this are treated as absent.
 *
 * Chosen to equal the value the brush already used, so brush behaviour on
 * existing fixtures is unchanged.
 */
inline constexpr double kMeshWeightEpsilon = 1e-6;

/**
 * @brief How far a canonical vertex's weights may sum from exactly one.
 *
 * Normalization divides by the summed weight, and re-summing the quotients
 * reproduces `1.0` only up to rounding: summing at most
 * `kMaxMeshWeightInfluences` values costs at most three roundings, so the
 * result lies within `3 * DBL_EPSILON` of one. Measured worst case over 1.8M
 * synthetic vertices is 2 ULP.
 *
 * This tolerance is what makes canonicalization a **bit-exact fixed point**:
 * a second pass sees a sum already inside it and skips the division entirely.
 * Testing for exactly `1.0` instead does not work, and the project's own
 * fixture is the counterexample — `0.6/0.8 + 0.2/0.8` is `0.9999999999999999`,
 * so an exact test never fires and every later pass would nudge the weights
 * again.
 */
inline constexpr double kMeshWeightSumTolerance =
    4.0 * std::numeric_limits<double>::epsilon();

/** @brief A bind offset in a bone's local frame, at the precision `.marrow` stores. */
struct MeshWeightBindOffset {
    double x{0.0};
    double y{0.0};
};

/**
 * @brief Inverse-transforms a world point into a bone's local frame.
 *
 * Returns `std::nullopt` when the transform is singular (`|det| <= 1e-8`). The
 * body promotes the float32 `BoneWorldTransform` components to `double` before
 * dividing, and the result is returned as `double`.
 *
 * The shell helper this replaces returned a `runtime::AttachmentVertex`, whose
 * members are **`float`** (`skeleton.hpp:151-157`), so every bind offset it
 * produced was silently rounded to float32 before being stored in a `double`
 * field and written to `.marrow` at full width. That cost roughly six
 * significant digits on every newly painted influence and made a rebind fail to
 * reproduce its own setup-world point by ~1e-5. Returning `double` is what lets
 * `rebind_mesh_weight_vertex()` actually satisfy the consistency contract its
 * documentation states.
 */
std::optional<MeshWeightBindOffset> inverse_transform_point_safe(
    const marrow::runtime::BoneWorldTransform& transform,
    double world_x,
    double world_y);

/**
 * @brief Rewrites one vertex's influence list into the single canonical form.
 *
 * Returns an empty string on success. On rejection it returns a message naming
 * the offending bone or value and leaves `*vertex` **untouched**, so a caller
 * that canonicalizes a list of vertices and stops at the first error has
 * mutated nothing it has not already accepted.
 *
 * The steps, in order: reject non-finite values, reject unknown and empty bone
 * names, merge duplicate bones (summed weight, weight-weighted mean bind
 * offset), drop influences at or below `kMeshWeightEpsilon`, sort by descending
 * weight then ascending skeleton index, cap at `kMaxMeshWeightInfluences`,
 * reject an empty result, and normalize.
 *
 * The post-condition is strictly stronger than every precondition
 * `validate_project_for_save()` (`project.cpp:5504-5568`) and the `.marrow`
 * loader (`project.cpp:2085-2220`) impose on a weight vertex, so a project
 * built only from canonical vertices can always be saved and reloaded.
 *
 * The function is a bit-exact fixed point of itself. Two properties make it so,
 * and both are load-bearing: the sum test uses `kMeshWeightSumTolerance` rather
 * than exact equality, and the sort runs **again** on the normalized weights.
 * Division can collapse two weights that differed by one ULP into an exact tie,
 * and the first sort ordered them on their pre-division values, so without the
 * second sort the output would not satisfy its own comparator.
 */
std::string canonicalize_mesh_weight_vertex(
    const marrow::runtime::SkeletonData& skeleton,
    marrow::editor::MeshWeightVertexEdit* vertex);

/**
 * @brief Converts a runtime mesh attachment's weights into an editable overlay.
 *
 * An influence whose `bone_index` is out of range is **skipped** rather than
 * given a synthesized placeholder name: a placeholder cannot resolve, so it
 * would turn every later canonicalization of that vertex into a hard rejection
 * and make the attachment unauthorable. A vertex left with no influences by the
 * skip is caught by `canonicalize_mesh_weight_vertex()` the first time anything
 * writes to it.
 *
 * The result is deliberately **not** canonicalized. Materializing an imported
 * attachment must not by itself rewrite weights the user never touched;
 * `normalize_weights` is the explicit repair command for that.
 */
marrow::editor::MeshWeightAttachmentEdit mesh_weight_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name,
    const marrow::runtime::AttachmentData& attachment);

/**
 * @brief Bone world transforms with every bone at its setup pose.
 *
 * Built on a scratch `runtime::Skeleton` so the live preview is never
 * disturbed. Returns an empty vector when `skeleton` is null.
 */
std::vector<marrow::runtime::BoneWorldTransform> setup_pose_bone_world_transforms(
    const std::shared_ptr<const marrow::runtime::SkeletonData>& skeleton);

/**
 * @brief Overload for callers that hold only a reference.
 *
 * `runtime::Skeleton` takes ownership by `shared_ptr`, so this wraps `skeleton`
 * in a **non-owning** aliasing handle. The scratch skeleton is local and never
 * escapes, so it cannot outlive the referent.
 */
std::vector<marrow::runtime::BoneWorldTransform> setup_pose_bone_world_transforms(
    const marrow::runtime::SkeletonData& skeleton);

/**
 * @brief One bone's setup-pose body, as the viewport draws it.
 *
 * `runtime::BoneData` carries no length (`skeleton.hpp:60-65`; the only
 * `"length"` in the parser is the path-constraint spacing mode), so a bone's
 * body has to come from the hierarchy. Marrow already has exactly one such
 * definition and this matches it: `shell_viewport.cpp:1659-1675` draws a bone
 * as the line from its parent's world origin to its own, and `:2127-2140`
 * hit-tests that same segment. A root bone degenerates to the point at its own
 * origin, which is what the viewport shows too -- it draws no body for a
 * parentless bone.
 */
struct BoneSetupSegment {
    double start_x{0.0};  ///< parent's setup-world origin, or own for a root
    double start_y{0.0};
    double end_x{0.0};    ///< own setup-world origin
    double end_y{0.0};
};

/**
 * @brief Every bone's setup-pose segment, indexed by skeleton bone index.
 *
 * Built by a single ascending loop, so the result's order is the skeleton's
 * order and depends on nothing implicit. A bone whose parent index does not
 * resolve, or resolves outside `setup_transforms`, degenerates to a point at
 * its own origin rather than reading out of bounds.
 */
std::vector<BoneSetupSegment> bone_setup_segments(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms);

/**
 * @brief Squared distance from a world point to a bone's setup segment.
 *
 * Squared, not linear, on purpose: the weight is `1 / d^2`, so the square root
 * would be taken only to be undone. Removing it removes two roundings per
 * candidate and one library call whose exactness is not guaranteed by IEEE-754
 * for every implementation.
 *
 * Structurally mirrors `shell_viewport.cpp:438-457` so the number the generator
 * measures is the number the hit-test measures, but deliberately does not share
 * its code: that version is `float` in screen space and lives in the executable,
 * which `src/tests/` cannot link.
 *
 * The degenerate test is `!(ab2 > 0.0)` rather than an epsilon. An epsilon here
 * would be a world-space threshold, i.e. a scale-dependent knob, and none is
 * needed: for any `ab2 > 0` the division is finite and `t` is clamped into
 * `[0, 1]`. Writing it as a negated `>` also routes a NaN -- reachable only from
 * a non-finite setup transform -- into the point branch, where the caller's
 * finiteness guard catches it, instead of into an unclamped `t`.
 */
double point_segment_distance_squared(
    double point_x,
    double point_y,
    const BoneSetupSegment& segment);

/**
 * @brief Replaces one vertex's influences with the inverse-square assignment
 *        over an explicit candidate set.
 *
 * Returns an empty string on success and leaves `*vertex` untouched on any
 * rejection. `candidate_bone_indices` must be non-empty, strictly ascending,
 * and free of duplicates -- the caller validates that once per call.
 *
 * The vertex's setup-world position `V` comes from
 * `setup_world_position_of_weight_vertex()` on its **existing** influences, so
 * generation preserves where the vertex sits and changes only which bones hold
 * it. Candidates are ordered by `(d^2 ascending, bone index ascending)`, which
 * is a strict total order because the indices are unique -- so the sorted
 * permutation is unique and independent of the order the caller supplied.
 *
 * The top-`K` raw `1/d^2` weights are normalized **before**
 * `canonicalize_mesh_weight_vertex()` sees them. This is load-bearing, not
 * cosmetic: the canonicalizer's `<= kMeshWeightEpsilon` drop is absolute and
 * runs before its own normalization, so a raw `1/d^2` turns that gate into
 * "farther than 1000 world units". Measured on this fixture's geometry, at 10x
 * its scale a legitimate influence is already dropped and at 100x every
 * influence is dropped and the vertex is rejected outright -- and the shipped
 * `tank` rig is 10x the fixture (max |world| 2395 against 230).
 * Pre-normalizing makes the threshold relative to the four kept candidates.
 *
 * **Determinism.** Every container on the path is a `std::vector` indexed or
 * sorted on the skeleton bone index; there is no hash-ordered container, no
 * parallelism, and no `sqrt`/`hypot`/`pow`/`fma`. The sum is accumulated left to
 * right over the sorted top-`K`. The result is therefore bit-identical across
 * repeated calls, across processes, and across the GUI, agent, and MCP entry
 * points **within one binary**. Cross-compiler and cross-architecture
 * bit-identity is *not* claimed: floating-point contraction may fuse `a*b + c`
 * into an `fma` and Marrow sets no `-ffp-contract`.
 *
 * Like rebind, generation is deterministic but **not** bit-exactly idempotent:
 * `BoneWorldTransform` is six `float`s while bind offsets are `double`, so a
 * second generate recovers `V` from the first one's output with a few ULPs of
 * difference and may legitimately report a change.
 */
std::string generate_mesh_weight_vertex(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms,
    const std::vector<BoneSetupSegment>& segments,
    const std::vector<std::size_t>& candidate_bone_indices,
    marrow::editor::MeshWeightVertexEdit* vertex);

/**
 * @brief The setup-world position a weighted vertex's influences describe.
 *
 * `V = (sum_i w_i * S_i(x_i,y_i)) / (sum_i w_i)`, with the division performed
 * only when the total is outside `kMeshWeightSumTolerance` -- a canonical
 * vertex is therefore unaffected bit for bit, while a vertex materialized from
 * a runtime document whose weights do not sum to one still yields its weighted
 * average rather than a scaled skinning sum.
 *
 * Rejects atomically on an empty influence list, an unknown bone, a bone
 * outside the setup pose, a non-finite intermediate, or a non-positive total.
 *
 * This is step 1 of `rebind_mesh_weight_vertex()`, extracted verbatim so that
 * rebind and `generate_mesh_weight_vertex()` cannot drift apart. It is the only
 * authoritative derivation: for a weighted mesh `geometry.vertices` is
 * decorative -- `Skeleton::evaluate_mesh_attachment_pose()` reads it only for
 * the vertex count (`src/runtime/skeleton_skin.cpp:529`) and the `.mskl` parser
 * fills it from a JSON array nothing validates against the weights.
 */
std::string setup_world_position_of_weight_vertex(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms,
    const marrow::editor::MeshWeightVertexEdit& vertex,
    double* world_x,
    double* world_y);

/**
 * @brief Re-expresses each influence's bind offset in its own bone's setup frame.
 *
 * Holds the skinned position and every weight fixed and solves for a
 * *consistent* set of local offsets: first the vertex's setup-world position
 * `V = sum_i w_i * S_i(x_i, y_i)`, then `(x_i', y_i') = S_i^-1(V)` for every
 * influence. Afterwards every bone's local offset points at the same setup-world
 * location, which is the definition of a correct bind.
 *
 * Rejects atomically, leaving `*vertex` untouched, on a singular setup
 * transform, an out-of-range bone, or a non-finite intermediate. Never changes
 * which bones influence the vertex and never changes a weight.
 *
 * **Precision.** `BoneWorldTransform` packs to six `float`s
 * (`skeleton.hpp:1009`) while bind offsets are `double`
 * (`project.hpp:196-197`), so `S(S^-1(V))` does not reproduce `V` bit-for-bit.
 * Rebind is therefore **deterministic** — the same input always yields the same
 * doubles — but it is **not** bit-exactly idempotent. The narrowing enters
 * through the transform, not through the weight.
 */
std::string rebind_mesh_weight_vertex(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms,
    marrow::editor::MeshWeightVertexEdit* vertex);

} // namespace marrow::editor::mesh_weight_model
