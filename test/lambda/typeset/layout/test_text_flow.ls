// Test: Text Flow Engine (Week 3 Implementation)
// Tests text flow, text shaper, and layout algorithms

fn test_text_flow_creation() {
    let ctx = create_context()
    let flow_engine = text_flow_create(ctx)
    
    assert(flow_engine != null, "Text flow engine should be created successfully")
    assert(flow_engine.lambda_context == ctx, "Context should be stored correctly")
    
    text_flow_destroy(flow_engine)
    destroy_context(ctx)
}

fn test_basic_text_flow() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let flow_engine = text_flow_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    // Set up flow context
    let flow_ctx = {
        content_area: {x: 0.0, y: 0.0, width: 200.0, height: 300.0},
        current_x: 0.0,
        current_y: 0.0,
        line_height: 14.0,
        current_line: null,
        current_paragraph: null,
        text_align: TEXT_ALIGN_LEFT,
        word_spacing: 0.0,
        letter_spacing: 0.0,
        paragraph_indent: 0.0,
        paragraph_spacing: 12.0
    }
    
    let text = "This is a sample paragraph that will be flowed into multiple lines within the content area."
    let result = text_flow_layout(flow_engine, flow_ctx, text, string_length(text), font)
    
    assert(result != null, "Text flow should succeed")
    assert(result.line_count > 1, "Should create multiple lines")
    assert(result.total_height > 0, "Should have positive total height")
    
    // Validate that lines fit within content area
    for (i in 0 to result.line_count) {
        let line = result.lines[i]
        assert(line.width <= flow_ctx.content_area.width, "Line should fit within content width")
        assert(line.y_position >= 0, "Line Y position should be valid")
    }
    
    text_flow_result_destroy(result)
    text_flow_destroy(flow_engine)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_text_alignment() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let flow_engine = text_flow_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let flow_ctx = {
        content_area: {x: 0.0, y: 0.0, width: 200.0, height: 300.0},
        current_x: 0.0,
        current_y: 0.0,
        line_height: 14.0,
        current_line: null,
        current_paragraph: null,
        text_align: TEXT_ALIGN_LEFT,
        word_spacing: 0.0,
        letter_spacing: 0.0,
        paragraph_indent: 0.0,
        paragraph_spacing: 12.0
    }
    
    let text = "Short line"
    
    // Test left alignment
    flow_ctx.text_align = TEXT_ALIGN_LEFT
    let left_result = text_flow_layout(flow_engine, flow_ctx, text, string_length(text), font)
    assert(left_result.lines[0].x_position == 0.0, "Left alignment should start at x=0")
    
    // Test center alignment
    flow_ctx.text_align = TEXT_ALIGN_CENTER
    let center_result = text_flow_layout(flow_engine, flow_ctx, text, string_length(text), font)
    assert(center_result.lines[0].x_position > 0.0, "Center alignment should offset text")
    
    // Test right alignment
    flow_ctx.text_align = TEXT_ALIGN_RIGHT
    let right_result = text_flow_layout(flow_engine, flow_ctx, text, string_length(text), font)
    assert(right_result.lines[0].x_position > center_result.lines[0].x_position, 
           "Right alignment should have larger offset than center")
    
    text_flow_result_destroy(left_result)
    text_flow_result_destroy(center_result)
    text_flow_result_destroy(right_result)
    text_flow_destroy(flow_engine)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_text_justification() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let flow_engine = text_flow_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let flow_ctx = {
        content_area: {x: 0.0, y: 0.0, width: 200.0, height: 300.0},
        current_x: 0.0,
        current_y: 0.0,
        line_height: 14.0,
        current_line: null,
        current_paragraph: null,
        text_align: TEXT_ALIGN_JUSTIFY,
        word_spacing: 0.0,
        letter_spacing: 0.0,
        paragraph_indent: 0.0,
        paragraph_spacing: 12.0
    }
    
    let text = "This line should be justified to fill the entire width of the content area"
    let result = text_flow_layout(flow_engine, flow_ctx, text, string_length(text), font)
    
    assert(result != null, "Justified text flow should succeed")
    
    // Check that non-final lines are justified to full width
    for (i in 0 to (result.line_count - 1)) {  // Skip last line
        let line = result.lines[i]
        let tolerance = 2.0  // Allow small tolerance for rounding
        assert(abs(line.width - flow_ctx.content_area.width) <= tolerance,
               "Justified line should fill content width")
    }
    
    text_flow_result_destroy(result)
    text_flow_destroy(flow_engine)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_paragraph_handling() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let flow_engine = text_flow_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let flow_ctx = {
        content_area: {x: 0.0, y: 0.0, width: 200.0, height: 300.0},
        current_x: 0.0,
        current_y: 0.0,
        line_height: 14.0,
        current_line: null,
        current_paragraph: null,
        text_align: TEXT_ALIGN_LEFT,
        word_spacing: 0.0,
        letter_spacing: 0.0,
        paragraph_indent: 20.0,
        paragraph_spacing: 12.0
    }
    
    let text = "First paragraph.\n\nSecond paragraph with more text that spans multiple lines."
    let result = text_flow_layout(flow_engine, flow_ctx, text, string_length(text), font)
    
    assert(result != null, "Paragraph flow should succeed")
    assert(result.paragraph_count >= 2, "Should detect multiple paragraphs")
    
    // Check paragraph spacing
    let first_para_end = result.paragraphs[0].end_y
    let second_para_start = result.paragraphs[1].start_y
    assert(second_para_start > first_para_end, "Paragraphs should be spaced apart")
    
    // Check paragraph indent (first line of second paragraph)
    let second_para_first_line = result.paragraphs[1].first_line_index
    let indented_line = result.lines[second_para_first_line]
    assert(indented_line.x_position >= flow_ctx.paragraph_indent,
           "First line should be indented")
    
    text_flow_result_destroy(result)
    text_flow_destroy(flow_engine)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_word_spacing_and_letter_spacing() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let flow_engine = text_flow_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let base_ctx = {
        content_area: {x: 0.0, y: 0.0, width: 200.0, height: 300.0},
        current_x: 0.0,
        current_y: 0.0,
        line_height: 14.0,
        current_line: null,
        current_paragraph: null,
        text_align: TEXT_ALIGN_LEFT,
        word_spacing: 0.0,
        letter_spacing: 0.0,
        paragraph_indent: 0.0,
        paragraph_spacing: 12.0
    }
    
    let text = "word word word"
    
    // Test normal spacing
    let normal_result = text_flow_layout(flow_engine, base_ctx, text, string_length(text), font)
    let normal_width = normal_result.lines[0].width
    
    // Test increased word spacing
    base_ctx.word_spacing = 3.0
    let word_spaced_result = text_flow_layout(flow_engine, base_ctx, text, string_length(text), font)
    assert(word_spaced_result.lines[0].width > normal_width,
           "Increased word spacing should make line wider")
    
    // Test increased letter spacing
    base_ctx.word_spacing = 0.0
    base_ctx.letter_spacing = 1.0
    let letter_spaced_result = text_flow_layout(flow_engine, base_ctx, text, string_length(text), font)
    assert(letter_spaced_result.lines[0].width > normal_width,
           "Increased letter spacing should make line wider")
    
    text_flow_result_destroy(normal_result)
    text_flow_result_destroy(word_spaced_result)
    text_flow_result_destroy(letter_spaced_result)
    text_flow_destroy(flow_engine)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_text_shaper_integration() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let flow_engine = text_flow_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    // Test complex Unicode text that requires shaping
    let unicode_text = "العربية français 中文 עברית"
    
    let flow_ctx = {
        content_area: {x: 0.0, y: 0.0, width: 300.0, height: 200.0},
        current_x: 0.0,
        current_y: 0.0,
        line_height: 16.0,
        current_line: null,
        current_paragraph: null,
        text_align: TEXT_ALIGN_LEFT,
        word_spacing: 0.0,
        letter_spacing: 0.0,
        paragraph_indent: 0.0,
        paragraph_spacing: 12.0
    }
    
    let result = text_flow_layout(flow_engine, flow_ctx, unicode_text, string_byte_length(unicode_text), font)
    
    assert(result != null, "Unicode text flow should succeed")
    assert(result.line_count > 0, "Should produce lines for Unicode text")
    assert(result.total_width > 0, "Should have positive width for shaped text")
    
    text_flow_result_destroy(result)
    text_flow_destroy(flow_engine)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_flow_element_management() {
    let ctx = create_context()
    let flow_engine = text_flow_create(ctx)
    
    // Test flow element creation and management
    let element = flow_element_create(ctx, FLOW_ELEMENT_TEXT)
    assert(element != null, "Flow element should be created")
    assert(element.type == FLOW_ELEMENT_TEXT, "Element type should be set correctly")
    
    // Test adding element to flow
    flow_add_element(flow_engine, element)
    assert(flow_engine.element_count == 1, "Element should be added to flow")
    
    // Test element removal
    flow_remove_element(flow_engine, element)
    assert(flow_engine.element_count == 0, "Element should be removed from flow")
    
    flow_element_destroy(element)
    text_flow_destroy(flow_engine)
    destroy_context(ctx)
}

fn test_flow_performance() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let flow_engine = text_flow_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let flow_ctx = {
        content_area: {x: 0.0, y: 0.0, width: 400.0, height: 600.0},
        current_x: 0.0,
        current_y: 0.0,
        line_height: 14.0,
        current_line: null,
        current_paragraph: null,
        text_align: TEXT_ALIGN_LEFT,
        word_spacing: 0.0,
        letter_spacing: 0.0,
        paragraph_indent: 0.0,
        paragraph_spacing: 12.0
    }
    
    // Generate large text for performance testing
    let large_text = ""
    for (i in 0 to 100) {
        large_text = large_text + "This is sentence " + string(i) + " in a large document. "
    }
    
    let start_time = current_time_millis()
    let result = text_flow_layout(flow_engine, flow_ctx, large_text, string_length(large_text), font)
    let duration = current_time_millis() - start_time
    
    assert(result != null, "Large text flow should succeed")
    assert(duration < 1000, "Flow should complete in reasonable time")  // Less than 1 second
    print("Flow performance: " + string(duration) + "ms for " + string(string_length(large_text)) + " characters")
    
    text_flow_result_destroy(result)
    text_flow_destroy(flow_engine)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

// Run all text flow tests
fn run_text_flow_tests() {
    print("Running Text Flow Tests...")
    
    test_text_flow_creation()
    print("✓ Text flow creation")
    
    test_basic_text_flow()
    print("✓ Basic text flow")
    
    test_text_alignment()
    print("✓ Text alignment")
    
    test_text_justification()
    print("✓ Text justification")
    
    test_paragraph_handling()
    print("✓ Paragraph handling")
    
    test_word_spacing_and_letter_spacing()
    print("✓ Word and letter spacing")
    
    test_text_shaper_integration()
    print("✓ Text shaper integration")
    
    test_flow_element_management()
    print("✓ Flow element management")
    
    test_flow_performance()
    print("✓ Flow performance")
    
    print("All text flow tests passed! ✅")
}

// Execute tests
run_text_flow_tests()
