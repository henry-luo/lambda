#include <gtest/gtest.h>
#include "../radiant/radiant.hpp"
#include "../radiant/event.hpp"

struct FullscreenPlatform {
    RadiantWindowPlacement current = {{40, 60, 800, 600}, false, false};
    RadiantWindowPlacement restored = {};
    bool available = true;
    bool fail = false;
    bool ignore_request = false;
    int calls = 0;
    RadiantFullscreenState* callback_state = nullptr;
    static bool read(void* context, RadiantWindowPlacement* out) {
        FullscreenPlatform* platform = static_cast<FullscreenPlatform*>(context);
        *out = platform->current;
        return platform->available;
    }
    static bool apply(void* context, bool enabled, const RadiantWindowPlacement* restore) {
        FullscreenPlatform* platform = static_cast<FullscreenPlatform*>(context);
        platform->calls++;
        if (platform->callback_state) {
            const RadiantFullscreenBackend callback_backend = {read, apply};
            EXPECT_FALSE(radiant_fullscreen_update(platform->callback_state,
                &callback_backend, platform, true));
            radiant_fullscreen_request(platform->callback_state, !enabled);
        }
        if (platform->fail) return false;
        if (platform->ignore_request) return true;
        if (enabled) {
            platform->current = {{0, 0, 1920, 1080}, false, true};
        } else {
            platform->restored = *restore;
            platform->current = *restore;
        }
        return true;
    }
};
static const RadiantFullscreenBackend fullscreen_test_backend = {
    FullscreenPlatform::read, FullscreenPlatform::apply
};

class WindowFullscreen : public ::testing::Test {
protected:
    RadiantFullscreenState state = {};
    FullscreenPlatform platform;
    void update(bool commit = true) {
        radiant_fullscreen_update(&state, &fullscreen_test_backend, &platform, commit);
    }
    void SetUp() override { update(false); }
};

TEST_F(WindowFullscreen, RequestsWaitForCommitAndCoalesce) {
    EXPECT_TRUE(radiant_fullscreen_request(&state, true));
    update(false);
    EXPECT_FALSE(state.actual);
    EXPECT_EQ(platform.calls, 0);
    EXPECT_TRUE(radiant_fullscreen_request(&state, false));
    update();
    EXPECT_EQ(platform.calls, 0);
    EXPECT_FALSE(state.changed);
    EXPECT_FALSE(state.pending);
}

TEST_F(WindowFullscreen, EnterExitPreservesNormalBoundsAndIsIdempotent) {
    ASSERT_TRUE(radiant_fullscreen_request(&state, true));
    update();
    EXPECT_TRUE(state.actual);
    EXPECT_TRUE(state.changed);
    state.changed = false;
    ASSERT_TRUE(radiant_fullscreen_request(&state, true));
    update();
    EXPECT_EQ(platform.calls, 1);
    EXPECT_FALSE(state.changed);
    ASSERT_TRUE(radiant_fullscreen_request(&state, false));
    update();
    EXPECT_FALSE(state.actual);
    EXPECT_EQ(platform.restored.bounds.x, 40);
    EXPECT_EQ(platform.restored.bounds.y, 60);
    EXPECT_EQ(platform.restored.bounds.width, 800);
    EXPECT_EQ(platform.restored.bounds.height, 600);
    EXPECT_FALSE(platform.restored.maximized);
}

TEST_F(WindowFullscreen, MaximizedWindowKeepsItsNormalRestoreRectangle) {
    platform.current = {{0, 0, 1920, 1040}, true, false};
    update(false);
    ASSERT_TRUE(radiant_fullscreen_request(&state, true));
    update();
    ASSERT_TRUE(radiant_fullscreen_request(&state, false));
    update();
    EXPECT_TRUE(platform.restored.maximized);
    EXPECT_EQ(platform.restored.bounds.x, 40);
    EXPECT_EQ(platform.restored.bounds.width, 800);
}

TEST_F(WindowFullscreen, FailedEnterKeepsActualStateAndAllowsRetry) {
    platform.fail = true;
    ASSERT_TRUE(radiant_fullscreen_request(&state, true));
    update();
    EXPECT_TRUE(state.failed);
    EXPECT_FALSE(state.actual);
    EXPECT_FALSE(state.requested);
    EXPECT_FALSE(state.changed);
    state.failed = false;
    platform.fail = false;
    ASSERT_TRUE(radiant_fullscreen_request(&state, true));
    update();
    EXPECT_TRUE(state.actual);
    EXPECT_FALSE(state.failed);
}

TEST_F(WindowFullscreen, FailedExitKeepsFullscreen) {
    ASSERT_TRUE(radiant_fullscreen_request(&state, true));
    update();
    state.changed = false;
    platform.fail = true;
    ASSERT_TRUE(radiant_fullscreen_request(&state, false));
    update();
    EXPECT_TRUE(state.failed);
    EXPECT_TRUE(state.actual);
    EXPECT_TRUE(state.requested);
    EXPECT_FALSE(state.changed);
}

TEST_F(WindowFullscreen, PlatformAcceptanceDoesNotSubstituteForActualMode) {
    platform.ignore_request = true;
    ASSERT_TRUE(radiant_fullscreen_request(&state, true));
    update();
    EXPECT_TRUE(state.failed);
    EXPECT_FALSE(state.actual);
    EXPECT_FALSE(state.requested);
    EXPECT_FALSE(state.changed);
}

TEST_F(WindowFullscreen, ReentrantPlatformCallbackKeepsTheNewestRequest) {
    platform.callback_state = &state;
    ASSERT_TRUE(radiant_fullscreen_request(&state, true));
    update();
    EXPECT_TRUE(state.actual);
    EXPECT_FALSE(state.requested);
    EXPECT_TRUE(state.pending);
    platform.callback_state = nullptr;
    update();
    EXPECT_FALSE(state.actual);
    EXPECT_FALSE(state.pending);
    EXPECT_EQ(platform.calls, 2);
}

TEST_F(WindowFullscreen, ExternalExitReconcilesWithoutReentering) {
    ASSERT_TRUE(radiant_fullscreen_request(&state, true));
    update();
    state.changed = false;
    platform.current = {{10, 20, 800, 600}, false, false};
    update();
    EXPECT_TRUE(state.changed);
    EXPECT_FALSE(state.actual);
    EXPECT_FALSE(state.requested);
    EXPECT_EQ(platform.calls, 1);
}

TEST_F(WindowFullscreen, UnavailablePlatformRejectsAndCancelsRequests) {
    ASSERT_TRUE(radiant_fullscreen_request(&state, true));
    platform.available = false;
    update();
    EXPECT_FALSE(state.available);
    EXPECT_FALSE(state.pending);
    EXPECT_TRUE(state.failed);
    EXPECT_EQ(platform.calls, 0);
    EXPECT_FALSE(radiant_fullscreen_request(&state, true));
    EXPECT_FALSE(radiant_fullscreen_request(&state, false));
}

TEST(WindowFullscreenGeometry, OverlapUsesScreenCoordinatesIncludingNegativeOrigins) {
    RadiantWindowBounds window = {-200, 20, 800, 600};
    EXPECT_EQ(radiant_fullscreen_overlap(window, {-1920, 0, 1920, 1080}), 120000);
    EXPECT_EQ(radiant_fullscreen_overlap(window, {0, 0, 1920, 1080}), 360000);
    EXPECT_EQ(radiant_fullscreen_overlap(window, {1920, 0, 1920, 1080}), 0);
}

TEST(WindowFullscreenGeometry, DisconnectedDisplayRestoreFitsRemainingWorkArea) {
    RadiantWindowBounds fitted = radiant_fullscreen_fit({3000, -900, 2000, 1200},
                                                       {-1280, 25, 1280, 775});
    EXPECT_EQ(fitted.x, -1280);
    EXPECT_EQ(fitted.y, 25);
    EXPECT_EQ(fitted.width, 1280);
    EXPECT_EQ(fitted.height, 775);
}

TEST(WindowFullscreenKeys, ToggleRepeatAndEscapeUseOneHostGate) {
    UiContext ui = {};
    ui.fullscreen.available = true;
    RdtEvent event = {};
    rdt_event_set_key(&event, RDT_EVENT_KEY_DOWN, GLFW_KEY_F11, 0, 0, 0);
    EXPECT_TRUE(radiant_window_fullscreen_key(&ui, &event));
    EXPECT_TRUE(ui.fullscreen.requested);
    EXPECT_TRUE(radiant_window_fullscreen_key(&ui, &event));
    EXPECT_TRUE(ui.fullscreen.requested);
    event.type = RDT_EVENT_KEY_UP;
    EXPECT_TRUE(radiant_window_fullscreen_key(&ui, &event));
    event.type = RDT_EVENT_KEY_DOWN;
    EXPECT_TRUE(radiant_window_fullscreen_key(&ui, &event));
    EXPECT_FALSE(ui.fullscreen.requested);
    ui.fullscreen.actual = ui.fullscreen.requested = true;
    event.key.key = RDT_KEY_ESCAPE;
    // a fresh Escape press reaches the document first
    EXPECT_FALSE(radiant_window_fullscreen_key(&ui, &event));
    EXPECT_TRUE(ui.fullscreen.requested);
    // a document that prevents it (a deck clearing its blackout) keeps fullscreen
    radiant_window_escape_default(&ui, true);
    EXPECT_TRUE(ui.fullscreen.requested);
    EXPECT_TRUE(ui.fullscreen.escape_page_consumed);
    event.type = RDT_EVENT_KEY_UP;
    EXPECT_FALSE(radiant_window_fullscreen_key(&ui, &event));
    event.type = RDT_EVENT_KEY_DOWN;
    // the next, unprevented press leaves fullscreen
    EXPECT_FALSE(radiant_window_fullscreen_key(&ui, &event));
    EXPECT_FALSE(ui.fullscreen.escape_page_consumed);
    radiant_window_escape_default(&ui, false);
    EXPECT_FALSE(ui.fullscreen.requested);
    ui.fullscreen.actual = false;
    // auto-repeat and release of the press that left fullscreen stay with the host
    EXPECT_TRUE(radiant_window_fullscreen_key(&ui, &event));
    event.type = RDT_EVENT_KEY_UP;
    EXPECT_TRUE(radiant_window_fullscreen_key(&ui, &event));
    event.type = RDT_EVENT_KEY_DOWN;
    EXPECT_FALSE(radiant_window_fullscreen_key(&ui, &event));
}

TEST(WindowFullscreenKeys, WindowlessHostsAndModifiedShortcutsPassThrough) {
    UiContext ui = {};
    RdtEvent event = {};
    rdt_event_set_key(&event, RDT_EVENT_KEY_DOWN, GLFW_KEY_F11, 0, 0, 0);
    EXPECT_FALSE(radiant_window_fullscreen_key(&ui, &event));
    ui.fullscreen.available = true;
    event.key.mods = RDT_MOD_CTRL;
    EXPECT_FALSE(radiant_window_fullscreen_key(&ui, &event));
    EXPECT_FALSE(ui.fullscreen.requested);
}
