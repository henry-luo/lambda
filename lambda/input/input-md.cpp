#include "input.h"
#include "../lambda-data.hpp"

// Forward declaration for math parser integration
void parse_math(Input* input, const char* math_string, const char* flavor);

// Forward declarations
static Item parse_markdown_content(Input *input, char** lines, int line_count);
static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_inline_content(Input *input, const char* text);
static Item parse_table(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_strikethrough(Input *input, const char* text, int* pos);
static Item parse_superscript(Input *input, const char* text, int* pos);
static Item parse_subscript(Input *input, const char* text, int* pos);
static Item parse_math_inline(Input *input, const char* text, int* pos);
static Item parse_math_display(Input *input, const char* text, int* pos);
static Item parse_emoji_shortcode(Input *input, const char* text, int* pos);
static int parse_yaml_frontmatter(Input *input, char** lines, int line_count, Element* meta);
static void parse_yaml_line(Input *input, const char* line, Element* meta);
static bool is_table_row(const char* line);
static bool is_table_separator(const char* line);

// Use common utility functions from input.c
#define skip_whitespace input_skip_whitespace
#define is_whitespace_char input_is_whitespace_char
#define is_empty_line input_is_empty_line
#define count_leading_chars input_count_leading_chars
#define trim_whitespace input_trim_whitespace
#define split_lines input_split_lines
#define free_lines input_free_lines
#define create_markdown_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element
#define add_attribute_item_to_element input_add_attribute_item_to_element

// Helper function to create string from buffer content
static String* create_string_from_buffer(Input* input, const char* text, int start, int len) {
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    strbuf_append_str_n(sb, text + start, len);
    return strbuf_to_string(sb);
}

// Helper function to trim and create string from buffer
static String* create_trimmed_string(Input* input, const char* text) {
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    
    // Skip leading whitespace
    while (*text && (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')) {
        text++;
    }
    
    const char* end = text + strlen(text) - 1;
    // Skip trailing whitespace
    while (end >= text && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    
    // Append trimmed content
    while (text <= end) {
        strbuf_append_char(sb, *text);
        text++;
    }
    
    return strbuf_to_string(sb);
}

// Helper function to increment element content length safely
static void increment_element_content_length(Element* element) {
    if (element && element->type) {
        TypeElmt* elmt_type = (TypeElmt*)element->type;
        elmt_type->content_length++;
    }
}

// Block parsing functions - checking functions remain the same
static bool is_atx_heading(const char* line) {
    int hash_count = count_leading_chars(line, '#');
    return hash_count >= 1 && hash_count <= 6 && 
           (line[hash_count] == '\0' || is_whitespace_char(line[hash_count]));
}

static bool is_thematic_break(const char* line) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    if (!line[pos] || (line[pos] != '-' && line[pos] != '*' && line[pos] != '_')) {
        return false;
    }
    
    char marker = line[pos];
    int count = 0;
    
    while (line[pos]) {
        if (line[pos] == marker) {
            count++;
        } else if (line[pos] != ' ') {
            return false;
        }
        pos++;
    }
    
    return count >= 3;
}

static bool is_fenced_code_block_start(const char* line, char* fence_char, int* fence_length) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    if (line[pos] != '`' && line[pos] != '~') return false;
    
    char local_fence_char = line[pos];
    int local_fence_length = 0;
    while (line[pos + local_fence_length] == local_fence_char) {
        local_fence_length++;
    }
    
    if (local_fence_length >= 3) {
        if (fence_char) *fence_char = local_fence_char;
        if (fence_length) *fence_length = local_fence_length;
        return true;
    }
    
    return false;
}

static bool is_list_marker(const char* line, bool* is_ordered, int* number) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    // Check for unordered list markers
    if (line[pos] == '-' || line[pos] == '+' || line[pos] == '*') {
        *is_ordered = false;
        pos++;
        return line[pos] == '\0' || is_whitespace_char(line[pos]);
    }
    
    // Check for ordered list markers
    if (isdigit(line[pos])) {
        int start_pos = pos;
        int num = 0;
        
        while (isdigit(line[pos]) && pos - start_pos < 9) {
            num = num * 10 + (line[pos] - '0');
            pos++;
        }
        
        if (pos > start_pos && (line[pos] == '.' || line[pos] == ')')) {
            *is_ordered = true;
            *number = num;
            pos++;
            return line[pos] == '\0' || is_whitespace_char(line[pos]);
        }
    }
    
    return false;
}

static bool is_blockquote(const char* line) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    return line[pos] == '>';
}

// Table parsing functions
static bool is_table_row(const char* line) {
    if (!line) return false;
    
    // Skip leading whitespace
    while (*line && isspace(*line)) line++;
    
    // Must start with |
    if (*line != '|') return false;
    
    // Must have at least one more | or end with |
    line++;
    while (*line) {
        if (*line == '|') return true;
        line++;
    }
    
    return false;
}

static bool is_table_separator(const char* line) {
    if (!line) return false;
    
    // Skip leading whitespace
    while (*line && isspace(*line)) line++;
    
    // Must start with |
    if (*line != '|') return false;
    
    line++;
    
    // Check each cell for separator pattern
    while (*line) {
        // Skip whitespace
        while (*line && isspace(*line)) line++;
        
        // Check for alignment indicators
        bool has_left = false, has_right = false;
        
        if (*line == ':') {
            has_left = true;
            line++;
        }
        
        // Must have at least one dash
        if (*line != '-') return false;
        
        while (*line == '-') line++;
        
        if (*line == ':') {
            has_right = true;
            line++;
        }
        
        // Skip whitespace
        while (*line && isspace(*line)) line++;
        
        // Must be | or end of line
        if (*line == '|') {
            line++;
        } else if (*line == '\0') {
            break;
        } else {
            return false;
        }
    }
    
    return true;
}

// Table parsing helper functions
static char** parse_table_alignment(const char* line, int* cell_count) {
    if (!is_table_separator(line)) return NULL;
    
    *cell_count = 0;
    
    // Count cells first
    const char* ptr = line;
    while (*ptr && isspace(*ptr)) ptr++; // Skip leading whitespace
    if (*ptr == '|') ptr++; // Skip leading |
    
    int count = 0;
    bool in_cell = false;
    
    while (*ptr) {
        if (*ptr == '|') {
            if (in_cell) count++;
            in_cell = false;
        } else if (!isspace(*ptr)) {
            in_cell = true;
        }
        ptr++;
    }
    if (in_cell) count++; // Last cell without trailing |
    
    if (count == 0) return NULL;
    
    // Allocate array
    char** alignments = (char**)malloc(count * sizeof(char*));
    *cell_count = count;
    
    // Parse alignments
    ptr = line;
    while (*ptr && isspace(*ptr)) ptr++; // Skip leading whitespace
    if (*ptr == '|') ptr++; // Skip leading |
    
    int cell_index = 0;
    const char* cell_start = ptr;
    
    while (*ptr && cell_index < count) {
        if (*ptr == '|' || *ptr == '\0') {
            // Extract cell content length
            int cell_len = ptr - cell_start;
            
            // Determine alignment - use stack allocation for small strings
            char trimmed_stack[256];
            const char* trimmed;
            if (cell_len < 255) {
                strncpy(trimmed_stack, cell_start, cell_len);
                trimmed_stack[cell_len] = '\0';
                // Simple trim
                char* start = trimmed_stack;
                while (*start && isspace(*start)) start++;
                char* end = trimmed_stack + strlen(trimmed_stack) - 1;
                while (end >= trimmed_stack && isspace(*end)) *end-- = '\0';
                trimmed = start;
            } else {
                // Fall back for very long cells
                char* temp = (char*)malloc(cell_len + 1);
                strncpy(temp, cell_start, cell_len);
                temp[cell_len] = '\0';
                char* trimmed_temp = trim_whitespace(temp);
                free(temp);
                trimmed = trimmed_temp;
            }
            
            bool has_left = (trimmed[0] == ':');
            bool has_right = (strlen(trimmed) > 0 && trimmed[strlen(trimmed) - 1] == ':');
            
            if (has_left && has_right) {
                alignments[cell_index] = strdup("center");
            } else if (has_right) {
                alignments[cell_index] = strdup("right");
            } else {
                alignments[cell_index] = strdup("left");
            }
            
            if (cell_len >= 255 && trimmed != trimmed_stack) {
                free((char*)trimmed);
            }
            cell_index++;
            
            if (*ptr == '|') {
                ptr++;
                cell_start = ptr;
            }
        } else {
            ptr++;
        }
    }
    
    return alignments;
}

static char** parse_table_row(const char* line, int* cell_count) {
    *cell_count = 0;
    if (!is_table_row(line)) return NULL;
    
    // Count cells first
    const char* ptr = line;
    while (*ptr && isspace(*ptr)) ptr++; // Skip leading whitespace
    if (*ptr == '|') ptr++; // Skip leading |
    
    int count = 0;
    bool in_cell = false;
    
    while (*ptr) {
        if (*ptr == '|') {
            if (in_cell) count++;
            in_cell = false;
        } else if (!isspace(*ptr)) {
            in_cell = true;
        }
        ptr++;
    }
    if (in_cell) count++; // Last cell without trailing |
    
    if (count == 0) return NULL;
    
    // Allocate array
    char** cells = (char**)malloc(count * sizeof(char*));
    *cell_count = count;
    
    // Parse cells
    ptr = line;
    while (*ptr && isspace(*ptr)) ptr++; // Skip leading whitespace
    if (*ptr == '|') ptr++; // Skip leading |
    
    int cell_index = 0;
    const char* cell_start = ptr;
    
    while (*ptr && cell_index < count) {
        if (*ptr == '|' || *ptr == '\0') {
            // Extract cell content
            int cell_len = ptr - cell_start;
            char* cell_content = (char*)malloc(cell_len + 1);
            strncpy(cell_content, cell_start, cell_len);
            cell_content[cell_len] = '\0';
            
            // Trim whitespace
            cells[cell_index] = trim_whitespace(cell_content);
            free(cell_content);
            
            cell_index++;
            
            if (*ptr == '|') {
                ptr++;
                cell_start = ptr;
            }
        } else {
            ptr++;
        }
    }
    
    return cells;
}

static void free_table_row(char** cells, int cell_count) {
    for (int i = 0; i < cell_count; i++) {
        free(cells[i]);
    }
    free(cells);
}

// YAML frontmatter functions
static int parse_yaml_frontmatter(Input *input, char** lines, int line_count, Element* meta) {
    if (line_count == 0) return 0;
    
    // Check if first line is YAML frontmatter delimiter using buffer
    String* first_line = create_trimmed_string(input, lines[0]);
    if (!first_line || strcmp(first_line->chars, "---") != 0) {
        return 0;
    }
    
    // Find closing delimiter
    int yaml_end = -1;
    for (int i = 1; i < line_count; i++) {
        String* line = create_trimmed_string(input, lines[i]);
        if (line && (strcmp(line->chars, "---") == 0 || strcmp(line->chars, "...") == 0)) {
            yaml_end = i;
            break;
        }
    }
    
    if (yaml_end == -1) return 0; // No closing delimiter found
    
    // Parse YAML content between delimiters
    for (int i = 1; i < yaml_end; i++) {
        if (lines[i] && !is_empty_line(lines[i])) {
            parse_yaml_line(input, lines[i], meta);
        }
    }
    
    return yaml_end + 1; // Return number of lines consumed
}

static void parse_yaml_line(Input *input, const char* line, Element* meta) {
    // Enhanced YAML key-value parsing using robust buffer functions
    String* line_trimmed = create_trimmed_string(input, line);
    
    // Skip empty lines and comments
    if (!line_trimmed || line_trimmed->len == 0 || line_trimmed->chars[0] == '#') {
        return;
    }
    
    char* colon = strchr(line_trimmed->chars, ':');
    if (!colon) {
        return;
    }
    
    // Extract key using buffer
    int key_len = colon - line_trimmed->chars;
    String* key = create_string_from_buffer(input, line_trimmed->chars, 0, key_len);
    key = create_trimmed_string(input, key->chars);
    
    // Extract value using buffer
    const char* value_start = colon + 1;
    String* value = create_trimmed_string(input, value_start);
    
    if (key && key->len > 0 && value && value->len > 0) {
        // Remove quotes from value if present
        if (value->len >= 2 && 
            ((value->chars[0] == '"' && value->chars[value->len-1] == '"') ||
             (value->chars[0] == '\'' && value->chars[value->len-1] == '\''))) {
            String* unquoted = create_string_from_buffer(input, value->chars, 1, value->len - 2);
            value = unquoted;
        }
        
        // Add as attribute to meta element
        add_attribute_to_element(input, meta, key->chars, value->chars);
    }
}

// Rewritten string-based parsing functions using proper strbuf patterns

// GitHub Emoji Shortcode Mapping
typedef struct {
    const char* shortcode;
    const char* unicode;
} EmojiMapping;

static const EmojiMapping emoji_mappings[] = {
    // Smileys & Emotion
    {":smile:", "😄"},
    {":smiley:", "😃"},
    {":grinning:", "😀"},
    {":blush:", "😊"},
    {":relaxed:", "☺️"},
    {":wink:", "😉"},
    {":heart_eyes:", "😍"},
    {":kissing_heart:", "😘"},
    {":kissing_closed_eyes:", "😚"},
    {":stuck_out_tongue:", "😛"},
    {":stuck_out_tongue_winking_eye:", "😜"},
    {":stuck_out_tongue_closed_eyes:", "😝"},
    {":disappointed:", "😞"},
    {":worried:", "😟"},
    {":angry:", "😠"},
    {":rage:", "😡"},
    {":cry:", "😢"},
    {":persevere:", "😣"},
    {":triumph:", "😤"},
    {":disappointed_relieved:", "😥"},
    {":frowning:", "😦"},
    {":anguished:", "😧"},
    {":fearful:", "😨"},
    {":weary:", "😩"},
    {":sleepy:", "😪"},
    {":tired_face:", "😫"},
    {":grimacing:", "😬"},
    {":sob:", "😭"},
    {":open_mouth:", "😮"},
    {":hushed:", "😯"},
    {":cold_sweat:", "😰"},
    {":scream:", "😱"},
    {":astonished:", "😲"},
    {":flushed:", "😳"},
    {":sleeping:", "😴"},
    {":dizzy_face:", "😵"},
    {":no_mouth:", "😶"},
    {":mask:", "😷"},
    {":sunglasses:", "😎"},
    {":confused:", "😕"},
    {":neutral_face:", "😐"},
    {":expressionless:", "😑"},
    {":unamused:", "😒"},
    {":sweat_smile:", "😅"},
    {":sweat:", "😓"},
    {":joy:", "😂"},
    {":laughing:", "😆"},
    {":innocent:", "😇"},
    {":smiling_imp:", "😈"},
    {":imp:", "👿"},
    {":skull:", "💀"},
    
    // People & Body
    {":wave:", "👋"},
    {":raised_hand:", "✋"},
    {":open_hands:", "👐"},
    {":point_up:", "☝️"},
    {":point_down:", "👇"},
    {":point_left:", "👈"},
    {":point_right:", "👉"},
    {":raised_hands:", "🙌"},
    {":pray:", "🙏"},
    {":clap:", "👏"},
    {":muscle:", "💪"},
    {":walking:", "🚶"},
    {":runner:", "🏃"},
    {":dancer:", "💃"},
    {":ok_hand:", "👌"},
    {":thumbsup:", "👍"},
    {":thumbsdown:", "👎"},
    {":punch:", "👊"},
    {":fist:", "✊"},
    {":v:", "✌️"},
    {":hand:", "✋"},
    
    // Nature
    {":dog:", "🐶"},
    {":cat:", "🐱"},
    {":mouse:", "🐭"},
    {":hamster:", "🐹"},
    {":rabbit:", "🐰"},
    {":bear:", "🐻"},
    {":panda_face:", "🐼"},
    {":koala:", "🐨"},
    {":tiger:", "🐯"},
    {":lion_face:", "🦁"},
    {":cow:", "🐮"},
    {":pig:", "🐷"},
    {":pig_nose:", "🐽"},
    {":frog:", "🐸"},
    {":octopus:", "🐙"},
    {":monkey_face:", "🐵"},
    {":see_no_evil:", "🙈"},
    {":hear_no_evil:", "🙉"},
    {":speak_no_evil:", "🙊"},
    {":monkey:", "🐒"},
    {":chicken:", "🐔"},
    {":penguin:", "🐧"},
    {":bird:", "🐦"},
    {":baby_chick:", "🐤"},
    {":hatched_chick:", "🐣"},
    {":hatching_chick:", "🐣"},
    {":wolf:", "🐺"},
    {":boar:", "🐗"},
    {":horse:", "🐴"},
    {":unicorn:", "🦄"},
    {":bee:", "🐝"},
    {":bug:", "🐛"},
    {":snail:", "🐌"},
    {":beetle:", "🐞"},
    {":ant:", "🐜"},
    {":spider:", "🕷️"},
    {":scorpion:", "🦂"},
    {":crab:", "🦀"},
    {":snake:", "🐍"},
    {":turtle:", "🐢"},
    {":tropical_fish:", "🐠"},
    {":fish:", "🐟"},
    {":blowfish:", "🐡"},
    {":dolphin:", "🐬"},
    {":whale:", "🐳"},
    {":whale2:", "🐋"},
    {":crocodile:", "🐊"},
    {":leopard:", "🐆"},
    {":tiger2:", "🐅"},
    {":water_buffalo:", "🐃"},
    {":ox:", "🐂"},
    {":cow2:", "🐄"},
    {":dromedary_camel:", "🐪"},
    {":camel:", "🐫"},
    {":elephant:", "🐘"},
    {":goat:", "🐐"},
    {":ram:", "🐏"},
    {":sheep:", "🐑"},
    {":racehorse:", "🐎"},
    {":pig2:", "🐖"},
    {":rat:", "🐀"},
    {":mouse2:", "🐁"},
    {":rooster:", "🐓"},
    {":turkey:", "🦃"},
    {":dove:", "🕊️"},
    {":dog2:", "🐕"},
    {":poodle:", "🐩"},
    {":cat2:", "🐈"},
    {":rabbit2:", "🐇"},
    {":chipmunk:", "🐿️"},
    {":feet:", "🐾"},
    {":dragon:", "🐉"},
    {":dragon_face:", "🐲"},
    {":cactus:", "🌵"},
    {":christmas_tree:", "🎄"},
    {":evergreen_tree:", "🌲"},
    {":deciduous_tree:", "🌳"},
    {":palm_tree:", "🌴"},
    {":seedling:", "🌱"},
    {":herb:", "🌿"},
    {":shamrock:", "☘️"},
    {":four_leaf_clover:", "🍀"},
    {":bamboo:", "🎍"},
    {":tanabata_tree:", "🎋"},
    {":leaves:", "🍃"},
    {":fallen_leaf:", "🍂"},
    {":maple_leaf:", "🍁"},
    {":ear_of_rice:", "🌾"},
    {":hibiscus:", "🌺"},
    {":sunflower:", "🌻"},
    {":rose:", "🌹"},
    {":tulip:", "🌷"},
    {":blossom:", "🌼"},
    {":cherry_blossom:", "🌸"},
    {":bouquet:", "💐"},
    {":mushroom:", "🍄"},
    {":chestnut:", "🌰"},
    {":jack_o_lantern:", "🎃"},
    {":shell:", "🐚"},
    {":spider_web:", "🕸️"},
    {":earth_americas:", "🌎"},
    {":earth_africa:", "🌍"},
    {":earth_asia:", "🌏"},
    {":full_moon:", "🌕"},
    {":waning_gibbous_moon:", "🌖"},
    {":last_quarter_moon:", "🌗"},
    {":waning_crescent_moon:", "🌘"},
    {":new_moon:", "🌑"},
    {":waxing_crescent_moon:", "🌒"},
    {":first_quarter_moon:", "🌓"},
    {":moon:", "🌔"},
    {":new_moon_with_face:", "🌚"},
    {":full_moon_with_face:", "🌝"},
    {":first_quarter_moon_with_face:", "🌛"},
    {":last_quarter_moon_with_face:", "🌜"},
    {":sun_with_face:", "🌞"},
    {":crescent_moon:", "🌙"},
    {":star:", "⭐"},
    {":star2:", "🌟"},
    {":dizzy:", "💫"},
    {":sparkles:", "✨"},
    {":comet:", "☄️"},
    {":sunny:", "☀️"},
    {":mostly_sunny:", "🌤️"},
    {":partly_sunny:", "⛅"},
    {":barely_sunny:", "🌦️"},
    {":partly_sunny_rain:", "🌦️"},
    {":cloud:", "☁️"},
    {":rain_cloud:", "🌧️"},
    {":thunder_cloud_rain:", "⛈️"},
    {":lightning:", "🌩️"},
    {":zap:", "⚡"},
    {":fire:", "🔥"},
    {":boom:", "💥"},
    {":snowflake:", "❄️"},
    {":cloud_snow:", "🌨️"},
    {":snowman2:", "⛄"},
    {":snowman:", "☃️"},
    {":wind_blowing_face:", "🌬️"},
    {":dash:", "💨"},
    {":cloud_tornado:", "🌪️"},
    {":fog:", "🌫️"},
    {":umbrella2:", "☂️"},
    {":umbrella:", "☔"},
    {":droplet:", "💧"},
    {":sweat_drops:", "💦"},
    {":ocean:", "🌊"},
    
    // Food & Drink
    {":green_apple:", "🍏"},
    {":apple:", "🍎"},
    {":pear:", "🍐"},
    {":tangerine:", "🍊"},
    {":lemon:", "🍋"},
    {":banana:", "🍌"},
    {":watermelon:", "🍉"},
    {":grapes:", "🍇"},
    {":strawberry:", "🍓"},
    {":melon:", "🍈"},
    {":cherries:", "🍒"},
    {":peach:", "🍑"},
    {":pineapple:", "🍍"},
    {":tomato:", "🍅"},
    {":eggplant:", "🍆"},
    {":hot_pepper:", "🌶️"},
    {":corn:", "🌽"},
    {":sweet_potato:", "🍠"},
    {":honey_pot:", "🍯"},
    {":bread:", "🍞"},
    {":cheese:", "🧀"},
    {":poultry_leg:", "🍗"},
    {":meat_on_bone:", "🍖"},
    {":fried_shrimp:", "🍤"},
    {":egg:", "🥚"},
    {":hamburger:", "🍔"},
    {":fries:", "🍟"},
    {":hotdog:", "🌭"},
    {":pizza:", "🍕"},
    {":spaghetti:", "🍝"},
    {":taco:", "🌮"},
    {":burrito:", "🌯"},
    {":ramen:", "🍜"},
    {":stew:", "🍲"},
    {":fish_cake:", "🍥"},
    {":sushi:", "🍣"},
    {":bento:", "🍱"},
    {":curry:", "🍛"},
    {":rice_ball:", "🍙"},
    {":rice:", "🍚"},
    {":rice_cracker:", "🍘"},
    {":oden:", "🍢"},
    {":dango:", "🍡"},
    {":shaved_ice:", "🍧"},
    {":ice_cream:", "🍨"},
    {":icecream:", "🍦"},
    {":cake:", "🍰"},
    {":birthday:", "🎂"},
    {":custard:", "🍮"},
    {":candy:", "🍬"},
    {":lollipop:", "🍭"},
    {":chocolate_bar:", "🍫"},
    {":popcorn:", "🍿"},
    {":doughnut:", "🍩"},
    {":cookie:", "🍪"},
    {":beer:", "🍺"},
    {":beers:", "🍻"},
    {":wine_glass:", "🍷"},
    {":cocktail:", "🍸"},
    {":tropical_drink:", "🍹"},
    {":champagne:", "🍾"},
    {":sake:", "🍶"},
    {":tea:", "🍵"},
    {":coffee:", "☕"},
    {":baby_bottle:", "🍼"},
    {":milk:", "🥛"},
    
    // Activities
    {":soccer:", "⚽"},
    {":basketball:", "🏀"},
    {":football:", "🏈"},
    {":baseball:", "⚾"},
    {":tennis:", "🎾"},
    {":volleyball:", "🏐"},
    {":rugby_football:", "🏉"},
    {":8ball:", "🎱"},
    {":golf:", "⛳"},
    {":golfer:", "🏌️"},
    {":ping_pong:", "🏓"},
    {":badminton:", "🏸"},
    {":hockey:", "🏒"},
    {":field_hockey:", "🏑"},
    {":cricket:", "🏏"},
    {":ski:", "🎿"},
    {":skier:", "⛷️"},
    {":snowboarder:", "🏂"},
    {":ice_skate:", "⛸️"},
    {":bow_and_arrow:", "🏹"},
    {":fishing_pole_and_fish:", "🎣"},
    {":rowboat:", "🚣"},
    {":swimmer:", "🏊"},
    {":surfer:", "🏄"},
    {":bath:", "🛀"},
    {":basketball_player:", "⛹️"},
    {":lifter:", "🏋️"},
    {":bicyclist:", "🚴"},
    {":mountain_bicyclist:", "🚵"},
    {":horse_racing:", "🏇"},
    {":levitate:", "🕴️"},
    {":trophy:", "🏆"},
    {":running_shirt_with_sash:", "🎽"},
    {":medal:", "🏅"},
    {":military_medal:", "🎖️"},
    {":reminder_ribbon:", "🎗️"},
    {":rosette:", "🏵️"},
    {":ticket:", "🎫"},
    {":admission_tickets:", "🎟️"},
    {":performing_arts:", "🎭"},
    {":art:", "🎨"},
    {":circus_tent:", "🎪"},
    {":microphone:", "🎤"},
    {":headphones:", "🎧"},
    {":musical_score:", "🎼"},
    {":musical_keyboard:", "🎹"},
    {":saxophone:", "🎷"},
    {":trumpet:", "🎺"},
    {":guitar:", "🎸"},
    {":violin:", "🎻"},
    {":clapper:", "🎬"},
    {":video_game:", "🎮"},
    {":space_invader:", "👾"},
    {":dart:", "🎯"},
    {":game_die:", "🎲"},
    {":slot_machine:", "🎰"},
    {":bowling:", "🎳"},
    
    // Travel & Places
    {":red_car:", "🚗"},
    {":taxi:", "🚕"},
    {":blue_car:", "🚙"},
    {":bus:", "🚌"},
    {":trolleybus:", "🚎"},
    {":race_car:", "🏎️"},
    {":police_car:", "🚓"},
    {":ambulance:", "🚑"},
    {":fire_engine:", "🚒"},
    {":minibus:", "🚐"},
    {":truck:", "🚚"},
    {":articulated_lorry:", "🚛"},
    {":tractor:", "🚜"},
    {":motorcycle:", "🏍️"},
    {":bike:", "🚲"},
    {":rotating_light:", "🚨"},
    {":oncoming_police_car:", "🚔"},
    {":oncoming_bus:", "🚍"},
    {":oncoming_automobile:", "🚘"},
    {":oncoming_taxi:", "🚖"},
    {":aerial_tramway:", "🚡"},
    {":mountain_cableway:", "🚠"},
    {":suspension_railway:", "🚟"},
    {":railway_car:", "🚃"},
    {":train:", "🚋"},
    {":monorail:", "🚝"},
    {":bullettrain_side:", "🚄"},
    {":bullettrain_front:", "🚅"},
    {":light_rail:", "🚈"},
    {":mountain_railway:", "🚞"},
    {":steam_locomotive:", "🚂"},
    {":train2:", "🚆"},
    {":metro:", "🚇"},
    {":tram:", "🚊"},
    {":station:", "🚉"},
    {":helicopter:", "🚁"},
    {":airplane:", "✈️"},
    {":airplane_departure:", "🛫"},
    {":airplane_arriving:", "🛬"},
    {":rocket:", "🚀"},
    {":satellite_orbital:", "🛰️"},
    {":seat:", "💺"},
    {":anchor:", "⚓"},
    {":construction:", "🚧"},
    {":fuelpump:", "⛽"},
    {":busstop:", "🚏"},
    {":vertical_traffic_light:", "🚦"},
    {":traffic_light:", "🚥"},
    {":checkered_flag:", "🏁"},
    {":ship:", "🚢"},
    {":ferris_wheel:", "🎡"},
    {":roller_coaster:", "🎢"},
    {":carousel_horse:", "🎠"},
    {":construction_site:", "🏗️"},
    {":foggy:", "🌁"},
    {":tokyo_tower:", "🗼"},
    {":factory:", "🏭"},
    {":fountain:", "⛲"},
    {":rice_scene:", "🎑"},
    {":mountain:", "⛰️"},
    {":mountain_snow:", "🏔️"},
    {":mount_fuji:", "🗻"},
    {":volcano:", "🌋"},
    {":japan:", "🗾"},
    {":camping:", "🏕️"},
    {":tent:", "⛺"},
    {":park:", "🏞️"},
    {":motorway:", "🛣️"},
    {":railway_track:", "🛤️"},
    {":sunrise:", "🌅"},
    {":sunrise_over_mountains:", "🌄"},
    {":desert:", "🏜️"},
    {":beach:", "🏖️"},
    {":island:", "🏝️"},
    {":city_sunset:", "🌇"},
    {":city_dusk:", "🌆"},
    {":cityscape:", "🏙️"},
    {":night_with_stars:", "🌃"},
    {":bridge_at_night:", "🌉"},
    {":milky_way:", "🌌"},
    {":stars:", "🌠"},
    {":sparkler:", "🎇"},
    {":fireworks:", "🎆"},
    {":rainbow:", "🌈"},
    {":homes:", "🏘️"},
    {":european_castle:", "🏰"},
    {":japanese_castle:", "🏯"},
    {":stadium:", "🏟️"},
    {":statue_of_liberty:", "🗽"},
    {":house:", "🏠"},
    {":house_with_garden:", "🏡"},
    {":house_buildings:", "🏘️"},
    {":derelict_house:", "🏚️"},
    {":office:", "🏢"},
    {":department_store:", "🏬"},
    {":post_office:", "🏣"},
    {":european_post_office:", "🏤"},
    {":hospital:", "🏥"},
    {":bank:", "🏦"},
    {":hotel:", "🏨"},
    {":convenience_store:", "🏪"},
    {":school:", "🏫"},
    {":love_hotel:", "🏩"},
    {":wedding:", "💒"},
    {":classical_building:", "🏛️"},
    {":church:", "⛪"},
    {":mosque:", "🕌"},
    {":synagogue:", "🕍"},
    {":kaaba:", "🕋"},
    {":shinto_shrine:", "⛩️"},
    
    // Objects
    {":watch:", "⌚"},
    {":iphone:", "📱"},
    {":calling:", "📲"},
    {":computer:", "💻"},
    {":keyboard:", "⌨️"},
    {":desktop:", "🖥️"},
    {":printer:", "🖨️"},
    {":mouse_three_button:", "🖱️"},
    {":trackball:", "🖲️"},
    {":joystick:", "🕹️"},
    {":compression:", "🗜️"},
    {":minidisc:", "💽"},
    {":floppy_disk:", "💾"},
    {":cd:", "💿"},
    {":dvd:", "📀"},
    {":vhs:", "📼"},
    {":camera:", "📷"},
    {":camera_with_flash:", "📸"},
    {":video_camera:", "📹"},
    {":movie_camera:", "🎥"},
    {":projector:", "📽️"},
    {":tv:", "📺"},
    {":radio:", "📻"},
    {":microphone2:", "🎙️"},
    {":level_slider:", "🎚️"},
    {":control_knobs:", "🎛️"},
    {":compass:", "🧭"},
    {":stopwatch:", "⏱️"},
    {":timer:", "⏲️"},
    {":alarm_clock:", "⏰"},
    {":clock:", "🕰️"},
    {":hourglass_flowing_sand:", "⏳"},
    {":hourglass:", "⌛"},
    {":satellite:", "📡"},
    {":battery:", "🔋"},
    {":electric_plug:", "🔌"},
    {":bulb:", "💡"},
    {":flashlight:", "🔦"},
    {":candle:", "🕯️"},
    {":diya_lamp:", "🪔"},
    {":wastebasket:", "🗑️"},
    {":oil:", "🛢️"},
    {":money_with_wings:", "💸"},
    {":dollar:", "💵"},
    {":yen:", "💴"},
    {":euro:", "💶"},
    {":pound:", "💷"},
    {":moneybag:", "💰"},
    {":credit_card:", "💳"},
    {":gem:", "💎"},
    {":scales:", "⚖️"},
    {":toolbox:", "🧰"},
    {":wrench:", "🔧"},
    {":hammer:", "🔨"},
    {":hammer_pick:", "⚒️"},
    {":tools:", "🛠️"},
    {":pick:", "⛏️"},
    {":nut_and_bolt:", "🔩"},
    {":gear:", "⚙️"},
    {":bricks:", "🧱"},
    {":chains:", "⛓️"},
    {":magnet:", "🧲"},
    {":gun:", "🔫"},
    {":bomb:", "💣"},
    {":firecracker:", "🧨"},
    {":knife:", "🔪"},
    {":dagger:", "🗡️"},
    {":crossed_swords:", "⚔️"},
    {":shield:", "🛡️"},
    {":smoking:", "🚬"},
    {":coffin:", "⚰️"},
    {":urn:", "⚱️"},
    {":amphora:", "🏺"},
    {":crystal_ball:", "🔮"},
    {":prayer_beads:", "📿"},
    {":nazar_amulet:", "🧿"},
    {":barber:", "💈"},
    {":alembic:", "⚗️"},
    {":telescope:", "🔭"},
    {":microscope:", "🔬"},
    {":hole:", "🕳️"},
    {":pill:", "💊"},
    {":syringe:", "💉"},
    {":drop_of_blood:", "🩸"},
    {":dna:", "🧬"},
    {":microbe:", "🦠"},
    {":petri_dish:", "🧫"},
    {":test_tube:", "🧪"},
    {":thermometer:", "🌡️"},
    {":broom:", "🧹"},
    {":basket:", "🧺"},
    {":toilet_paper:", "🧻"},
    {":label:", "🏷️"},
    {":bookmark:", "🔖"},
    {":toilet:", "🚽"},
    {":shower:", "🚿"},
    {":bathtub:", "🛁"},
    {":soap:", "🧼"},
    {":sponge:", "🧽"},
    {":fire_extinguisher:", "🧯"},
    {":shopping_cart:", "🛒"},
    
    // Symbols
    {":heart:", "❤️"},
    {":orange_heart:", "🧡"},
    {":yellow_heart:", "💛"},
    {":green_heart:", "💚"},
    {":blue_heart:", "💙"},
    {":purple_heart:", "💜"},
    {":brown_heart:", "🤎"},
    {":black_heart:", "🖤"},
    {":white_heart:", "🤍"},
    {":broken_heart:", "💔"},
    {":heart_exclamation:", "❣️"},
    {":two_hearts:", "💕"},
    {":revolving_hearts:", "💞"},
    {":heartbeat:", "💓"},
    {":heartpulse:", "💗"},
    {":sparkling_heart:", "💖"},
    {":cupid:", "💘"},
    {":gift_heart:", "💝"},
    {":heart_decoration:", "💟"},
    {":peace:", "☮️"},
    {":cross:", "✝️"},
    {":star_and_crescent:", "☪️"},
    {":om_symbol:", "🕉️"},
    {":wheel_of_dharma:", "☸️"},
    {":star_of_david:", "✡️"},
    {":six_pointed_star:", "🔯"},
    {":menorah:", "🕎"},
    {":yin_yang:", "☯️"},
    {":orthodox_cross:", "☦️"},
    {":place_of_worship:", "🛐"},
    {":ophiuchus:", "⛎"},
    {":aries:", "♈"},
    {":taurus:", "♉"},
    {":gemini:", "♊"},
    {":cancer:", "♋"},
    {":leo:", "♌"},
    {":virgo:", "♍"},
    {":libra:", "♎"},
    {":scorpius:", "♏"},
    {":sagittarius:", "♐"},
    {":capricorn:", "♑"},
    {":aquarius:", "♒"},
    {":pisces:", "♓"},
    {":id:", "🆔"},
    {":atom:", "⚛️"},
    {":accept:", "🉑"},
    {":radioactive:", "☢️"},
    {":biohazard:", "☣️"},
    {":mobile_phone_off:", "📴"},
    {":vibration_mode:", "📳"},
    {":u6709:", "🈶"},
    {":u7121:", "🈚"},
    {":u7533:", "🈸"},
    {":u55b6:", "🈺"},
    {":u6708:", "🈷️"},
    {":eight_pointed_black_star:", "✴️"},
    {":vs:", "🆚"},
    {":white_flower:", "💮"},
    {":ideograph_advantage:", "🉐"},
    {":secret:", "㊙️"},
    {":congratulations:", "㊗️"},
    {":u5408:", "🈴"},
    {":u6e80:", "🈵"},
    {":u5272:", "🈹"},
    {":u7981:", "🈲"},
    {":a:", "🅰️"},
    {":b:", "🅱️"},
    {":ab:", "🆎"},
    {":cl:", "🆑"},
    {":o2:", "🅾️"},
    {":sos:", "🆘"},
    {":x:", "❌"},
    {":o:", "⭕"},
    {":octagonal_sign:", "🛑"},
    {":no_entry:", "⛔"},
    {":name_badge:", "📛"},
    {":no_entry_sign:", "🚫"},
    {":100:", "💯"},
    {":anger:", "💢"},
    {":hotsprings:", "♨️"},
    {":no_pedestrians:", "🚷"},
    {":do_not_litter:", "🚯"},
    {":no_bicycles:", "🚳"},
    {":non-potable_water:", "🚱"},
    {":underage:", "🔞"},
    {":no_mobile_phones:", "📵"},
    {":no_smoking:", "🚭"},
    {":exclamation:", "❗"},
    {":grey_exclamation:", "❕"},
    {":question:", "❓"},
    {":grey_question:", "❔"},
    {":bangbang:", "‼️"},
    {":interrobang:", "⁉️"},
    {":low_brightness:", "🔅"},
    {":high_brightness:", "🔆"},
    {":part_alternation_mark:", "〽️"},
    {":warning:", "⚠️"},
    {":children_crossing:", "🚸"},
    {":trident:", "🔱"},
    {":fleur-de-lis:", "⚜️"},
    {":beginner:", "🔰"},
    {":recycle:", "♻️"},
    {":white_check_mark:", "✅"},
    {":u6307:", "🈯"},
    {":chart:", "💹"},
    {":sparkle:", "❇️"},
    {":eight_spoked_asterisk:", "✳️"},
    {":negative_squared_cross_mark:", "❎"},
    {":globe_with_meridians:", "🌐"},
    {":diamond_shape_with_a_dot_inside:", "💠"},
    {":m:", "Ⓜ️"},
    {":cyclone:", "🌀"},
    {":zzz:", "💤"},
    {":atm:", "🏧"},
    {":wc:", "🚾"},
    {":wheelchair:", "♿"},
    {":parking:", "🅿️"},
    {":u7a7a:", "🈳"},
    {":sa:", "🈂️"},
    {":passport_control:", "🛂"},
    {":customs:", "🛃"},
    {":baggage_claim:", "🛄"},
    {":left_luggage:", "🛅"},
    {":mens:", "🚹"},
    {":womens:", "🚺"},
    {":baby_symbol:", "🚼"},
    {":restroom:", "🚻"},
    {":put_litter_in_its_place:", "🚮"},
    {":cinema:", "🎦"},
    {":signal_strength:", "📶"},
    {":koko:", "🈁"},
    {":symbols:", "🔣"},
    {":information_source:", "ℹ️"},
    {":abc:", "🔤"},
    {":abcd:", "🔡"},
    {":capital_abcd:", "🔠"},
    {":ng:", "🆖"},
    {":ok:", "🆗"},
    {":up:", "🆙"},
    {":cool:", "🆒"},
    {":new:", "🆕"},
    {":free:", "🆓"},
    {":zero:", "0️⃣"},
    {":one:", "1️⃣"},
    {":two:", "2️⃣"},
    {":three:", "3️⃣"},
    {":four:", "4️⃣"},
    {":five:", "5️⃣"},
    {":six:", "6️⃣"},
    {":seven:", "7️⃣"},
    {":eight:", "8️⃣"},
    {":nine:", "9️⃣"},
    {":keycap_ten:", "🔟"},
    {":1234:", "🔢"},
    {":hash:", "#️⃣"},
    {":asterisk:", "*️⃣"},
    
    // Flags (popular ones)
    {":us:", "🇺🇸"},
    {":uk:", "🇬🇧"},
    {":fr:", "🇫🇷"},
    {":de:", "🇩🇪"},
    {":it:", "🇮🇹"},
    {":es:", "🇪🇸"},
    {":ru:", "🇷🇺"},
    {":jp:", "🇯🇵"},
    {":kr:", "🇰🇷"},
    {":cn:", "🇨🇳"},
    {":ca:", "🇨🇦"},
    {":au:", "🇦🇺"},
    {":in:", "🇮🇳"},
    {":br:", "🇧🇷"},
    {":mx:", "🇲🇽"},
    
    // GitHub specific
    {":octocat:", "🐙"},
    {":shipit:", "🚀"},
    {":bowtie:", "👔"},
    
    // Common programming/tech
    {":computer:", "💻"},
    {":keyboard:", "⌨️"},
    {":bug:", "🐛"},
    {":gear:", "⚙️"},
    {":wrench:", "🔧"},
    {":hammer:", "🔨"},
    {":electric_plug:", "🔌"},
    {":battery:", "🔋"},
    {":bulb:", "💡"},
    {":mag:", "🔍"},
    {":mag_right:", "🔎"},
    {":lock:", "🔒"},
    {":unlock:", "🔓"},
    {":key:", "🔑"},
    {":link:", "🔗"},
    {":paperclip:", "📎"},
    
    // End marker
    {NULL, NULL}
};

static Item parse_emoji_shortcode(Input *input, const char* text, int* pos) {
    if (text[*pos] != ':') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    (*pos)++; // Skip opening :
    
    // Find the closing :
    int shortcode_start = *pos;
    while (text[*pos] != '\0' && text[*pos] != ':') {
        // Only allow letters, numbers, underscores, and hyphens in shortcodes
        if (!isalnum(text[*pos]) && text[*pos] != '_' && text[*pos] != '-') {
            *pos = start_pos;
            return {.item = ITEM_NULL};
        }
        (*pos)++;
    }
    
    if (text[*pos] != ':') {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    // Extract shortcode using buffer
    int shortcode_len = *pos - shortcode_start;
    if (shortcode_len == 0) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    strbuf_append_char(sb, ':');
    strbuf_append_str_n(sb, text + shortcode_start, shortcode_len);
    strbuf_append_char(sb, ':');
    String* shortcode = strbuf_to_string(sb);
    
    (*pos)++; // Skip closing :
    
    // Look up the emoji
    const char* emoji_unicode = NULL;
    for (int i = 0; emoji_mappings[i].shortcode != NULL; i++) {
        if (strcmp(shortcode->chars, emoji_mappings[i].shortcode) == 0) {
            emoji_unicode = emoji_mappings[i].unicode;
            break;
        }
    }
    
    if (emoji_unicode == NULL) {
        // If not found, reset position and return null
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    // Create an emoji element for the unicode emoji
    Element* emoji_elem = create_markdown_element(input, "emoji");
    if (!emoji_elem) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    // Add the unicode emoji as text content using buffer
    strbuf_reset(sb);
    strbuf_append_str(sb, emoji_unicode);
    String* emoji_str = strbuf_to_string(sb);
    if (emoji_str) {
        list_push((List*)emoji_elem, {.item = s2it(emoji_str)});
        increment_element_content_length(emoji_elem);
    }
    
    return {.item = (uint64_t)emoji_elem};
}

static Item parse_strikethrough(Input *input, const char* text, int* pos) {
    if (text[*pos] != '~' || text[*pos + 1] != '~') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    *pos += 2; // Skip ~~
    
    int content_start = *pos;
    int content_end = -1;
    
    // Find closing ~~
    while (text[*pos] != '\0' && text[*pos + 1] != '\0') {
        if (text[*pos] == '~' && text[*pos + 1] == '~') {
            content_end = *pos;
            *pos += 2;
            break;
        }
        (*pos)++;
    }
    
    if (content_end == -1) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    Element* strike_elem = create_markdown_element(input, "s");
    if (!strike_elem) return {.item = ITEM_NULL};
    
    // Extract content using buffer
    String* content = create_string_from_buffer(input, text, content_start, content_end - content_start);
    
    if (content && content->len > 0) {
        Item text_content = parse_inline_content(input, content->chars);
        if (text_content.item != ITEM_NULL) {
            list_push((List*)strike_elem, text_content);
            increment_element_content_length(strike_elem);
        }
    }
    
    return {.item = (uint64_t)strike_elem};
}

static Item parse_superscript(Input *input, const char* text, int* pos) {
    if (text[*pos] != '^') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    (*pos)++; // Skip ^
    
    int content_start = *pos;
    int content_end = -1;
    
    // Find closing ^ or end of word
    while (text[*pos] != '\0') {
        if (text[*pos] == '^') {
            content_end = *pos;
            (*pos)++;
            break;
        } else if (isspace(text[*pos])) {
            content_end = *pos;
            break;
        }
        (*pos)++;
    }
    
    if (content_end == -1) content_end = *pos;
    
    if (content_end == content_start) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    Element* sup_elem = create_markdown_element(input, "sup");
    if (!sup_elem) return {.item = ITEM_NULL};
    
    // Extract content using the buffer
    String* content = create_string_from_buffer(input, text, content_start, content_end - content_start);
    
    if (content) {
        list_push((List*)sup_elem, {.item = s2it(content)});
        increment_element_content_length(sup_elem);
    }
    
    return {.item = (uint64_t)sup_elem};
}

static Item parse_subscript(Input *input, const char* text, int* pos) {
    if (text[*pos] != '~' || text[*pos + 1] == '~') return {.item = ITEM_NULL}; // Not ~ or ~~
    
    int start_pos = *pos;
    (*pos)++; // Skip ~
    
    int content_start = *pos;
    int content_end = -1;
    
    // Find closing ~ or end of word
    while (text[*pos] != '\0') {
        if (text[*pos] == '~') {
            content_end = *pos;
            (*pos)++;
            break;
        } else if (isspace(text[*pos])) {
            content_end = *pos;
            break;
        }
        (*pos)++;
    }
    
    if (content_end == -1) content_end = *pos;
    
    if (content_end == content_start) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    Element* sub_elem = create_markdown_element(input, "sub");
    if (!sub_elem) return {.item = ITEM_NULL};
    
    // Extract content using the buffer
    String* content = create_string_from_buffer(input, text, content_start, content_end - content_start);
    
    if (content) {
        list_push((List*)sub_elem, {.item = s2it(content)});
        increment_element_content_length(sub_elem);
    }
    
    return {.item = (uint64_t)sub_elem};
}

static Item parse_math_inline(Input *input, const char* text, int* pos) {
    int len = strlen(text);
    int start = *pos;
    
    if (start >= len || text[start] != '$') {
        return {.item = ITEM_NULL};
    }
    
    // Skip opening $
    int math_start = start + 1;
    int math_end = math_start;
    
    // Find closing $
    while (math_end < len && text[math_end] != '$') {
        // Handle escaped $
        if (text[math_end] == '\\' && math_end + 1 < len) {
            math_end += 2;
        } else {
            math_end++;
        }
    }
    
    if (math_end >= len || text[math_end] != '$') {
        return {.item = ITEM_NULL}; // No closing $
    }
    
    // Extract math content
    int content_len = math_end - math_start;
    if (content_len <= 0) {
        return {.item = ITEM_NULL}; // Empty math expression
    }
    
    String* math_content = create_string_from_buffer(input, text, math_start, content_len);
    
    // Parse the math content using the same input context (reuse memory pool)
    // Save current input state
    Item saved_root = input->root;
    StrBuf* saved_sb = input->sb;
    
    // Temporarily reset for math parsing
    input->root = {.item = ITEM_NULL};
    
    // Parse the math content using our math parser
    parse_math(input, math_content->chars, "latex");
    
    // Create wrapper element
    Element* math_elem = create_markdown_element(input, "math");
    if (math_elem && input->root.item != ITEM_NULL && input->root.item != ITEM_ERROR) {
        // Add the parsed math as child
        list_push((List*)math_elem, input->root);
        increment_element_content_length(math_elem);

        // Update position
        *pos = math_end + 1;

        // Restore input state
        input->root = saved_root;
        input->sb = saved_sb;

        return {.item = (uint64_t)math_elem};
    }

    // Cleanup on failure
    // Restore input state
    input->root = saved_root;
    input->sb = saved_sb;
    return {.item = ITEM_NULL};
}

// Parse display math expression: $$math$$
static Item parse_math_display(Input *input, const char* text, int* pos) {
    int len = strlen(text);
    int start = *pos;
    
    if (start + 1 >= len || text[start] != '$' || text[start + 1] != '$') {
        return {.item = ITEM_NULL};
    }
    
    // Skip opening $$
    int math_start = start + 2;
    int math_end = math_start;
    
    // Find closing $$
    while (math_end + 1 < len) {
        if (text[math_end] == '$' && text[math_end + 1] == '$') {
            break;
        }
        // Handle escaped $
        if (text[math_end] == '\\' && math_end + 1 < len) {
            math_end += 2;
        } else {
            math_end++;
        }
    }
    
    if (math_end + 1 >= len || text[math_end] != '$' || text[math_end + 1] != '$') {
        return {.item = ITEM_NULL}; // No closing $$
    }
    
    // Extract math content
    int content_len = math_end - math_start;
    if (content_len <= 0) {
        return {.item = ITEM_NULL}; // Empty math expression
    }
    
    String* math_content = create_string_from_buffer(input, text, math_start, content_len);
    
    // Parse the math content using the same input context (reuse memory pool)
    // Save current input state
    Item saved_root = input->root;
    StrBuf* saved_sb = input->sb;
    
    // Temporarily reset for math parsing
    input->root = {.item = ITEM_NULL};
    
    // Parse the math content using our math parser
    parse_math(input, math_content->chars, "latex");
    
    // Create wrapper element
    Element* math_elem = create_markdown_element(input, "displaymath");
    if (math_elem && input->root.item != ITEM_NULL && input->root.item != ITEM_ERROR) {
        // Add the parsed math as child
        list_push((List*)math_elem, input->root);
        increment_element_content_length(math_elem);

        // Update position
        *pos = math_end + 2;

        // Restore input state
        input->root = saved_root;
        input->sb = saved_sb;

        return {.item = (uint64_t)math_elem};
    }

    // Cleanup on failure
    // Restore input state
    input->root = saved_root;
    input->sb = saved_sb;
    return {.item = ITEM_NULL};
}

// Rewritten string-based parsing functions using proper strbuf patterns

static Item parse_emphasis(Input *input, const char* text, int* pos, char marker) {
    if (text[*pos] != marker) return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    int marker_count = 0;
    
    // Count markers
    while (text[*pos] == marker) {
        marker_count++;
        (*pos)++;
    }
    
    // Need at least 1 marker, max 2 for emphasis
    if (marker_count == 0 || marker_count > 2) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    int content_start = *pos;
    int content_end = -1;
    
    // Find closing markers
    while (text[*pos] != '\0') {
        if (text[*pos] == marker) {
            int close_marker_count = 0;
            int temp_pos = *pos;
            
            while (text[temp_pos] == marker) {
                close_marker_count++;
                temp_pos++;
            }
            
            if (close_marker_count >= marker_count) {
                content_end = *pos;
                *pos = temp_pos;
                break;
            }
        }
        (*pos)++;
    }
    
    if (content_end == -1) {
        // No closing marker found, revert
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    // Create element
    const char* tag_name = (marker_count >= 2) ? "strong" : "em";
    Element* emphasis_elem = create_markdown_element(input, tag_name);
    if (!emphasis_elem) return {.item = ITEM_NULL};
    
    // Extract content between markers using buffer
    String* content = create_string_from_buffer(input, text, content_start, content_end - content_start);
    
    if (content && content->len > 0) {
        Item text_content = parse_inline_content(input, content->chars);
        if (text_content.item != ITEM_NULL) {
            list_push((List*)emphasis_elem, text_content);
            increment_element_content_length(emphasis_elem);
        }
    }
    
    return {.item = (uint64_t)emphasis_elem};
}

static Item parse_code_span(Input *input, const char* text, int* pos) {
    if (text[*pos] != '`') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    int backtick_count = 0;
    
    // Count opening backticks
    while (text[*pos] == '`') {
        backtick_count++;
        (*pos)++;
    }
    
    int content_start = *pos;
    int content_end = -1;
    
    // Find closing backticks
    while (text[*pos] != '\0') {
        if (text[*pos] == '`') {
            int close_backtick_count = 0;
            int temp_pos = *pos;
            
            while (text[temp_pos] == '`') {
                close_backtick_count++;
                temp_pos++;
            }
            
            if (close_backtick_count == backtick_count) {
                content_end = *pos;
                *pos = temp_pos;
                break;
            }
        }
        (*pos)++;
    }
    
    if (content_end == -1) {
        // No closing backticks found, revert
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    Element* code_elem = create_markdown_element(input, "code");
    if (!code_elem) return {.item = ITEM_NULL};
    
    // Extract content between backticks using buffer
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    
    int content_len = content_end - content_start;
    
    // Trim single spaces from start and end if both are spaces
    if (content_len >= 2 && text[content_start] == ' ' && text[content_end - 1] == ' ') {
        strbuf_append_str_n(sb, text + content_start + 1, content_len - 2);
    } else {
        strbuf_append_str_n(sb, text + content_start, content_len);
    }
    
    String* code_str = strbuf_to_string(sb);
    if (code_str) {
        list_push((List*)code_elem, {.item = s2it(code_str)});
        increment_element_content_length(code_elem);
    }
    
    return {.item = (uint64_t)code_elem};
}

static Item parse_link(Input *input, const char* text, int* pos) {
    if (text[*pos] != '[') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    (*pos)++; // Skip opening [
    
    int link_text_start = *pos;
    int link_text_end = -1;
    
    // Find closing ]
    while (text[*pos] != '\0' && text[*pos] != ']') {
        (*pos)++;
    }
    
    if (text[*pos] != ']') {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    link_text_end = *pos;
    (*pos)++; // Skip ]
    
    // Check for ( to start URL
    if (text[*pos] != '(') {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    (*pos)++; // Skip (
    int url_start = *pos;
    int url_end = -1;
    int title_start = -1;
    int title_end = -1;
    
    // Find URL and optional title
    bool in_angle_brackets = false;
    bool found_title = false;
    
    if (text[*pos] == '<') {
        in_angle_brackets = true;
        (*pos)++;
        url_start = *pos;
    }
    
    while (text[*pos] != '\0') {
        if (in_angle_brackets && text[*pos] == '>') {
            url_end = *pos;
            (*pos)++;
            break;
        } else if (!in_angle_brackets && (text[*pos] == ')' || text[*pos] == ' ')) {
            if (url_end == -1) url_end = *pos;
            
            if (text[*pos] == ' ') {
                // Look for title
                (*pos)++;
                while (text[*pos] == ' ') (*pos)++;
                
                if (text[*pos] == '"' || text[*pos] == '\'' || text[*pos] == '(') {
                    char title_delim = text[*pos];
                    if (title_delim == '(') title_delim = ')';
                    
                    (*pos)++;
                    title_start = *pos;
                    
                    while (text[*pos] != '\0' && text[*pos] != title_delim) {
                        (*pos)++;
                    }
                    
                    if (text[*pos] == title_delim) {
                        title_end = *pos;
                        (*pos)++;
                        found_title = true;
                    }
                }
                
                while (text[*pos] == ' ') (*pos)++;
            }
            
            if (text[*pos] == ')') {
                (*pos)++; // Skip )
                break;
            }
        } else {
            (*pos)++;
        }
    }
    
    if (url_end == -1) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    // Create link element
    Element* link_elem = create_markdown_element(input, "a");
    if (!link_elem) return {.item = ITEM_NULL};
    
    // Extract and add href attribute using buffer
    String* url = create_string_from_buffer(input, text, url_start, url_end - url_start);
    add_attribute_to_element(input, link_elem, "href", url->chars);
    
    // Add title attribute if present
    if (found_title && title_start != -1 && title_end != -1) {
        String* title = create_string_from_buffer(input, text, title_start, title_end - title_start);
        add_attribute_to_element(input, link_elem, "title", title->chars);
    }
    
    // Extract and parse link text using buffer
    String* link_text = create_string_from_buffer(input, text, link_text_start, link_text_end - link_text_start);
    
    if (link_text && link_text->len > 0) {
        Item text_content = parse_inline_content(input, link_text->chars);
        if (text_content.item != ITEM_NULL) {
            list_push((List*)link_elem, text_content);
            increment_element_content_length(link_elem);
        }
    }
    
    return {.item = (uint64_t)link_elem};
}

// Simple inline content parser that demonstrates clean buffer usage
static Item parse_inline_content(Input *input, const char* text) {
    if (!text || strlen(text) == 0) { return {.item = ITEM_NULL}; }
    
    int len = strlen(text);
    int pos = 0;
    
    // Create a span to hold mixed content
    Element* span = create_markdown_element(input, "span");
    if (!span) return {.item = ITEM_NULL};
    
    StrBuf* sb = input->sb;
    
    while (pos < len) {
        char ch = text[pos];
        
        // Check for various inline elements
        if (ch == '*' || ch == '_') {
            // Flush any accumulated text
            if (sb->length > sizeof(uint32_t)) {  // Has content beyond length field
                String* text_str = strbuf_to_string(sb);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    increment_element_content_length(span);
                }
                strbuf_reset(sb);
            }
            
            Item emphasis = parse_emphasis(input, text, &pos, ch);
            if (emphasis.item != ITEM_NULL) {
                list_push((List*)span, emphasis);
                increment_element_content_length(span);
                continue;
            }
        } else if (ch == '`') {
            // Flush any accumulated text
            if (sb->length > sizeof(uint32_t)) {  // Has content beyond length field
                String* text_str = strbuf_to_string(sb);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    increment_element_content_length(span);
                }
                strbuf_reset(sb);
            }
            
            Item code_span = parse_code_span(input, text, &pos);
            if (code_span.item != ITEM_NULL) {
                list_push((List*)span, code_span);
                increment_element_content_length(span);
                continue;
            }
        } else if (ch == '[') {
            // Flush any accumulated text
            if (sb->length > sizeof(uint32_t)) {  // Has content beyond length field
                String* text_str = strbuf_to_string(sb);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    increment_element_content_length(span);
                }
                strbuf_reset(sb);
            }
            
            Item link = parse_link(input, text, &pos);
            if (link.item != ITEM_NULL) {
                list_push((List*)span, link);
                increment_element_content_length(span);
                continue;
            }
        } else if (ch == '~') {
            // Flush any accumulated text
            if (sb->length > sizeof(uint32_t)) {  // Has content beyond length field
                String* text_str = strbuf_to_string(sb);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    increment_element_content_length(span);
                }
                strbuf_reset(sb);
            }
            
            // Check for strikethrough first (~~)
            Item strikethrough = parse_strikethrough(input, text, &pos);
            if (strikethrough.item != ITEM_NULL) {
                list_push((List*)span, strikethrough);
                increment_element_content_length(span);
                continue;
            }
            
            // Check for subscript (~)
            Item subscript = parse_subscript(input, text, &pos);
            if (subscript.item != ITEM_NULL) {
                list_push((List*)span, subscript);
                increment_element_content_length(span);
                continue;
            }
        } else if (ch == '^') {
            // Flush any accumulated text
            if (sb->length > sizeof(uint32_t)) {  // Has content beyond length field
                String* text_str = strbuf_to_string(sb);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    increment_element_content_length(span);
                }
                strbuf_reset(sb);
            }
            
            Item superscript = parse_superscript(input, text, &pos);
            if (superscript.item != ITEM_NULL) {
                list_push((List*)span, superscript);
                increment_element_content_length(span);
                continue;
            }
        } else if (ch == '$') {
            // Flush any accumulated text
            if (sb->length > sizeof(uint32_t)) {  // Has content beyond length field
                String* text_str = strbuf_to_string(sb);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    increment_element_content_length(span);
                }
                strbuf_reset(sb);
            }
            
            // Check for display math first ($$)
            Item math_display = parse_math_display(input, text, &pos);
            if (math_display.item != ITEM_NULL) {
                list_push((List*)span, math_display);
                increment_element_content_length(span);
                continue;
            }
            
            // Check for inline math ($)
            Item math_inline = parse_math_inline(input, text, &pos);
            if (math_inline.item != ITEM_NULL) {
                list_push((List*)span, math_inline);
                increment_element_content_length(span);
                continue;
            }
        } else if (ch == ':') {
            // Flush any accumulated text
            if (sb->length > sizeof(uint32_t)) {  // Has content beyond length field
                String* text_str = strbuf_to_string(sb);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    increment_element_content_length(span);
                }
                strbuf_reset(sb);
            }
            
            Item emoji = parse_emoji_shortcode(input, text, &pos);
            if (emoji.item != ITEM_NULL) {
                list_push((List*)span, emoji);
                increment_element_content_length(span);
                continue;
            }
        }
        
        // If no special parsing occurred, add character to text buffer
        strbuf_append_char(sb, ch);
        pos++;
    }
    
    // Flush any remaining text
    if (sb->length > sizeof(uint32_t)) {  // Has content beyond length field
        String* text_str = strbuf_to_string(sb);
        if (text_str && text_str->len > 0) {
            list_push((List*)span, {.item = s2it(text_str)});
            increment_element_content_length(span);
        }
    }
    
    // If span has no content, return null
    if (((List*)span)->length == 0) {
        return {.item = ITEM_NULL};
    }
    
    // If span has only one text item, return it directly
    if (((List*)span)->length == 1) {
        return ((Item*)((List*)span)->items)[0];
    }
    
    return {.item = (uint64_t)span};
}

// Additional missing parse functions - rewritten with proper buffer usage

static Item parse_header(Input *input, const char* line) {
    if (!is_atx_heading(line)) return {.item = ITEM_NULL};
    
    int hash_count = count_leading_chars(line, '#');
    
    // Skip hashes and whitespace
    const char* content_start = line + hash_count;
    while (*content_start && is_whitespace_char(*content_start)) {
        content_start++;
    }
    
    // Create header element
    char tag_name[10];
    snprintf(tag_name, sizeof(tag_name), "h%d", hash_count);
    Element* header = create_markdown_element(input, tag_name);
    if (!header) return {.item = ITEM_NULL};
    
    // Add level attribute as required by PandocSchema
    char level_str[10];
    snprintf(level_str, sizeof(level_str), "%d", hash_count);
    add_attribute_to_element(input, header, "level", level_str);
    
    // Add content if present using buffer
    if (*content_start != '\0') {
        String* content = create_trimmed_string(input, content_start);
        if (content && content->len > 0) {
            Item text_content = parse_inline_content(input, content->chars);
            if (text_content.item != ITEM_NULL) {
                list_push((List*)header, text_content);
                increment_element_content_length(header);
            }
        }
    }
    
    return {.item = (uint64_t)header};
}

static Item parse_thematic_break(Input *input) {
    Element* hr = create_markdown_element(input, "hr");
    return {.item = (uint64_t)hr};
}

static Item parse_code_block(Input *input, char** lines, int* current_line, int total_lines) {
    char fence_char;
    int fence_length;
    
    if (!is_fenced_code_block_start(lines[*current_line], &fence_char, &fence_length)) {
        return {.item = ITEM_NULL};
    }
    
    // Extract info string (language) using buffer
    const char* info_start = lines[*current_line];
    while (*info_start && *info_start != fence_char) info_start++;
    while (*info_start == fence_char) info_start++;
    
    String* info_string = create_trimmed_string(input, info_start);
    
    // Create code element directly (no pre wrapper)
    Element* code_block = create_markdown_element(input, "code");
    if (!code_block) {
        return {.item = ITEM_NULL};
    }
    
    // Add language attribute if present
    if (info_string && info_string->len > 0) {
        add_attribute_to_element(input, code_block, "language", info_string->chars);
    }
    
    // Check if this is a math code block
    bool is_math_block = (info_string && info_string->len > 0 && 
                         (strcmp(info_string->chars, "math") == 0 || 
                          strcmp(info_string->chars, "latex") == 0 ||
                          strcmp(info_string->chars, "tex") == 0));
    
    (*current_line)++;
    
    // Collect code content using buffer
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    bool first_line = true;
    
    while (*current_line < total_lines) {
        const char* line = lines[*current_line];
        
        if (!line) {
            (*current_line)++;
            continue;
        }
        
        // Check for closing fence
        if (line[0] == fence_char) {
            int close_fence_length = count_leading_chars(line, fence_char);
            if (close_fence_length >= fence_length) {
                (*current_line)++; // Move past the closing fence
                break;
            }
        }
        
        // Add line to content
        if (!first_line) {
            strbuf_append_char(sb, '\n');
        }
        
        strbuf_append_str(sb, line);
        first_line = false;
        (*current_line)++;
    }
    
    // Create string content
    String* content_str = strbuf_to_string(sb);
    
    // Add content to element
    if (content_str && content_str->len > 0) {
        if (is_math_block) {
            // Change element type to displaymath for math blocks
            Element* math_elem = create_markdown_element(input, "displaymath");
            if (math_elem) {
                add_attribute_to_element(input, math_elem, "language", "math");
                list_push((List*)math_elem, {.item = s2it(content_str)});
                increment_element_content_length(math_elem);
                return {.item = (uint64_t)math_elem};
            }
        }
        
        list_push((List*)code_block, {.item = s2it(content_str)});
        increment_element_content_length(code_block);
    }
    
    return {.item = (uint64_t)code_block};
}

static Item parse_blockquote(Input *input, char** lines, int* current_line, int total_lines) {
    if (!is_blockquote(lines[*current_line])) {
        return {.item = ITEM_NULL};
    }
    
    Element* blockquote = create_markdown_element(input, "blockquote");
    if (!blockquote) return {.item = ITEM_NULL};
    
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    
    // Collect all consecutive blockquote lines
    while (*current_line < total_lines && is_blockquote(lines[*current_line])) {
        const char* line = lines[*current_line];
        
        // Skip leading > and optional space
        int pos = 0;
        while (pos < 3 && line[pos] == ' ') pos++;
        if (line[pos] == '>') {
            pos++;
            if (line[pos] == ' ') pos++;
        }
        
        // Add remaining content
        if (sb->length > sizeof(uint32_t)) { // Not first line
            strbuf_append_char(sb, '\n');
        }
        strbuf_append_str(sb, line + pos);
        
        (*current_line)++;
    }
    
    // Parse the collected content as markdown
    String* content = strbuf_to_string(sb);
    if (content && content->len > 0) {
        // Split into lines and parse recursively
        int sub_line_count;
        char** sub_lines = split_lines(content->chars, &sub_line_count);
        
        if (sub_lines) {
            int sub_current_line = 0;
            while (sub_current_line < sub_line_count) {
                Item block = parse_block_element(input, sub_lines, &sub_current_line, sub_line_count);
                if (block.item != ITEM_NULL) {
                    list_push((List*)blockquote, block);
                    increment_element_content_length(blockquote);
                }
            }
            free_lines(sub_lines, sub_line_count);
        }
    }
    
    return {.item = (uint64_t)blockquote};
}

static Item parse_paragraph(Input *input, char** lines, int* current_line, int total_lines) {
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    
    // Collect consecutive non-empty lines that aren't special blocks
    bool first_line = true;
    while (*current_line < total_lines) {
        const char* line = lines[*current_line];
        
        if (!line || strlen(line) == 0) break;
        if (is_atx_heading(line)) break;
        if (is_thematic_break(line)) break;
        if (is_fenced_code_block_start(line, NULL, NULL)) break;
        if (is_blockquote(line)) break;
        
        bool is_ordered;
        int number;
        if (is_list_marker(line, &is_ordered, &number)) break;
        
        if (!first_line) {
            strbuf_append_char(sb, ' ');
        }
        strbuf_append_str(sb, line);
        
        first_line = false;
        (*current_line)++;
    }
    
    if (sb->length <= sizeof(uint32_t)) { // No content
        return {.item = ITEM_NULL};
    }
    
    Element* p = create_markdown_element(input, "p");
    if (!p) return {.item = ITEM_NULL};
    
    String* content = strbuf_to_string(sb);
    if (content && content->len > 0) {
        Item inline_content = parse_inline_content(input, content->chars);
        if (inline_content.item != ITEM_NULL) {
            list_push((List*)p, inline_content);
            increment_element_content_length(p);
        }
    }
    
    return {.item = (uint64_t)p};
}

// Additional helper functions

static Item parse_list(Input *input, char** lines, int* current_line, int total_lines) {
    bool is_ordered;
    int start_number;
    
    if (!is_list_marker(lines[*current_line], &is_ordered, &start_number)) {
        return {.item = ITEM_NULL};
    }
    
    const char* list_tag = is_ordered ? "ol" : "ul";
    Element* list_elem = create_markdown_element(input, list_tag);
    if (!list_elem) return {.item = ITEM_NULL};
    
    // Add start attribute for ordered lists if not starting at 1
    if (is_ordered && start_number != 1) {
        char start_str[16];
        snprintf(start_str, sizeof(start_str), "%d", start_number);
        add_attribute_to_element(input, list_elem, "start", start_str);
    }
    
    while (*current_line < total_lines) {
        bool line_is_ordered;
        int line_number;
        
        if (!is_list_marker(lines[*current_line], &line_is_ordered, &line_number) ||
            line_is_ordered != is_ordered) {
            break;
        }
        
        // Create list item
        Element* li = create_markdown_element(input, "li");
        if (!li) break;
        
        // Extract content after marker
        const char* line = lines[*current_line];
        int pos = 0;
        
        // Skip leading spaces
        while (pos < 3 && line[pos] == ' ') pos++;
        
        // Skip marker
        if (is_ordered) {
            while (isdigit(line[pos])) pos++;
            if (line[pos] == '.' || line[pos] == ')') pos++;
        } else {
            pos++; // Skip -, +, or *
        }
        
        // Skip spaces after marker
        while (line[pos] == ' ') pos++;
        
        // Add content if present
        if (line[pos] != '\0') {
            Item content = parse_inline_content(input, line + pos);
            if (content.item != ITEM_NULL) {
                list_push((List*)li, content);
                increment_element_content_length(li);
            }
        }
        
        list_push((List*)list_elem, {.item = (uint64_t)li});
        increment_element_content_length(list_elem);
        
        (*current_line)++;
    }
    
    return {.item = (uint64_t)list_elem};
}

// Complete table parsing implementation
static Item parse_table(Input *input, char** lines, int* current_line, int total_lines) {
    if (!is_table_row(lines[*current_line])) return {.item = ITEM_NULL};
    
    // Check if next line is separator
    if (*current_line + 1 >= total_lines || !is_table_separator(lines[*current_line + 1])) {
        return {.item = ITEM_NULL};
    }
    
    // Parse alignment from separator line
    int alignment_count;
    char** alignments = parse_table_alignment(lines[*current_line + 1], &alignment_count);
    
    // Create table element
    Element* table = create_markdown_element(input, "table");
    if (!table) {
        if (alignments) {
            for (int i = 0; i < alignment_count; i++) free(alignments[i]);
            free(alignments);
        }
        return {.item = ITEM_NULL};
    }
    
    // Create colgroup for column specifications
    if (alignments) {
        Element* colgroup = create_markdown_element(input, "colgroup");
        if (colgroup) {
            for (int i = 0; i < alignment_count; i++) {
                Element* col = create_markdown_element(input, "col");
                if (col) {
                    add_attribute_to_element(input, col, "align", alignments[i]);
                    list_push((List*)colgroup, {.item = (uint64_t)col});
                    increment_element_content_length(colgroup);
                }
            }
            list_push((List*)table, {.item = (uint64_t)colgroup});
            increment_element_content_length(table);
        }
    }
    
    // Parse header row
    int header_cell_count;
    char** header_cells = parse_table_row(lines[*current_line], &header_cell_count);
    
    if (!header_cells) {
        if (alignments) {
            for (int i = 0; i < alignment_count; i++) free(alignments[i]);
            free(alignments);
        }
        return {.item = ITEM_NULL};
    }
    
    // Create thead
    Element* thead = create_markdown_element(input, "thead");
    if (!thead) {
        free_table_row(header_cells, header_cell_count);
        if (alignments) {
            for (int i = 0; i < alignment_count; i++) free(alignments[i]);
            free(alignments);
        }
        return {.item = ITEM_NULL};
    }
    
    Element* header_row = create_markdown_element(input, "tr");
    if (!header_row) {
        free_table_row(header_cells, header_cell_count);
        if (alignments) {
            for (int i = 0; i < alignment_count; i++) free(alignments[i]);
            free(alignments);
        }
        return {.item = ITEM_NULL};
    }
    
    // Add header cells
    for (int i = 0; i < header_cell_count; i++) {
        Element* th = create_markdown_element(input, "th");
        if (th) {
            // Add alignment attribute
            if (alignments && i < alignment_count) {
                add_attribute_to_element(input, th, "align", alignments[i]);
            }
            
            if (header_cells[i] && strlen(header_cells[i]) > 0) {
                Item cell_content = parse_inline_content(input, header_cells[i]);
                if (cell_content.item != ITEM_NULL) {
                    list_push((List*)th, cell_content);
                    increment_element_content_length(th);
                }
            }
        }
        if (th) {
            list_push((List*)header_row, {.item = (uint64_t)th});
            increment_element_content_length(header_row);
        }
    }
    
    list_push((List*)thead, {.item = (uint64_t)header_row});
    increment_element_content_length(thead);
    
    list_push((List*)table, {.item = (uint64_t)thead});
    increment_element_content_length(table);
    
    free_table_row(header_cells, header_cell_count);
    
    (*current_line) += 2; // Skip header and separator
    
    // Create tbody
    Element* tbody = create_markdown_element(input, "tbody");
    if (!tbody) {
        if (alignments) {
            for (int i = 0; i < alignment_count; i++) free(alignments[i]);
            free(alignments);
        }
        return {.item = (uint64_t)table};
    }
    
    // Parse data rows
    while (*current_line < total_lines && is_table_row(lines[*current_line])) {
        int cell_count;
        char** cells = parse_table_row(lines[*current_line], &cell_count);
        
        if (!cells) break;
        
        Element* row = create_markdown_element(input, "tr");
        if (!row) {
            free_table_row(cells, cell_count);
            break;
        }
        
        // Add cells (pad with empty cells if needed)
        for (int i = 0; i < header_cell_count; i++) {
            Element* td = create_markdown_element(input, "td");
            if (td) {
                // Add alignment attribute
                if (alignments && i < alignment_count) {
                    add_attribute_to_element(input, td, "align", alignments[i]);
                }
                
                if (i < cell_count && cells[i] && strlen(cells[i]) > 0) {
                    Item cell_content = parse_inline_content(input, cells[i]);
                    if (cell_content.item != ITEM_NULL) {
                        list_push((List*)td, cell_content);
                        increment_element_content_length(td);
                    }
                }
                list_push((List*)row, {.item = (uint64_t)td});
                increment_element_content_length(row);
            }
        }
        
        list_push((List*)tbody, {.item = (uint64_t)row});
        increment_element_content_length(tbody);
        
        free_table_row(cells, cell_count);
        (*current_line)++;
    }
    
    list_push((List*)table, {.item = (uint64_t)tbody});
    increment_element_content_length(table);
    
    // Cleanup
    if (alignments) {
        for (int i = 0; i < alignment_count; i++) free(alignments[i]);
        free(alignments);
    }
    
    return {.item = (uint64_t)table};
}

// Complete parse_block_element implementation
static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines) {
    if (*current_line >= total_lines || !lines[*current_line]) {
        return {.item = ITEM_NULL};
    }
    
    const char* line = lines[*current_line];
    
    // Skip empty lines
    if (is_empty_line(line)) {
        (*current_line)++;
        return {.item = ITEM_NULL};
    }
    
    // Check for ATX headers
    if (is_atx_heading(line)) {
        Item header = parse_header(input, line);
        if (header.item != ITEM_NULL) {
            (*current_line)++;
            return header;
        }
    }
    
    // Check for thematic breaks
    if (is_thematic_break(line)) {
        (*current_line)++;
        return parse_thematic_break(input);
    }
    
    // Check for fenced code blocks
    if (is_fenced_code_block_start(line, NULL, NULL)) {
        return parse_code_block(input, lines, current_line, total_lines);
    }
    
    // Check for blockquotes
    if (is_blockquote(line)) {
        return parse_blockquote(input, lines, current_line, total_lines);
    }
    
    // Check for lists
    bool is_ordered;
    int number;
    if (is_list_marker(line, &is_ordered, &number)) {
        return parse_list(input, lines, current_line, total_lines);
    }
    
    // Check for tables
    if (is_table_row(line) && *current_line + 1 < total_lines && is_table_separator(lines[*current_line + 1])) {
        return parse_table(input, lines, current_line, total_lines);
    }
    
    // Default to paragraph
    return parse_paragraph(input, lines, current_line, total_lines);
}

// Complete parse_markdown_content implementation
static Item parse_markdown_content(Input *input, char** lines, int line_count) {
    // Create the root document element
    Element* doc = create_markdown_element(input, "doc");
    if (!doc) return {.item = ITEM_NULL};
    
    // Add version attribute to doc
    add_attribute_to_element(input, doc, "version", "1.0");
    
    // Create metadata element
    Element* meta = create_markdown_element(input, "meta");
    if (!meta) return {.item = (uint64_t)doc};
    
    // Add default metadata
    add_attribute_to_element(input, meta, "title", "Markdown Document");
    add_attribute_to_element(input, meta, "language", "en");
    
    // Parse YAML frontmatter if present
    int content_start = parse_yaml_frontmatter(input, lines, line_count, meta);
    
    // Add meta to doc
    list_push((List*)doc, {.item = (uint64_t)meta});
    increment_element_content_length(doc);
    
    // Create body element for content
    Element* body = create_markdown_element(input, "body");
    if (!body) return {.item = (uint64_t)doc};
    
    int current_line = content_start; // Start after YAML frontmatter
    
    while (current_line < line_count) {
        // Skip empty lines
        if (!lines[current_line] || is_empty_line(lines[current_line])) {
            current_line++;
            continue;
        }
        
        Item block = parse_block_element(input, lines, &current_line, line_count);
        if (block.item != ITEM_NULL) {
            list_push((List*)body, block);
            increment_element_content_length(body);
        }
    }
    
    // Add body to doc
    list_push((List*)doc, {.item = (uint64_t)body});
    increment_element_content_length(doc);
    
    return {.item = (uint64_t)doc};
}

// Entry point function
void parse_markdown(Input* input, const char* markdown_string) {
    input->sb = strbuf_new_pooled(input->pool);
    int line_count;
    char** lines = split_lines(markdown_string, &line_count);
    input->root = parse_markdown_content(input, lines, line_count);
    free_lines(lines, line_count);
}
