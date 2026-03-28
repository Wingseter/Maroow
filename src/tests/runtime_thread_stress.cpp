#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace {

constexpr std::size_t kDefaultIterations = 2048;
constexpr std::size_t kDefaultThreadCount = 4;
constexpr std::size_t kMaxDefaultThreadCount = 8;

struct Options {
    std::filesystem::path skeleton_path{"assets/fixtures/player_idle.mskl"};
    std::size_t thread_count{0};
    std::size_t iterations{kDefaultIterations};
};

struct ThreadContext {
    marrow::runtime::Skeleton skeleton;
    marrow::runtime::AnimationState animation_state;

    ThreadContext(
        const std::shared_ptr<const marrow::runtime::SkeletonData>& skeleton_data,
        std::string_view animation_name)
        : skeleton(skeleton_data),
          animation_state(skeleton_data) {
        const std::shared_ptr<marrow::runtime::TrackEntry> entry =
            animation_state.set_animation(0, animation_name, true);
        if (entry == nullptr) {
            throw std::runtime_error("failed to create the stress animation entry");
        }
    }
};

void print_usage(std::string_view program) {
    std::cerr << "Usage: " << program
              << " [skeleton_path] [--threads N] [--iterations N]\n";
}

bool parse_size(std::string_view value, std::size_t* out_value) {
    if (out_value == nullptr || value.empty()) {
        return false;
    }

    std::size_t parsed = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }

        const std::size_t digit = static_cast<std::size_t>(ch - '0');
        if (parsed > (static_cast<std::size_t>(-1) - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }

    *out_value = parsed;
    return true;
}

Options parse_options(int argc, char** argv) {
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--threads" || argument == "--iterations") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("missing value for " + std::string(argument));
            }

            std::size_t parsed_value = 0;
            if (!parse_size(argv[index + 1], &parsed_value) || parsed_value == 0) {
                throw std::invalid_argument("invalid numeric value for " + std::string(argument));
            }

            if (argument == "--threads") {
                options.thread_count = parsed_value;
            } else {
                options.iterations = parsed_value;
            }

            ++index;
            continue;
        }

        if (argument == "--help" || argument == "-h") {
            print_usage(argv[0] != nullptr ? argv[0] : "marrow_thread_stress");
            std::exit(0);
        }

        if (argument.rfind("--", 0) == 0) {
            throw std::invalid_argument("unknown option " + std::string(argument));
        }

        options.skeleton_path = std::filesystem::path(argument);
    }

    return options;
}

std::size_t default_thread_count() {
    const unsigned int hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads == 0U) {
        return kDefaultThreadCount;
    }

    return std::max<std::size_t>(
        2,
        std::min<std::size_t>(hardware_threads, kMaxDefaultThreadCount));
}

const marrow::runtime::AnimationData& select_animation(
    const marrow::runtime::SkeletonData& skeleton_data) {
    if (const marrow::runtime::AnimationData* idle = skeleton_data.find_animation("idle");
        idle != nullptr) {
        return *idle;
    }

    if (!skeleton_data.animations().empty()) {
        return skeleton_data.animations().front();
    }

    throw std::runtime_error("the stress skeleton does not contain any animations");
}

bool is_finite_transform(const marrow::runtime::BoneWorldTransform& transform) {
    return std::isfinite(transform.a) &&
        std::isfinite(transform.b) &&
        std::isfinite(transform.c) &&
        std::isfinite(transform.d) &&
        std::isfinite(transform.world_x) &&
        std::isfinite(transform.world_y);
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    try {
        options = parse_options(argc, argv);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        print_usage(argv[0] != nullptr ? argv[0] : "marrow_thread_stress");
        return 1;
    }

    if (options.thread_count == 0) {
        options.thread_count = default_thread_count();
    }

    const marrow::runtime::SkeletonDataResult load_result =
        marrow::runtime::load_skeleton_data(options.skeleton_path);
    if (!load_result) {
        if (load_result.error.has_value()) {
            std::cerr << load_result.error->format() << '\n';
        } else {
            std::cerr << "failed to load skeleton data from " << options.skeleton_path << '\n';
        }
        return 1;
    }

    const std::shared_ptr<const marrow::runtime::SkeletonData> skeleton_data =
        load_result.skeleton_data;
    if (skeleton_data == nullptr) {
        std::cerr << "load_skeleton_data succeeded without returning SkeletonData.\n";
        return 1;
    }

    const marrow::runtime::AnimationData& animation = select_animation(*skeleton_data);

    std::vector<ThreadContext> contexts;
    contexts.reserve(options.thread_count);
    try {
        for (std::size_t thread_index = 0; thread_index < options.thread_count; ++thread_index) {
            contexts.emplace_back(skeleton_data, animation.name);
            contexts.back().skeleton.set_attachment_playback_time(
                static_cast<double>(thread_index) * (1.0 / 30.0));
        }
    } catch (const std::exception& exception) {
        std::cerr << "failed to initialize thread contexts: " << exception.what() << '\n';
        return 1;
    }

    std::atomic<std::size_t> ready_count{0};
    std::atomic<bool> start{false};
    std::atomic<bool> failed{false};
    std::mutex failure_mutex;
    std::vector<std::string> failures;

    auto record_failure = [&](std::string message) {
        failed.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(failure_mutex);
        if (failures.size() < 16) {
            failures.push_back(std::move(message));
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(options.thread_count);
    for (std::size_t thread_index = 0; thread_index < options.thread_count; ++thread_index) {
        workers.emplace_back([&, thread_index] {
            ThreadContext& context = contexts[thread_index];
            ready_count.fetch_add(1, std::memory_order_acq_rel);
            while (!start.load(std::memory_order_acquire)) {
                if (failed.load(std::memory_order_acquire)) {
                    return;
                }
                std::this_thread::yield();
            }

            try {
                for (std::size_t iteration = 0;
                     iteration < options.iterations &&
                     !failed.load(std::memory_order_acquire);
                     ++iteration) {
                    const double delta = (1.0 / 60.0) +
                        static_cast<double>((thread_index + iteration) % 5) * 0.0005;
                    context.animation_state.update(delta);
                    context.animation_state.apply(context.skeleton);
                    context.skeleton.advance_attachment_playback(delta * 0.5);

                    const std::shared_ptr<marrow::runtime::TrackEntry> current =
                        context.animation_state.get_current(0);
                    if (current == nullptr || current->animation == nullptr) {
                        record_failure(
                            "thread " + std::to_string(thread_index) +
                            " lost its current track entry");
                        return;
                    }

                    const auto& world_transforms = context.skeleton.bone_world_transforms();
                    if (world_transforms.size() != skeleton_data->bones().size()) {
                        record_failure(
                            "thread " + std::to_string(thread_index) +
                            " observed a mismatched bone transform count");
                        return;
                    }

                    for (std::size_t bone_index = 0;
                         bone_index < world_transforms.size();
                         ++bone_index) {
                        if (is_finite_transform(world_transforms[bone_index])) {
                            continue;
                        }

                        record_failure(
                            "thread " + std::to_string(thread_index) +
                            " produced a non-finite world transform at bone " +
                            std::to_string(bone_index));
                        return;
                    }
                }
            } catch (const std::exception& exception) {
                record_failure(
                    "thread " + std::to_string(thread_index) +
                    " threw: " + exception.what());
            }
        });
    }

    while (ready_count.load(std::memory_order_acquire) < options.thread_count) {
        std::this_thread::yield();
    }

    start.store(true, std::memory_order_release);

    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    if (!failures.empty()) {
        for (const std::string& failure : failures) {
            std::cerr << failure << '\n';
        }
        return 1;
    }

    std::cout << "Concurrent skeleton update stress passed: threads="
              << options.thread_count
              << " iterations=" << options.iterations
              << " animation=" << animation.name
              << " source=" << options.skeleton_path.string() << '\n';
    return 0;
}
