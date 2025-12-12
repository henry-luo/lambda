/**
 * Unit tests for enhanced grid layout types and algorithms
 *
 * Tests cover:
 * - Coordinate system conversions (GridLine, OriginZeroLine, TrackCounts)
 * - CellOccupancyMatrix operations
 * - EnhancedGridTrack and TrackSizingFunction
 * - Track sizing algorithm
 * - Auto-placement algorithm
 */

#include <gtest/gtest.h>
#include "../radiant/grid_types.hpp"
#include "../radiant/grid_occupancy.hpp"
#include "../radiant/grid_track.hpp"
#include "../radiant/grid_sizing_algorithm.hpp"
#include "../radiant/grid_placement.hpp"

using namespace radiant::grid;

// ============================================================================
// Coordinate System Tests
// ============================================================================

class GridCoordinatesTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(GridCoordinatesTest, OriginZeroLineArithmetic) {
    OriginZeroLine line1(3);
    OriginZeroLine line2(5);

    // Addition
    OriginZeroLine sum = line1 + line2;
    EXPECT_EQ(sum.value, 8);

    // Subtraction
    OriginZeroLine diff = line2 - line1;
    EXPECT_EQ(diff.value, 2);

    // Addition with uint16_t
    OriginZeroLine plus3 = line1 + static_cast<uint16_t>(3);
    EXPECT_EQ(plus3.value, 6);

    // Compound assignment
    OriginZeroLine line3(2);
    line3 += static_cast<uint16_t>(4);
    EXPECT_EQ(line3.value, 6);
}

TEST_F(GridCoordinatesTest, OriginZeroLineComparison) {
    OriginZeroLine line1(3);
    OriginZeroLine line2(5);
    OriginZeroLine line3(3);

    EXPECT_TRUE(line1 < line2);
    EXPECT_TRUE(line2 > line1);
    EXPECT_TRUE(line1 == line3);
    EXPECT_TRUE(line1 != line2);
    EXPECT_TRUE(line1 <= line3);
    EXPECT_TRUE(line1 >= line3);
}

TEST_F(GridCoordinatesTest, OriginZeroLineImpliedTracks) {
    // Positive line - no negative implicit tracks needed
    OriginZeroLine pos(3);
    EXPECT_EQ(pos.implied_negative_implicit_tracks(), 0);
    EXPECT_EQ(pos.implied_positive_implicit_tracks(2), 1); // 3 > 2, so 1 positive implicit

    // Negative line - needs negative implicit tracks
    OriginZeroLine neg(-2);
    EXPECT_EQ(neg.implied_negative_implicit_tracks(), 2);
    EXPECT_EQ(neg.implied_positive_implicit_tracks(5), 0);
}

TEST_F(GridCoordinatesTest, GridLineToOriginZero) {
    uint16_t explicit_tracks = 3; // 4 lines (0, 1, 2, 3 in origin-zero)

    // Positive CSS grid lines (1 = first line)
    GridLine line1(1);
    EXPECT_EQ(line1.into_origin_zero_line(explicit_tracks).value, 0);

    GridLine line2(2);
    EXPECT_EQ(line2.into_origin_zero_line(explicit_tracks).value, 1);

    GridLine line4(4);
    EXPECT_EQ(line4.into_origin_zero_line(explicit_tracks).value, 3);

    // Negative CSS grid lines (-1 = last line)
    GridLine lineN1(-1);
    EXPECT_EQ(lineN1.into_origin_zero_line(explicit_tracks).value, 3); // Last line

    GridLine lineN2(-2);
    EXPECT_EQ(lineN2.into_origin_zero_line(explicit_tracks).value, 2);
}

TEST_F(GridCoordinatesTest, TrackCountsBasics) {
    TrackCounts counts(2, 3, 1); // 2 negative implicit, 3 explicit, 1 positive implicit

    EXPECT_EQ(counts.len(), 6);
    EXPECT_EQ(counts.implicit_start_line().value, -2);
    EXPECT_EQ(counts.implicit_end_line().value, 4); // 3 explicit + 1 positive
}

TEST_F(GridCoordinatesTest, TrackCountsCoordinateConversion) {
    TrackCounts counts(1, 3, 0); // 1 negative implicit, 3 explicit

    // oz_line_to_next_track: OriginZero line -> matrix track index
    EXPECT_EQ(counts.oz_line_to_next_track(OriginZeroLine(-1)), 0); // First track
    EXPECT_EQ(counts.oz_line_to_next_track(OriginZeroLine(0)), 1);
    EXPECT_EQ(counts.oz_line_to_next_track(OriginZeroLine(1)), 2);

    // track_to_prev_oz_line: matrix track index -> OriginZero line
    EXPECT_EQ(counts.track_to_prev_oz_line(0).value, -1);
    EXPECT_EQ(counts.track_to_prev_oz_line(1).value, 0);
    EXPECT_EQ(counts.track_to_prev_oz_line(2).value, 1);
}

TEST_F(GridCoordinatesTest, LineSpan) {
    LineSpan span(OriginZeroLine(1), OriginZeroLine(4));
    EXPECT_EQ(span.span(), 3);

    // Empty span
    LineSpan empty(OriginZeroLine(2), OriginZeroLine(2));
    EXPECT_EQ(empty.span(), 0);

    // Reversed (should return 0)
    LineSpan reversed(OriginZeroLine(5), OriginZeroLine(2));
    EXPECT_EQ(reversed.span(), 0);
}

// ============================================================================
// CellOccupancyMatrix Tests
// ============================================================================

class CellOccupancyMatrixTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(CellOccupancyMatrixTest, BasicCreation) {
    TrackCounts cols(0, 3, 0); // 3 columns
    TrackCounts rows(0, 2, 0); // 2 rows

    CellOccupancyMatrix matrix(cols, rows);

    EXPECT_EQ(matrix.rows(), 2);
    EXPECT_EQ(matrix.cols(), 3);

    // All cells should be unoccupied
    for (size_t r = 0; r < matrix.rows(); ++r) {
        for (size_t c = 0; c < matrix.cols(); ++c) {
            EXPECT_EQ(matrix.get(r, c), CellOccupancyState::Unoccupied);
        }
    }
}

TEST_F(CellOccupancyMatrixTest, SetAndGet) {
    TrackCounts cols(0, 3, 0);
    TrackCounts rows(0, 3, 0);

    CellOccupancyMatrix matrix(cols, rows);

    matrix.set(0, 0, CellOccupancyState::DefinitelyPlaced);
    matrix.set(1, 1, CellOccupancyState::AutoPlaced);

    EXPECT_EQ(matrix.get(0, 0), CellOccupancyState::DefinitelyPlaced);
    EXPECT_EQ(matrix.get(1, 1), CellOccupancyState::AutoPlaced);
    EXPECT_EQ(matrix.get(2, 2), CellOccupancyState::Unoccupied);
}

TEST_F(CellOccupancyMatrixTest, MarkArea) {
    TrackCounts cols(0, 4, 0);
    TrackCounts rows(0, 4, 0);

    CellOccupancyMatrix matrix(cols, rows);

    // Mark a 2x2 area starting at (1,1)
    LineSpan col_span(OriginZeroLine(1), OriginZeroLine(3)); // columns 1-2
    LineSpan row_span(OriginZeroLine(1), OriginZeroLine(3)); // rows 1-2

    matrix.mark_area_as(
        AbsoluteAxis::Horizontal,
        col_span,
        row_span,
        CellOccupancyState::DefinitelyPlaced
    );

    // Check marked cells
    EXPECT_EQ(matrix.get(1, 1), CellOccupancyState::DefinitelyPlaced);
    EXPECT_EQ(matrix.get(1, 2), CellOccupancyState::DefinitelyPlaced);
    EXPECT_EQ(matrix.get(2, 1), CellOccupancyState::DefinitelyPlaced);
    EXPECT_EQ(matrix.get(2, 2), CellOccupancyState::DefinitelyPlaced);

    // Check unmarked cells
    EXPECT_EQ(matrix.get(0, 0), CellOccupancyState::Unoccupied);
    EXPECT_EQ(matrix.get(0, 1), CellOccupancyState::Unoccupied);
    EXPECT_EQ(matrix.get(3, 3), CellOccupancyState::Unoccupied);
}

TEST_F(CellOccupancyMatrixTest, AreaIsUnoccupied) {
    TrackCounts cols(0, 4, 0);
    TrackCounts rows(0, 4, 0);

    CellOccupancyMatrix matrix(cols, rows);

    // Mark cell (1,1) as occupied
    matrix.set(1, 1, CellOccupancyState::DefinitelyPlaced);

    // Area that doesn't include (1,1) should be unoccupied
    LineSpan col_span(OriginZeroLine(2), OriginZeroLine(4));
    LineSpan row_span(OriginZeroLine(2), OriginZeroLine(4));

    EXPECT_TRUE(matrix.line_area_is_unoccupied(AbsoluteAxis::Horizontal, col_span, row_span));

    // Area that includes (1,1) should be occupied
    LineSpan col_span2(OriginZeroLine(0), OriginZeroLine(2));
    LineSpan row_span2(OriginZeroLine(0), OriginZeroLine(2));

    EXPECT_FALSE(matrix.line_area_is_unoccupied(AbsoluteAxis::Horizontal, col_span2, row_span2));
}

TEST_F(CellOccupancyMatrixTest, RowAndColumnOccupancy) {
    TrackCounts cols(0, 3, 0);
    TrackCounts rows(0, 3, 0);

    CellOccupancyMatrix matrix(cols, rows);

    // Initially no rows/columns are occupied
    EXPECT_FALSE(matrix.row_is_occupied(0));
    EXPECT_FALSE(matrix.column_is_occupied(0));

    // Mark a cell
    matrix.set(1, 2, CellOccupancyState::AutoPlaced);

    EXPECT_TRUE(matrix.row_is_occupied(1));
    EXPECT_TRUE(matrix.column_is_occupied(2));
    EXPECT_FALSE(matrix.row_is_occupied(0));
    EXPECT_FALSE(matrix.column_is_occupied(0));
}

// ============================================================================
// GridTrack and TrackSizingFunction Tests
// ============================================================================

class GridTrackTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(GridTrackTest, MinTrackSizingFunction) {
    auto auto_fn = MinTrackSizingFunction::Auto();
    EXPECT_TRUE(auto_fn.is_intrinsic());
    EXPECT_FALSE(auto_fn.uses_percentage());

    auto length_fn = MinTrackSizingFunction::Length(100.0f);
    EXPECT_FALSE(length_fn.is_intrinsic());
    EXPECT_EQ(length_fn.resolve(500.0f), 100.0f);

    auto percent_fn = MinTrackSizingFunction::Percent(50.0f);
    EXPECT_TRUE(percent_fn.uses_percentage());
    EXPECT_EQ(percent_fn.resolve(200.0f), 100.0f);
}

TEST_F(GridTrackTest, MaxTrackSizingFunction) {
    auto fr_fn = MaxTrackSizingFunction::Fr(2.0f);
    EXPECT_TRUE(fr_fn.is_fr());
    EXPECT_EQ(fr_fn.flex_factor(), 2.0f);

    auto fit_content_fn = MaxTrackSizingFunction::FitContentPx(150.0f);
    EXPECT_TRUE(fit_content_fn.is_fit_content());
    EXPECT_EQ(fit_content_fn.fit_content_limit(500.0f), 150.0f);

    auto fit_content_pct = MaxTrackSizingFunction::FitContentPercent(20.0f);
    EXPECT_EQ(fit_content_pct.fit_content_limit(500.0f), 100.0f);
}

TEST_F(GridTrackTest, TrackSizingFunctionFactories) {
    auto auto_track = TrackSizingFunction::Auto();
    EXPECT_FALSE(auto_track.is_flexible());
    EXPECT_TRUE(auto_track.has_intrinsic_sizing());

    auto fr_track = TrackSizingFunction::Fr(1.5f);
    EXPECT_TRUE(fr_track.is_flexible());
    EXPECT_EQ(fr_track.max.flex_factor(), 1.5f);

    auto fixed_track = TrackSizingFunction::Length(200.0f);
    EXPECT_FALSE(fixed_track.is_flexible());
    EXPECT_FALSE(fixed_track.has_intrinsic_sizing());
}

TEST_F(GridTrackTest, EnhancedGridTrackBasics) {
    EnhancedGridTrack track(
        MinTrackSizingFunction::Auto(),
        MaxTrackSizingFunction::Fr(1.0f)
    );

    EXPECT_TRUE(track.is_flexible());
    EXPECT_TRUE(track.has_intrinsic_sizing_function());
    EXPECT_EQ(track.flex_factor(), 1.0f);
    EXPECT_EQ(track.kind, GridTrackKind::Track);
    EXPECT_FALSE(track.is_collapsed);
}

TEST_F(GridTrackTest, EnhancedGridTrackGutter) {
    auto gutter = EnhancedGridTrack::Gutter(10.0f);

    EXPECT_EQ(gutter.kind, GridTrackKind::Gutter);
    EXPECT_FALSE(gutter.is_flexible());
    EXPECT_EQ(gutter.min_track_sizing_function.resolve(100.0f), 10.0f);
}

TEST_F(GridTrackTest, ScratchValueReset) {
    EnhancedGridTrack track;

    track.base_size_planned_increase = 50.0f;
    track.growth_limit_planned_increase = 30.0f;
    track.infinitely_growable = true;

    track.reset_scratch_values();

    EXPECT_EQ(track.base_size_planned_increase, 0.0f);
    EXPECT_EQ(track.growth_limit_planned_increase, 0.0f);
    EXPECT_FALSE(track.infinitely_growable);
}

// ============================================================================
// Track Sizing Algorithm Tests
// ============================================================================

class TrackSizingAlgorithmTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(TrackSizingAlgorithmTest, InitializeTrackSizes) {
    std::vector<EnhancedGridTrack> tracks;

    // Fixed track
    tracks.push_back(EnhancedGridTrack(
        MinTrackSizingFunction::Length(100.0f),
        MaxTrackSizingFunction::Length(100.0f)
    ));

    // Auto track
    tracks.push_back(EnhancedGridTrack(
        MinTrackSizingFunction::Auto(),
        MaxTrackSizingFunction::Auto()
    ));

    // Fr track
    tracks.push_back(EnhancedGridTrack(
        MinTrackSizingFunction::Auto(),
        MaxTrackSizingFunction::Fr(1.0f)
    ));

    initialize_track_sizes(tracks, 500.0f);

    // Fixed track should have base_size = growth_limit = 100
    EXPECT_EQ(tracks[0].base_size, 100.0f);
    EXPECT_EQ(tracks[0].growth_limit, 100.0f);

    // Auto track should have base_size = 0, growth_limit = infinity
    EXPECT_EQ(tracks[1].base_size, 0.0f);
    EXPECT_TRUE(std::isinf(tracks[1].growth_limit));

    // Fr track should have base_size = 0, growth_limit = infinity
    EXPECT_EQ(tracks[2].base_size, 0.0f);
    EXPECT_TRUE(std::isinf(tracks[2].growth_limit));
}

TEST_F(TrackSizingAlgorithmTest, MaximizeTracks) {
    std::vector<EnhancedGridTrack> tracks;

    // Track with growth limit of 150
    EnhancedGridTrack track1(
        MinTrackSizingFunction::Length(50.0f),
        MaxTrackSizingFunction::Length(150.0f)
    );
    track1.base_size = 50.0f;
    track1.growth_limit = 150.0f;
    tracks.push_back(track1);

    // Track with infinite growth limit
    EnhancedGridTrack track2(
        MinTrackSizingFunction::Length(50.0f),
        MaxTrackSizingFunction::Auto()
    );
    track2.base_size = 50.0f;
    track2.growth_limit = std::numeric_limits<float>::infinity();
    tracks.push_back(track2);

    // Total used: 100, available: 300, free: 200
    // Only track1 has finite growth limit (room = 100)
    maximize_tracks(tracks, 300.0f, 300.0f);

    // track1 should be maximized to its growth limit
    EXPECT_EQ(tracks[0].base_size, 150.0f);
    // track2 base_size unchanged (infinite growth limit)
    EXPECT_EQ(tracks[1].base_size, 50.0f);
}

TEST_F(TrackSizingAlgorithmTest, ExpandFlexibleTracks) {
    std::vector<EnhancedGridTrack> tracks;

    // Fixed 100px track
    EnhancedGridTrack fixed(
        MinTrackSizingFunction::Length(100.0f),
        MaxTrackSizingFunction::Length(100.0f)
    );
    fixed.base_size = 100.0f;
    fixed.growth_limit = 100.0f;
    tracks.push_back(fixed);

    // 1fr track
    EnhancedGridTrack fr1(
        MinTrackSizingFunction::Auto(),
        MaxTrackSizingFunction::Fr(1.0f)
    );
    fr1.base_size = 0.0f;
    fr1.growth_limit = std::numeric_limits<float>::infinity();
    tracks.push_back(fr1);

    // 2fr track
    EnhancedGridTrack fr2(
        MinTrackSizingFunction::Auto(),
        MaxTrackSizingFunction::Fr(2.0f)
    );
    fr2.base_size = 0.0f;
    fr2.growth_limit = std::numeric_limits<float>::infinity();
    tracks.push_back(fr2);

    // Available: 400, fixed uses 100, 300 left for fr tracks
    // 1fr + 2fr = 3fr total, so 1fr = 100, 2fr = 200
    expand_flexible_tracks(tracks, -1, -1, 400.0f);

    EXPECT_EQ(tracks[0].base_size, 100.0f); // Fixed unchanged
    EXPECT_NEAR(tracks[1].base_size, 100.0f, 0.1f); // 1fr
    EXPECT_NEAR(tracks[2].base_size, 200.0f, 0.1f); // 2fr
}

TEST_F(TrackSizingAlgorithmTest, StretchAutoTracks) {
    std::vector<EnhancedGridTrack> tracks;

    // Fixed 100px track
    EnhancedGridTrack fixed(
        MinTrackSizingFunction::Length(100.0f),
        MaxTrackSizingFunction::Length(100.0f)
    );
    fixed.base_size = 100.0f;
    fixed.growth_limit = 100.0f;
    tracks.push_back(fixed);

    // Auto track
    EnhancedGridTrack auto_track(
        MinTrackSizingFunction::Auto(),
        MaxTrackSizingFunction::Auto()
    );
    auto_track.base_size = 50.0f;
    auto_track.growth_limit = std::numeric_limits<float>::infinity();
    tracks.push_back(auto_track);

    // Available: 300, used: 150, free: 150
    // Only 1 auto track, so it gets all 150
    stretch_auto_tracks(tracks, -1, 300.0f);

    EXPECT_EQ(tracks[0].base_size, 100.0f); // Fixed unchanged
    EXPECT_NEAR(tracks[1].base_size, 200.0f, 0.1f); // Auto stretched
}

TEST_F(TrackSizingAlgorithmTest, ComputeTrackOffsets) {
    std::vector<EnhancedGridTrack> tracks;

    EnhancedGridTrack track1;
    track1.base_size = 100.0f;
    tracks.push_back(track1);

    EnhancedGridTrack track2;
    track2.base_size = 150.0f;
    tracks.push_back(track2);

    EnhancedGridTrack track3;
    track3.base_size = 75.0f;
    tracks.push_back(track3);

    compute_track_offsets(tracks, 10.0f); // 10px gap

    EXPECT_EQ(tracks[0].offset, 0.0f);
    EXPECT_EQ(tracks[1].offset, 110.0f);  // 100 + 10 gap
    EXPECT_EQ(tracks[2].offset, 270.0f);  // 100 + 10 + 150 + 10
}

// ============================================================================
// Auto-Placement Algorithm Tests
// ============================================================================

class GridPlacementTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(GridPlacementTest, GridPlacementBasics) {
    auto auto_placement = GridPlacement::Auto(2);
    EXPECT_FALSE(auto_placement.is_definite);
    EXPECT_EQ(auto_placement.get_span(), 2);

    auto line_placement = GridPlacement::FromLines(1, 3);
    EXPECT_TRUE(line_placement.is_definite);
    EXPECT_EQ(line_placement.get_span(), 2);

    auto span_placement = GridPlacement::FromStartSpan(2, 3);
    EXPECT_TRUE(span_placement.is_definite);
    EXPECT_EQ(span_placement.get_span(), 3);
}

TEST_F(GridPlacementTest, PlacementToOriginZero) {
    // Grid with 3 explicit columns
    uint16_t explicit_tracks = 3;

    // Line 1 -> OriginZero 0
    auto placement1 = GridPlacement::FromLines(1, 0);
    LineSpan span1 = placement1.to_origin_zero(explicit_tracks);
    EXPECT_EQ(span1.start.value, 0);

    // Line -1 -> OriginZero 3 (last line)
    auto placementN1 = GridPlacement::FromLines(-1, 0);
    LineSpan spanN1 = placementN1.to_origin_zero(explicit_tracks);
    EXPECT_EQ(spanN1.start.value, 3);
}

TEST_F(GridPlacementTest, PlaceDefiniteItems) {
    TrackCounts cols(0, 4, 0);
    TrackCounts rows(0, 4, 0);
    CellOccupancyMatrix matrix(cols, rows);

    std::vector<GridItemInfo> items;

    // Item 1: Definite position at column 1-3, row 1-2
    GridItemInfo item1;
    item1.item_index = 0;
    item1.column = GridPlacement::FromLines(1, 3);
    item1.row = GridPlacement::FromLines(1, 2);
    items.push_back(item1);

    // Item 2: Definite position at column 3-4, row 2-4
    GridItemInfo item2;
    item2.item_index = 1;
    item2.column = GridPlacement::FromLines(3, 4);
    item2.row = GridPlacement::FromLines(2, 4);
    items.push_back(item2);

    place_grid_items(matrix, items, GridAutoFlow::Row, 4, 4);

    // Check item 1 resolved position
    EXPECT_EQ(items[0].resolved_column.start.value, 0);
    EXPECT_EQ(items[0].resolved_column.end.value, 2);
    EXPECT_EQ(items[0].resolved_row.start.value, 0);
    EXPECT_EQ(items[0].resolved_row.end.value, 1);

    // Check item 2 resolved position
    EXPECT_EQ(items[1].resolved_column.start.value, 2);
    EXPECT_EQ(items[1].resolved_column.end.value, 3);
    EXPECT_EQ(items[1].resolved_row.start.value, 1);
    EXPECT_EQ(items[1].resolved_row.end.value, 3);
}

TEST_F(GridPlacementTest, AutoPlacementRowFlow) {
    TrackCounts cols(0, 3, 0);
    TrackCounts rows(0, 3, 0);
    CellOccupancyMatrix matrix(cols, rows);

    std::vector<GridItemInfo> items;

    // 6 auto-placed items in a 3x3 grid (row flow)
    for (int i = 0; i < 6; ++i) {
        GridItemInfo item;
        item.item_index = i;
        item.column = GridPlacement::Auto();
        item.row = GridPlacement::Auto();
        items.push_back(item);
    }

    place_grid_items(matrix, items, GridAutoFlow::Row, 3, 3);

    // Items should fill row by row:
    // [0, 1, 2]
    // [3, 4, 5]
    EXPECT_EQ(items[0].resolved_column.start.value, 0);
    EXPECT_EQ(items[0].resolved_row.start.value, 0);

    EXPECT_EQ(items[1].resolved_column.start.value, 1);
    EXPECT_EQ(items[1].resolved_row.start.value, 0);

    EXPECT_EQ(items[2].resolved_column.start.value, 2);
    EXPECT_EQ(items[2].resolved_row.start.value, 0);

    EXPECT_EQ(items[3].resolved_column.start.value, 0);
    EXPECT_EQ(items[3].resolved_row.start.value, 1);
}

TEST_F(GridPlacementTest, AutoFlowHelpers) {
    EXPECT_EQ(primary_axis(GridAutoFlow::Row), AbsoluteAxis::Horizontal);
    EXPECT_EQ(primary_axis(GridAutoFlow::Column), AbsoluteAxis::Vertical);
    EXPECT_EQ(primary_axis(GridAutoFlow::RowDense), AbsoluteAxis::Horizontal);
    EXPECT_EQ(primary_axis(GridAutoFlow::ColumnDense), AbsoluteAxis::Vertical);

    EXPECT_FALSE(is_dense(GridAutoFlow::Row));
    EXPECT_FALSE(is_dense(GridAutoFlow::Column));
    EXPECT_TRUE(is_dense(GridAutoFlow::RowDense));
    EXPECT_TRUE(is_dense(GridAutoFlow::ColumnDense));
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
