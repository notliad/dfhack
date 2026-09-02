// SPDX-License-Identifier: Zlib

#ifndef SMOOTH_MOVEMENT_VISUAL_ANIMATION_H
#define SMOOTH_MOVEMENT_VISUAL_ANIMATION_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "CoordTemplate.h"
#include "DataDefs.h"
#include "df/coord2d.h"

namespace df {
struct graphic_viewportst;
}

enum class viewport_visual_layer : uint8_t {
    right,
    center,
    left,
    upright,
    up,
    upleft,
    vehicle,
    item,
    designation,
    count
};

enum class visual_render_groupst : uint8_t { item, vehicle, main, upper, designation, count };

struct visual_layer_descriptorst {
    viewport_visual_layer layer;
    visual_render_groupst render_group;
    bool moves_independently;
    bool matches_any_previous;
    DFHack::Coord2d<int8_t> anchor_offset;
};

const std::array visual_layer_descriptors = {
    visual_layer_descriptorst{.layer = viewport_visual_layer::right,
                              .render_group = visual_render_groupst::main,
                              .moves_independently = false,
                              .matches_any_previous = false,
                              .anchor_offset = {-1, 0}},
    visual_layer_descriptorst{.layer = viewport_visual_layer::center,
                              .render_group = visual_render_groupst::main,
                              .moves_independently = true,
                              .matches_any_previous = false,
                              .anchor_offset = {0, 0}},
    visual_layer_descriptorst{.layer = viewport_visual_layer::left,
                              .render_group = visual_render_groupst::main,
                              .moves_independently = false,
                              .matches_any_previous = false,
                              .anchor_offset = {1, 0}},
    visual_layer_descriptorst{.layer = viewport_visual_layer::upright,
                              .render_group = visual_render_groupst::upper,
                              .moves_independently = false,
                              .matches_any_previous = false,
                              .anchor_offset = {-1, 1}},
    visual_layer_descriptorst{.layer = viewport_visual_layer::up,
                              .render_group = visual_render_groupst::upper,
                              .moves_independently = false,
                              .matches_any_previous = false,
                              .anchor_offset = {0, 1}},
    visual_layer_descriptorst{.layer = viewport_visual_layer::upleft,
                              .render_group = visual_render_groupst::upper,
                              .moves_independently = false,
                              .matches_any_previous = false,
                              .anchor_offset = {1, 1}},
    visual_layer_descriptorst{.layer = viewport_visual_layer::vehicle,
                              .render_group = visual_render_groupst::vehicle,
                              .moves_independently = true,
                              .matches_any_previous = true,
                              .anchor_offset = {0, 0}},
    visual_layer_descriptorst{.layer = viewport_visual_layer::item,
                              .render_group = visual_render_groupst::item,
                              .moves_independently = true,
                              .matches_any_previous = false,
                              .anchor_offset = {0, 0}},
    visual_layer_descriptorst{.layer = viewport_visual_layer::designation,
                              .render_group = visual_render_groupst::designation,
                              .moves_independently = false,
                              .matches_any_previous = true,
                              .anchor_offset = {0, 0}}};

constexpr std::array visual_layer_draw_order = {
    viewport_visual_layer::center,   viewport_visual_layer::item,
    viewport_visual_layer::vehicle,  viewport_visual_layer::right,
    viewport_visual_layer::left,     viewport_visual_layer::upright,
    viewport_visual_layer::up,       viewport_visual_layer::upleft,
    viewport_visual_layer::designation};

constexpr bool valid_visual_layer_draw_order() {
    uint16_t layers = 0;
    for (const auto layer : visual_layer_draw_order) {
        const uint16_t bit = uint16_t(1U << static_cast<uint8_t>(layer));
        if (layers & bit)
            return false;
        layers |= bit;
    }
    return layers == uint16_t((1U << static_cast<uint8_t>(viewport_visual_layer::count)) - 1);
}

static_assert(valid_visual_layer_draw_order());

inline const visual_layer_descriptorst &visual_layer_descriptor(viewport_visual_layer layer) {
    return visual_layer_descriptors[static_cast<size_t>(layer)];
}

inline visual_render_groupst visual_render_group(viewport_visual_layer layer) {
    return visual_layer_descriptor(layer).render_group;
}

inline bool visual_layer_moves_independently(viewport_visual_layer layer) {
    return visual_layer_descriptor(layer).moves_independently;
}

inline bool visual_layer_tracks_own_movement(viewport_visual_layer layer) {
    const auto &descriptor = visual_layer_descriptor(layer);
    return descriptor.moves_independently ||
           descriptor.render_group == visual_render_groupst::designation;
}

inline bool visual_layer_matches(viewport_visual_layer layer, int32_t current, int32_t previous) {
    return visual_layer_descriptor(layer).matches_any_previous ? previous != 0
                                                               : previous == current;
}

constexpr uint32_t default_movement_duration_ms = 100;
constexpr int32_t default_game_fps = 100;
constexpr uint32_t default_movement_duration_numerator =
    default_movement_duration_ms * uint32_t(default_game_fps);

struct viewport_visual_animation_inputst {
    const df::graphic_viewportst *viewport = nullptr;
    df::coord2d dimensions = df::coord2d(0, 0);
    uint64_t context_revision = 0;
    std::array<const int32_t *, static_cast<size_t>(viewport_visual_layer::count)> current{};
    std::array<const int32_t *, static_cast<size_t>(viewport_visual_layer::count)> previous{};
    std::span<const int32_t> current_background;
    std::span<const int32_t> previous_background;
    uint32_t movement_duration_ms = default_movement_duration_ms;
    // Current map-scroll offset (window_x/window_y). A pure pan does not bump
    // context_revision. Only a hint: it changes at input time, the buffers shift
    // on a later render frame.
    df::coord2d pan = df::coord2d(0, 0);

    bool valid() const {
        if (viewport == nullptr || dimensions.x <= 0 || dimensions.y <= 0)
            return false;
        for (size_t layer = 0; layer < current.size(); ++layer) {
            if (current[layer] == nullptr || previous[layer] == nullptr)
                return false;
        }
        if (current_background.empty() != previous_background.empty())
            return false;
        if (!current_background.empty() &&
            (current_background.size() != size_t(dimensions.x) * size_t(dimensions.y) ||
             previous_background.size() != current_background.size()))
            return false;
        return true;
    }
};

struct visual_movement_renderst {
    bool active = false;
    DFHack::Coord2d<float> source{0.0f, 0.0f};
    float progress = 1.0f;
    bool inherited = false;
};

struct visual_follow_renderst {
    bool active = false;
    DFHack::Coord2d<float> offset{0.0f, 0.0f};
};

inline float animation_progress(uint32_t now_ms, uint32_t start_time_ms, uint32_t duration_ms) {
    return std::min(1.0f, float(now_ms - start_time_ms) / duration_ms);
}

inline bool visual_moved_between_tiles(viewport_visual_layer layer, const int32_t *current,
                                       const int32_t *previous, int32_t source, int32_t target) {
    return previous[target] == 0 && current[source] == 0 &&
           (layer == viewport_visual_layer::designation || previous[source] != 0);
}

inline int32_t inherited_visual_source_tile(int32_t overlay_target, float center_source,
                                            float center_target) {
    return overlay_target + int32_t(std::lround(center_source - center_target));
}

enum class visual_facingst : int8_t { east = 0, west = 1 };

// DF creature art faces west, so only east needs flipping. Also the default and
// cleared value.
constexpr visual_facingst native_sprite_facing = visual_facingst::west;

// Sticky facing: only a horizontal component changes it.
constexpr visual_facingst facing_after_move(int32_t dx, visual_facingst previous) {
    if (dx > 0)
        return visual_facingst::east;
    if (dx < 0)
        return visual_facingst::west;
    return previous;
}

// anchor_offset.x is only -1, 0 or +1, so a creature is at most three columns wide
// here.
constexpr int32_t mirrored_tile_x(int32_t piece_x, int32_t anchor_x) {
    return anchor_x - (piece_x - anchor_x);
}

class visual_animation_managerst {
    struct movementst {
        uint64_t id;
        viewport_visual_layer layer;
        int32_t texpos;
        DFHack::Coord2d<float> source;
        df::coord2d target;
        uint32_t start_time_ms;
        uint32_t duration_ms;
    };

    struct movement_cadencest {
        viewport_visual_layer layer;
        int32_t texpos;
        df::coord2d tile;
        uint32_t last_move_time_ms;
    };

    struct viewport_animationst {
        const df::graphic_viewportst *viewport = nullptr;
        df::coord2d dimensions = df::coord2d(0, 0);
        uint64_t context_revision = 0;
        bool has_context = false;
        bool seen = false;
        std::vector<movementst> movements;
        std::vector<movement_cadencest> movement_cadences;
        uint64_t follow_movement_id = 0;
        uint32_t movement_duration_ms = default_movement_duration_ms;
        // One facing per tile, not per unit: the viewport exposes one creature
        // texpos per tile.
        std::vector<int8_t> facing;
        // Stationary mirrored creatures are repainted every frame; this is the
        // cheap pre-check.
        bool has_mirrored = false;
        df::coord2d pan = df::coord2d(0, 0);
        bool has_pan = false;
        // Window scrolls not yet observed in the buffers, oldest first.
        // A signed total would cancel on a reversing drag while both shifts are
        // still owed.
        std::vector<df::coord2d> pending;
        // Redraws no prefix has matched.
        int32_t pending_frames = 0;
        // Redraws spent waiting for the buffers to move at all.
        int32_t pending_age = 0;
        // Redraws left in which new-movement detection stays suppressed after
        // scroll activity.
        int32_t suppress_frames = 0;
        // Buffer contents last seen, to recognize a repeat of them.
        std::optional<uint64_t> buffer_signature;
        // Set while the previous buffer still belongs to a view that has been left
        // behind.
        bool previous_view_stale = false;
    };

    uint32_t frame_time_ms = 0;
    uint32_t frame_delta_ms = 0;
    bool has_frame = false;
    bool force_full_redraw = false;
    uint64_t next_movement_id = 1;
    std::vector<viewport_animationst> viewports;

    // Cadence is trusted for only a bounded multiple of the configured fallback.
    static constexpr uint32_t max_movement_cadence_baselines = 4;
    // Scrolling faster than detection keeps up: give up rather than test ever
    // more prefixes.
    static constexpr size_t max_pending_shifts = 8;
    // Bounds the wait on a scroll that never lands, so suppression cannot stick
    // forever. These are observed recovery limits, not viewport limits.
    static constexpr int32_t max_pending_age_frames = 120;
    // A majority of visible, position-bearing sprites must confirm a scroll.
    static constexpr double min_scroll_match_ratio = 0.5;
    // Terrain is denser evidence than sprites, so require a stronger match.
    static constexpr double min_background_scroll_match_ratio = 0.6;
    // Wait for buffer settling before treating changes as creature movement.
    static constexpr int32_t scroll_settle_redraws = 2;
    // Give up when a queued scroll does not match this many redraws.
    static constexpr int32_t max_unmatched_scroll_redraws = 4;

    static void clear_pending(viewport_animationst &state) {
        state.pending.clear();
        state.pending_frames = 0;
        state.pending_age = 0;
    }

    static void abandon_pending(viewport_animationst &state) {
        state.movements.clear();
        state.movement_cadences.clear();
        state.follow_movement_id = 0;
        clear_pending(state);
    }

    static void reset_facing(viewport_animationst &state) {
        std::fill(state.facing.begin(), state.facing.end(), int8_t(native_sprite_facing));
        state.has_mirrored = false;
    }

    static void reset_tracking(viewport_animationst &state) {
        abandon_pending(state);
        state.suppress_frames = 0;
    }

    // Identifies the buffer contents this frame, to tell a redrawn viewport from
    // a repeated one.
    static uint64_t compute_buffer_signature(const viewport_visual_animation_inputst &input) {
        // FNV-1a. Only ever compared against the previous frame's value, never
        // stored.
        constexpr uint64_t fnv_offset_basis = 0xcbf29ce484222325ULL;
        constexpr uint64_t fnv_prime = 0x100000001b3ULL;
        uint64_t hash = fnv_offset_basis;
        const int32_t tile_count = input.dimensions.x * input.dimensions.y;
        for (size_t layer = 0; layer < input.current.size(); ++layer) {
            if (!visual_layer_tracks_own_movement(static_cast<viewport_visual_layer>(layer)))
                continue;
            for (int32_t i = 0; i < tile_count; ++i) {
                hash = (hash ^ uint64_t(uint32_t(input.current[layer][i]))) * fnv_prime;
                hash = (hash ^ uint64_t(uint32_t(input.previous[layer][i]))) * fnv_prime;
            }
        }
        if (!input.current_background.empty() && !input.previous_background.empty())
            for (int32_t i = 0; i < tile_count; ++i) {
                hash = (hash ^ uint64_t(uint32_t(input.current_background[i]))) * fnv_prime;
                hash = (hash ^ uint64_t(uint32_t(input.previous_background[i]))) * fnv_prime;
            }
        return hash;
    }

    static double background_shift_match_ratio(const viewport_visual_animation_inputst &input,
                                               int32_t dwx, int32_t dwy) {
        if (input.current_background.empty() || input.previous_background.empty())
            return -1.0;
        int32_t considered = 0;
        int32_t matches = 0;
        for (int32_t x = 0; x < input.dimensions.x; ++x) {
            const int32_t sx = x + dwx;
            if (sx < 0 || sx >= input.dimensions.x)
                continue;
            for (int32_t y = 0; y < input.dimensions.y; ++y) {
                const int32_t sy = y + dwy;
                if (sy < 0 || sy >= input.dimensions.y)
                    continue;
                const int32_t current = input.current_background[x * input.dimensions.y + y];
                if (current == 0)
                    continue;
                ++considered;
                if (input.previous_background[sx * input.dimensions.y + sy] == current)
                    ++matches;
            }
        }
        return considered == 0 ? -1.0 : double(matches) / double(considered);
    }

    // Fraction of tracked sprites consistent with a buffer shift:
    // current[x]==previous[x+dwx]. Negative when there is nothing to compare.
    static double shift_match_ratio(const viewport_visual_animation_inputst &input, int32_t dwx,
                                    int32_t dwy) {
        int32_t considered = 0;
        int32_t matches = 0;
        for (size_t layer = 0; layer < input.current.size(); ++layer) {
            const auto id = static_cast<viewport_visual_layer>(layer);
            if (!visual_layer_tracks_own_movement(id))
                continue;
            // A layer matching any non-zero previous carries no position, so it would
            // vote for every hypothesis and carry an unapplied scroll over the bar.
            if (visual_layer_descriptor(id).matches_any_previous)
                continue;
            const int32_t *current = input.current[layer];
            const int32_t *previous = input.previous[layer];
            for (int32_t x = 0; x < input.dimensions.x; ++x) {
                const int32_t sx = x + dwx;
                if (sx < 0 || sx >= input.dimensions.x)
                    continue;
                for (int32_t y = 0; y < input.dimensions.y; ++y) {
                    const int32_t texpos = current[x * input.dimensions.y + y];
                    if (texpos == 0)
                        continue;
                    const int32_t sy = y + dwy;
                    if (sy < 0 || sy >= input.dimensions.y)
                        continue;
                    ++considered;
                    if (visual_layer_matches(id, texpos, previous[sx * input.dimensions.y + sy]))
                        ++matches;
                }
            }
        }
        if (considered == 0)
            return -1.0;
        return double(matches) / double(considered);
    }

    static df::coord2d shared_movement_delta(std::span<const int32_t> current,
                                             std::span<const int32_t> previous,
                                             int32_t dim_x, int32_t dim_y) {
        df::coord2d best(0, 0);
        int32_t best_count = 1;
        bool ambiguous = false;
        for (int32_t dx = -1; dx <= 1; ++dx) {
            for (int32_t dy = -1; dy <= 1; ++dy) {
                if (dx == 0 && dy == 0)
                    continue;
                int32_t count = 0;
                for (int32_t x = 0; x < dim_x; ++x) {
                    const int32_t source_x = x + dx;
                    if (source_x < 0 || source_x >= dim_x)
                        continue;
                    for (int32_t y = 0; y < dim_y; ++y) {
                        const int32_t source_y = y + dy;
                        if (source_y < 0 || source_y >= dim_y)
                            continue;
                        const int32_t target = x * dim_y + y;
                        const int32_t texpos = current[target];
                        if (texpos != 0 && previous[target] != texpos &&
                            previous[source_x * dim_y + source_y] == texpos)
                            ++count;
                    }
                }
                if (count > best_count) {
                    best_count = count;
                    best = df::coord2d(dx, dy);
                    ambiguous = false;
                } else if (count == best_count && count > 1)
                    ambiguous = true;
            }
        }
        return ambiguous ? df::coord2d(0, 0) : best;
    }

    viewport_animationst &get_viewport(const viewport_visual_animation_inputst &input) {
        for (viewport_animationst &state : viewports) {
            if (state.viewport == input.viewport)
                return state;
        }
        viewports.emplace_back();
        viewports.back().viewport = input.viewport;
        return viewports.back();
    }

    float movement_progress(uint32_t start_time_ms, uint32_t duration_ms) const {
        return animation_progress(frame_time_ms, start_time_ms, duration_ms);
    }

    static bool valid_movement_cadence(uint32_t interval_ms, uint32_t baseline_ms) {
        return interval_ms != 0 &&
               uint64_t(interval_ms) <= uint64_t(baseline_ms) * max_movement_cadence_baselines;
    }

  public:
    visual_animation_managerst() = default;

    void begin_frame(uint32_t now_ms) {
        frame_delta_ms = has_frame ? now_ms - frame_time_ms : 0;
        frame_time_ms = now_ms;
        has_frame = true;
        force_full_redraw = false;
        // Keep one final full redraw when the last movement expires.
        for (viewport_animationst &state : viewports) {
            state.seen = false;
            if (!state.movements.empty())
                force_full_redraw = true;
        }
    }

    void synchronize_viewport(const viewport_visual_animation_inputst &input) {
        if (input.viewport == nullptr)
            return;
        viewport_animationst &state = get_viewport(input);
        state.seen = true;

        if (!input.valid()) {
            reset_tracking(state);
            state.has_context = false;
            state.has_mirrored = false;
            return;
        }

        // Only a replaced view leaves a previous buffer belonging somewhere else; a
        // first sighting does not.
        const bool view_switched =
            state.has_context &&
            (state.context_revision != input.context_revision || state.dimensions != input.dimensions);
        const bool context_changed = !state.has_context || view_switched;
        // The scroll delta is queued here as a hint; the buffers are
        // hypothesis-tested each frame to find where it lands. Detection stays
        // suppressed until then: a shifted buffer makes every panned creature look
        // like a real move.
        if (state.has_pan && state.pan != input.pan) {
            if (state.pending.size() >= max_pending_shifts) {
                // Same give-up as the other two sites: the owed shifts are unknowable
                // now.
                abandon_pending(state);
                reset_facing(state);
            }
            state.pending.emplace_back(input.pan - state.pan);
            state.pending_frames = 0;
            state.suppress_frames = scroll_settle_redraws;
        }
        state.context_revision = input.context_revision;
        state.dimensions = input.dimensions;
        state.has_context = true;
        state.pan = input.pan;
        state.has_pan = true;
        if (state.movement_duration_ms != input.movement_duration_ms) {
            state.movement_cadences.clear();
            state.movement_duration_ms = input.movement_duration_ms;
        }
        if (context_changed) {
            state.facing.assign(size_t(input.dimensions.x) * size_t(input.dimensions.y),
                                int8_t(native_sprite_facing));
            state.has_mirrored = false;
        }
        // This hook runs per frame; the viewport is recomputed only when it
        // changes, and while paused hardly at all. Re-reading a landed scroll steps
        // every sprite by a tile.
        const uint64_t signature = compute_buffer_signature(input);
        const bool buffers_advanced =
            !state.buffer_signature || *state.buffer_signature != signature;
        state.buffer_signature = signature;

        if (context_changed) {
            // Skips the recompute sweep, so clear has_mirrored here or a stale true
            // survives.
            state.has_mirrored = false;
            reset_tracking(state);
            // window_z, zoom and resize change at input time; the buffers cross
            // later. This reset covers only the input frame, not the crossing itself.
            if (view_switched)
                state.previous_view_stale = true;
            return;
        }

        // On the crossing frame `current` is the new view and `previous` the old
        // one, so a sprite on each side, a tile apart, reads as one that moved
        // between them.
        const bool crossed_views = buffers_advanced && state.previous_view_stale;
        if (buffers_advanced)
            state.previous_view_stale = false;
        // The new view is drawn at the current window, so a queued scroll is
        // already in it. Left queued it would never match, and suppress everything
        // until it aged out.
        if (crossed_views)
            clear_pending(state);

        bool translated = false;
        df::coord2d landed_shift(0, 0);
        if (buffers_advanced && !crossed_views && !state.pending.empty()) {
            // Queued scrolls land in order and may coalesce, so each hypothesis is a
            // prefix. Shortest first: over-retiring leaves owed shifts to be read as
            // movement.
            df::coord2d landed(0, 0);
            size_t landed_count = 0;
            bool any_data = false;
            df::coord2d shift(0, 0);
            for (size_t count = 1; count <= state.pending.size(); ++count) {
                shift = shift + state.pending[count - 1];
                // A prefix netting to zero is indistinguishable from "nothing landed
                // yet". Accepting it would retire shifts the buffers have still to
                // apply.
                if (shift == df::coord2d{0, 0})
                    continue;
                const double background_ratio = background_shift_match_ratio(input, shift.x, shift.y);
                const double ratio = background_ratio >= 0.0
                                         ? background_ratio
                                         : shift_match_ratio(input, shift.x, shift.y);
                // Emptiness is per-prefix: a long one can push every sprite out of
                // range while a shorter one still has something to say.
                if (ratio < 0.0)
                    continue;
                any_data = true;
                if (ratio >= (background_ratio >= 0.0 ? min_background_scroll_match_ratio
                                                      : min_scroll_match_ratio)) {
                    landed = shift;
                    landed_count = count;
                    break;
                }
            }
            if (!any_data) {
                // Nothing visible to anchor the test on: nothing to animate either.
                abandon_pending(state);
                reset_facing(state);
            } else if (landed_count > 0) {
                // Re-anchor in-flight movements and drop anything scrolled off-screen.
                const int32_t dwx = landed.x;
                const int32_t dwy = landed.y;
                state.movements.erase(
                    std::remove_if(state.movements.begin(), state.movements.end(),
                                   [&](movementst &movement) {
                                       movement.source.x -= dwx;
                                       movement.source.y -= dwy;
                                       movement.target.x -= dwx;
                                       movement.target.y -= dwy;
                                       return movement.target.x < 0 ||
                                              movement.target.x >= input.dimensions.x ||
                                              movement.target.y < 0 ||
                                              movement.target.y >= input.dimensions.y;
                                   }),
                    state.movements.end());
                state.movement_cadences.erase(
                    std::remove_if(state.movement_cadences.begin(), state.movement_cadences.end(),
                                   [&](movement_cadencest &cadence) {
                                       cadence.tile = cadence.tile - df::coord2d(dwx, dwy);
                                       return cadence.tile.x < 0 ||
                                              cadence.tile.x >= input.dimensions.x ||
                                              cadence.tile.y < 0 ||
                                              cadence.tile.y >= input.dimensions.y;
                                   }),
                    state.movement_cadences.end());
                // Facing describes creatures still on screen, so translate it rather
                // than drop it.
                if (state.facing.size() ==
                    size_t(input.dimensions.x) * size_t(input.dimensions.y)) {
                    std::vector<int8_t> shifted(state.facing.size(), int8_t(native_sprite_facing));
                    for (int32_t x = 0; x < input.dimensions.x; ++x) {
                        const int32_t sx = x + dwx;
                        if (sx < 0 || sx >= input.dimensions.x)
                            continue;
                        for (int32_t y = 0; y < input.dimensions.y; ++y) {
                            const int32_t sy = y + dwy;
                            if (sy < 0 || sy >= input.dimensions.y)
                                continue;
                            shifted[x * input.dimensions.y + y] =
                                state.facing[sx * input.dimensions.y + sy];
                        }
                    }
                    state.facing.swap(shifted);
                }
                state.pending.erase(state.pending.begin(),
                                    state.pending.begin() + std::ptrdiff_t(landed_count));
                state.pending_frames = 0;
                state.pending_age = 0;
                // The scroll is accounted for; the settle window must not block the
                // rebased pass.
                state.suppress_frames = 0;
                landed_shift = landed;
                translated = true;
            } else {
                const double static_background_ratio = background_shift_match_ratio(input, 0, 0);
                const bool buffers_static =
                    static_background_ratio >= 0.0
                        ? static_background_ratio >= min_background_scroll_match_ratio
                        : shift_match_ratio(input, 0, 0) >= min_scroll_match_ratio;
                if (buffers_static && ++state.pending_age <= max_pending_age_frames) {
                // The buffers have not moved yet, so the scroll is still in flight.
                state.pending_frames = 0;
                } else if (++state.pending_frames > max_unmatched_scroll_redraws) {
                // The shift never showed up recognizably: fall back to the safe reset.
                abandon_pending(state);
                // The delta was never identified, so the grid cannot be translated.
                reset_facing(state);
                // It may yet land, so do not resume detection on the very next redraw.
                state.suppress_frames = scroll_settle_redraws;
                }
            }
        }

        const bool suppress = !buffers_advanced || crossed_views || !state.pending.empty() ||
                              state.suppress_frames > 0;
        // The countdown measures redraws, not frames, so a repeated viewport must
        // not spend it.
        if (buffers_advanced && state.suppress_frames > 0)
            --state.suppress_frames;

        // On the landing frame `previous` is still framed on the pre-scroll view.
        // Rebasing it by the landed delta keeps a creature that walked during the
        // scroll.
        auto previous_layers = input.previous;
        std::vector<std::vector<int32_t>> rebased_previous;
        if (translated && !suppress) {
            rebased_previous.resize(input.previous.size());
            for (size_t layer = 0; layer < input.previous.size(); ++layer) {
                if (!visual_layer_tracks_own_movement(static_cast<viewport_visual_layer>(layer)))
                    continue;
                rebased_previous[layer].assign(
                    size_t(input.dimensions.x) * size_t(input.dimensions.y), 0);
                for (int32_t x = 0; x < input.dimensions.x; ++x) {
                    const int32_t sx = x + landed_shift.x;
                    if (sx < 0 || sx >= input.dimensions.x)
                        continue;
                    for (int32_t y = 0; y < input.dimensions.y; ++y) {
                        const int32_t sy = y + landed_shift.y;
                        if (sy < 0 || sy >= input.dimensions.y)
                            continue;
                        rebased_previous[layer][x * input.dimensions.y + y] =
                            input.previous[layer][sx * input.dimensions.y + sy];
                    }
                }
                previous_layers[layer] = rebased_previous[layer].data();
            }
        }
        if (!suppress) {
            const int32_t tile_count = input.dimensions.x * input.dimensions.y;
            std::vector<uint8_t> claimed_sources(tile_count);
            const size_t existing_movement_count = state.movements.size();
            const std::vector<movement_cadencest> cadences_at_frame_start =
                state.movement_cadences;
            std::vector<uint8_t> claimed_cadences(cadences_at_frame_start.size());
            std::vector<movement_cadencest> new_cadences;
            long double best_follow_distance = std::numeric_limits<long double>::max();
            if (translated)
                state.follow_movement_id = 0;
            // A chained movement's source may already have been rewritten this frame.
            const std::vector<int8_t> facing_at_frame_start = state.facing;
            // Source clears are deferred until every movement this frame is
            // registered. A source can be another movement's target in the same frame
            // -- a chain.
            std::vector<uint8_t> facing_target_written;
            std::vector<int32_t> pending_facing_source_clears;
            if (state.facing.size() == size_t(input.dimensions.x) * size_t(input.dimensions.y))
                facing_target_written.assign(state.facing.size(), 0);
            for (size_t layer = 0; layer < input.current.size(); ++layer) {
                if (!visual_layer_tracks_own_movement(static_cast<viewport_visual_layer>(layer)))
                    continue;
                std::fill(claimed_sources.begin(), claimed_sources.end(), 0);
                const int32_t *current = input.current[layer];
                const int32_t *previous = previous_layers[layer];
                const auto shared_delta =
                    static_cast<viewport_visual_layer>(layer) == viewport_visual_layer::center
                        ? shared_movement_delta(
                              std::span(current, size_t(tile_count)),
                              std::span(previous, size_t(tile_count)), input.dimensions.x,
                              input.dimensions.y)
                        : df::coord2d(0, 0);
                for (int32_t x = 0; x < input.dimensions.x; ++x) {
                    for (int32_t y = 0; y < input.dimensions.y; ++y) {
                        const int32_t target = x * input.dimensions.y + y;
                        const int32_t texpos = current[target];
                        if (texpos == 0)
                            continue;
                        if (static_cast<viewport_visual_layer>(layer) ==
                                viewport_visual_layer::item &&
                            previous_layers[static_cast<size_t>(viewport_visual_layer::center)]
                                           [target] != 0)
                            continue;

                        int32_t source = -1;
                        int32_t candidate_count = 0;
                        if (shared_delta.x != 0 || shared_delta.y != 0) {
                            const int32_t source_x = x + shared_delta.x;
                            const int32_t source_y = y + shared_delta.y;
                            if (source_x >= 0 && source_x < input.dimensions.x && source_y >= 0 &&
                                source_y < input.dimensions.y) {
                                const int32_t candidate = source_x * input.dimensions.y + source_y;
                                if (!claimed_sources[candidate] && previous[target] != texpos &&
                                    previous[candidate] == texpos) {
                                    source = candidate;
                                    candidate_count = 1;
                                }
                            }
                        }
                        // Otherwise require a unique same-sprite move between empty cells.
                        if (candidate_count == 0 && previous[target] == 0) {
                            for (int32_t dx = -1; dx <= 1; ++dx) {
                                for (int32_t dy = -1; dy <= 1; ++dy) {
                                    if (dx == 0 && dy == 0)
                                        continue;
                                    const int32_t source_x = x + dx;
                                    const int32_t source_y = y + dy;
                                    if (source_x < 0 || source_x >= input.dimensions.x ||
                                        source_y < 0 || source_y >= input.dimensions.y)
                                        continue;
                                    const int32_t candidate =
                                        source_x * input.dimensions.y + source_y;
                                    if (!claimed_sources[candidate] &&
                                        visual_layer_matches(
                                            static_cast<viewport_visual_layer>(layer), texpos,
                                            previous[candidate]) &&
                                        current[candidate] == 0) {
                                        source = candidate;
                                        ++candidate_count;
                                    }
                                }
                            }
                        }
                        if (candidate_count != 1)
                            continue;

                        claimed_sources[source] = 1;
                        DFHack::Coord2d<float> visual_source{
                            float(source / input.dimensions.y), float(source % input.dimensions.y)};
                        uint32_t duration_ms = state.movement_duration_ms;
                        size_t cadence_source = 0;
                        int32_t cadence_count = 0;
                        const df::coord2d source_tile(source / input.dimensions.y,
                                                      source % input.dimensions.y);
                        for (size_t i = 0; i < cadences_at_frame_start.size(); ++i) {
                            const movement_cadencest &cadence = cadences_at_frame_start[i];
                            if (claimed_cadences[i] || cadence.layer !=
                                                          static_cast<viewport_visual_layer>(layer) ||
                                cadence.tile != source_tile || previous[source] == 0 ||
                                !visual_layer_matches(cadence.layer, previous[source], cadence.texpos))
                                continue;
                            cadence_source = i;
                            ++cadence_count;
                        }
                        if (cadence_count == 1) {
                            claimed_cadences[cadence_source] = 1;
                            const uint32_t interval_ms =
                                frame_time_ms - cadences_at_frame_start[cadence_source].last_move_time_ms;
                            if (valid_movement_cadence(interval_ms, state.movement_duration_ms))
                                duration_ms = interval_ms;
                        }
                        new_cadences.push_back({static_cast<viewport_visual_layer>(layer), texpos,
                                                df::coord2d(x, y), frame_time_ms});
                        for (size_t i = 0; i < existing_movement_count; ++i) {
                            const movementst &movement = state.movements[i];
                            if (movement.layer != static_cast<viewport_visual_layer>(layer) ||
                                movement.target.x != visual_source.x ||
                                movement.target.y != visual_source.y)
                                continue;
                            const float progress =
                                movement_progress(movement.start_time_ms, movement.duration_ms);
                            visual_source = movement.source.lerp(
                                {float(movement.target.x), float(movement.target.y)}, progress);
                            break;
                        }
                        const uint64_t movement_id = next_movement_id++;
                        state.movements.push_back({movement_id,
                                                   static_cast<viewport_visual_layer>(layer),
                                                   texpos,
                                                   visual_source,
                                                   df::coord2d(x, y),
                                                   frame_time_ms,
                                                   duration_ms});
                        if (translated &&
                            static_cast<viewport_visual_layer>(layer) ==
                                viewport_visual_layer::center &&
                            df::coord2d(x - source_tile.x, y - source_tile.y) == landed_shift) {
                            const long double centered_x =
                                static_cast<long double>(2) * x - (input.dimensions.x - 1);
                            const long double centered_y =
                                static_cast<long double>(2) * y - (input.dimensions.y - 1);
                            const long double distance = centered_x * centered_x + centered_y * centered_y;
                            if (distance < best_follow_distance) {
                                best_follow_distance = distance;
                                state.follow_movement_id = movement_id;
                            }
                        }
                        if (static_cast<viewport_visual_layer>(layer) ==
                                viewport_visual_layer::center &&
                            state.facing.size() ==
                                size_t(input.dimensions.x) * size_t(input.dimensions.y) &&
                            !facing_at_frame_start.empty() &&
                            facing_at_frame_start.size() == state.facing.size()) {
                            const int32_t source_tile_x = source / input.dimensions.y;
                            const int32_t target_index = x * input.dimensions.y + y;
                            state.facing[target_index] = int8_t(facing_after_move(
                                x - source_tile_x,
                                static_cast<visual_facingst>(facing_at_frame_start[source])));
                            facing_target_written[size_t(target_index)] = 1;
                            pending_facing_source_clears.push_back(source);
                        }
                    }
                }
            }
            // A source vacates its tile only if no movement this frame claimed it as
            // a target.
            if (!facing_target_written.empty()) {
                for (int32_t pending_source : pending_facing_source_clears) {
                    if (!facing_target_written[size_t(pending_source)])
                        state.facing[size_t(pending_source)] = int8_t(native_sprite_facing);
                }
            }
            std::vector<movement_cadencest> retained_cadences;
            retained_cadences.reserve(cadences_at_frame_start.size() + new_cadences.size());
            for (size_t i = 0; i < cadences_at_frame_start.size(); ++i) {
                if (claimed_cadences[i])
                    continue;
                const movement_cadencest &cadence = cadences_at_frame_start[i];
                const size_t layer = static_cast<size_t>(cadence.layer);
                const int32_t tile = cadence.tile.x * input.dimensions.y + cadence.tile.y;
                if (!valid_movement_cadence(frame_time_ms - cadence.last_move_time_ms,
                                            state.movement_duration_ms) ||
                    input.current[layer][tile] == 0 ||
                    !visual_layer_matches(cadence.layer, input.current[layer][tile], cadence.texpos))
                    continue;
                retained_cadences.push_back(cadence);
            }
            for (const movement_cadencest &cadence : new_cadences) {
                retained_cadences.erase(
                    std::remove_if(retained_cadences.begin(), retained_cadences.end(),
                                   [&](const movement_cadencest &existing) {
                                       return existing.layer == cadence.layer &&
                                              existing.tile == cadence.tile;
                                   }),
                    retained_cadences.end());
                retained_cadences.push_back(cadence);
            }
            state.movement_cadences = std::move(retained_cadences);
        }
        state.movements.erase(
            std::remove_if(
                state.movements.begin(), state.movements.end(),
                [&](const movementst &movement) {
                    const size_t layer = static_cast<size_t>(movement.layer);
                    const int32_t target =
                        movement.target.x * input.dimensions.y + movement.target.y;
                    const int32_t current = input.current[layer][target];
                    return frame_time_ms - movement.start_time_ms >= movement.duration_ms ||
                           current == 0 ||
                           !visual_layer_matches(movement.layer, current, movement.texpos);
                }),
            state.movements.end());
        // has_mirrored is recomputed here rather than maintained at every write
        // site.
        if (state.facing.size() == size_t(input.dimensions.x) * size_t(input.dimensions.y)) {
            const int32_t *center_current =
                input.current[static_cast<size_t>(viewport_visual_layer::center)];
            bool any_mirrored = false;
            for (size_t i = 0; i < state.facing.size(); ++i) {
                if (center_current[i] == 0)
                    state.facing[i] = int8_t(native_sprite_facing);
                else if (state.facing[i] != int8_t(native_sprite_facing))
                    any_mirrored = true;
            }
            state.has_mirrored = any_mirrored;
        }
        if (!state.movements.empty())
            force_full_redraw = true;
    }

    void end_frame() {
        viewports.erase(
            std::remove_if(viewports.begin(), viewports.end(),
                           [](const viewport_animationst &state) { return !state.seen; }),
            viewports.end());
        for (const viewport_animationst &state : viewports) {
            if (!state.movements.empty())
                force_full_redraw = true;
        }
    }

    uint32_t get_frame_time_ms() const { return frame_time_ms; }

    uint32_t get_frame_delta_ms() const { return frame_delta_ms; }

    visual_facingst get_facing(const void *viewport, df::coord2d pos) const {
        for (const viewport_animationst &state : viewports) {
            if (state.viewport != viewport)
                continue;
            if (pos.x < 0 || pos.x >= state.dimensions.x || pos.y < 0 ||
                pos.y >= state.dimensions.y)
                break;
            const size_t index = size_t(pos.x) * size_t(state.dimensions.y) + size_t(pos.y);
            if (index >= state.facing.size())
                break;
            return static_cast<visual_facingst>(state.facing[index]);
        }
        return native_sprite_facing;
    }

    visual_facingst get_facing(const void *viewport, int32_t x, int32_t y) const {
        return get_facing(viewport, df::coord2d(x, y));
    }

    bool has_mirrored_facing(const void *viewport) const {
        for (const viewport_animationst &state : viewports) {
            if (state.viewport != viewport)
                continue;
            return state.has_mirrored;
        }
        return false;
    }

    bool requires_full_redraw() const { return force_full_redraw; }

    bool has_active_movement(const void *viewport) const {
        for (const viewport_animationst &state : viewports) {
            if (state.viewport == viewport)
                return !state.movements.empty();
        }
        return false;
    }

    visual_follow_renderst get_follow(const void *viewport) const {
        for (const viewport_animationst &state : viewports) {
            if (state.viewport != viewport || state.follow_movement_id == 0)
                continue;
            for (const movementst &movement : state.movements) {
                if (movement.id != state.follow_movement_id)
                    continue;
                const float remaining =
                    1.0f - movement_progress(movement.start_time_ms, movement.duration_ms);
                return {true,
                        {(float(movement.target.x) - movement.source.x) * remaining,
                         (float(movement.target.y) - movement.source.y) * remaining}};
            }
        }
        return {};
    }

    visual_movement_renderst get_movement(const void *viewport, viewport_visual_layer layer,
                                          int32_t target_x, int32_t target_y) const {
        for (const viewport_animationst &state : viewports) {
            if (state.viewport != viewport)
                continue;
            const movementst *companion = nullptr;
            bool ambiguous = false;
            for (const movementst &movement : state.movements) {
                if (movement.layer == layer && movement.target.x == target_x &&
                    movement.target.y == target_y) {
                    return {true, movement.source,
                            movement_progress(movement.start_time_ms, movement.duration_ms)};
                }
                if (layer == viewport_visual_layer::vehicle ||
                    layer == viewport_visual_layer::center ||
                    movement.layer != viewport_visual_layer::center ||
                    std::abs(movement.target.x - target_x) > 1 ||
                    std::abs(movement.target.y - target_y) > 1)
                    continue;
                if (companion != nullptr && (companion->source.x - companion->target.x !=
                                                 movement.source.x - movement.target.x ||
                                             companion->source.y - companion->target.y !=
                                                 movement.source.y - movement.target.y ||
                                             companion->start_time_ms != movement.start_time_ms))
                    ambiguous = true;
                else if (companion == nullptr)
                    companion = &movement;
            }
            if (ambiguous)
                return {};
            if (companion != nullptr)
                return {true,
                        {target_x + companion->source.x - companion->target.x,
                         target_y + companion->source.y - companion->target.y},
                        movement_progress(companion->start_time_ms, companion->duration_ms),
                        true};
            break;
        }
        return {};
    }
};

#endif // SMOOTH_MOVEMENT_VISUAL_ANIMATION_H
