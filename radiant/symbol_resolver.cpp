/**
 * @file symbol_resolver.cpp
 * @brief Unified symbol resolution for rendering HTML entities and emoji shortcodes
 */

#include "symbol_resolver.h"
#include "../lambda/input/html_entities.h"
#include <cstring>

// String structure from Lambda (for resolve_symbol_string)
typedef struct String {
    uint32_t len;
    char chars[];
} String;

// Emoji shortcode table (without : delimiters)
// This is a curated subset of the most commonly used emoji
static const struct {
    const char* name;
    const char* utf8;
} emoji_table[] = {
    // Smileys & Emotion
    {"smile", "😄"},
    {"smiley", "😃"},
    {"grinning", "😀"},
    {"blush", "😊"},
    {"wink", "😉"},
    {"heart_eyes", "😍"},
    {"kissing_heart", "😘"},
    {"stuck_out_tongue", "😛"},
    {"disappointed", "😞"},
    {"angry", "😠"},
    {"rage", "😡"},
    {"cry", "😢"},
    {"sob", "😭"},
    {"joy", "😂"},
    {"laughing", "😆"},
    {"sweat_smile", "😅"},
    {"sunglasses", "😎"},
    {"thinking", "🤔"},
    {"smirk", "😏"},
    {"neutral_face", "😐"},
    {"expressionless", "😑"},
    {"unamused", "😒"},
    {"roll_eyes", "🙄"},
    {"grimacing", "😬"},
    {"relieved", "😌"},
    {"pensive", "😔"},
    {"sleepy", "😪"},
    {"drooling_face", "🤤"},
    {"sleeping", "😴"},
    {"mask", "😷"},
    {"face_with_thermometer", "🤒"},
    {"nerd_face", "🤓"},
    {"innocent", "😇"},
    {"smiling_imp", "😈"},
    {"skull", "💀"},
    {"ghost", "👻"},
    {"alien", "👽"},
    {"robot", "🤖"},
    {"poop", "💩"},
    {"clown_face", "🤡"},
    {"see_no_evil", "🙈"},
    {"hear_no_evil", "🙉"},
    {"speak_no_evil", "🙊"},

    // Hearts & Love
    {"heart", "❤️"},
    {"orange_heart", "🧡"},
    {"yellow_heart", "💛"},
    {"green_heart", "💚"},
    {"blue_heart", "💙"},
    {"purple_heart", "💜"},
    {"black_heart", "🖤"},
    {"white_heart", "🤍"},
    {"broken_heart", "💔"},
    {"sparkling_heart", "💖"},
    {"heartbeat", "💓"},
    {"heartpulse", "💗"},
    {"two_hearts", "💕"},
    {"revolving_hearts", "💞"},
    {"gift_heart", "💝"},
    {"heart_decoration", "💟"},
    {"cupid", "💘"},
    {"kiss", "💋"},

    // Gestures & Body
    {"wave", "👋"},
    {"raised_hand", "✋"},
    {"ok_hand", "👌"},
    {"thumbsup", "👍"},
    {"thumbsdown", "👎"},
    {"clap", "👏"},
    {"raised_hands", "🙌"},
    {"open_hands", "👐"},
    {"pray", "🙏"},
    {"handshake", "🤝"},
    {"muscle", "💪"},
    {"point_up", "☝️"},
    {"point_down", "👇"},
    {"point_left", "👈"},
    {"point_right", "👉"},
    {"v", "✌️"},
    {"punch", "👊"},
    {"fist", "✊"},
    {"crossed_fingers", "🤞"},
    {"metal", "🤘"},
    {"call_me_hand", "🤙"},
    {"eyes", "👀"},
    {"eye", "👁️"},
    {"brain", "🧠"},

    // Animals
    {"dog", "🐶"},
    {"cat", "🐱"},
    {"mouse", "🐭"},
    {"hamster", "🐹"},
    {"rabbit", "🐰"},
    {"fox_face", "🦊"},
    {"bear", "🐻"},
    {"panda_face", "🐼"},
    {"koala", "🐨"},
    {"tiger", "🐯"},
    {"lion", "🦁"},
    {"cow", "🐮"},
    {"pig", "🐷"},
    {"frog", "🐸"},
    {"monkey", "🐒"},
    {"chicken", "🐔"},
    {"penguin", "🐧"},
    {"bird", "🐦"},
    {"eagle", "🦅"},
    {"duck", "🦆"},
    {"owl", "🦉"},
    {"bat", "🦇"},
    {"wolf", "🐺"},
    {"horse", "🐴"},
    {"unicorn", "🦄"},
    {"bee", "🐝"},
    {"bug", "🐛"},
    {"butterfly", "🦋"},
    {"snail", "🐌"},
    {"snake", "🐍"},
    {"dragon", "🐉"},
    {"turtle", "🐢"},
    {"fish", "🐟"},
    {"dolphin", "🐬"},
    {"whale", "🐳"},
    {"octopus", "🐙"},
    {"crab", "🦀"},
    {"shrimp", "🦐"},

    // Nature & Weather
    {"sun", "☀️"},
    {"moon", "🌙"},
    {"star", "⭐"},
    {"stars", "🌟"},
    {"cloud", "☁️"},
    {"rainbow", "🌈"},
    {"umbrella", "☂️"},
    {"snowflake", "❄️"},
    {"fire", "🔥"},
    {"droplet", "💧"},
    {"ocean", "🌊"},
    {"earth_americas", "🌎"},
    {"earth_africa", "🌍"},
    {"earth_asia", "🌏"},
    {"globe_with_meridians", "🌐"},
    {"full_moon", "🌕"},
    {"new_moon", "🌑"},
    {"zap", "⚡"},
    {"comet", "☄️"},
    {"boom", "💥"},
    {"sparkles", "✨"},
    {"dizzy", "💫"},

    // Food & Drink
    {"apple", "🍎"},
    {"green_apple", "🍏"},
    {"banana", "🍌"},
    {"grapes", "🍇"},
    {"strawberry", "🍓"},
    {"watermelon", "🍉"},
    {"lemon", "🍋"},
    {"orange", "🍊"},
    {"peach", "🍑"},
    {"cherries", "🍒"},
    {"pizza", "🍕"},
    {"hamburger", "🍔"},
    {"fries", "🍟"},
    {"hotdog", "🌭"},
    {"taco", "🌮"},
    {"burrito", "🌯"},
    {"sushi", "🍣"},
    {"ramen", "🍜"},
    {"cake", "🎂"},
    {"cookie", "🍪"},
    {"doughnut", "🍩"},
    {"icecream", "🍨"},
    {"ice_cream", "🍦"},
    {"chocolate_bar", "🍫"},
    {"candy", "🍬"},
    {"lollipop", "🍭"},
    {"coffee", "☕"},
    {"tea", "🍵"},
    {"beer", "🍺"},
    {"wine_glass", "🍷"},
    {"cocktail", "🍸"},
    {"champagne", "🍾"},
    {"tropical_drink", "🍹"},

    // Objects & Symbols
    {"rocket", "🚀"},
    {"airplane", "✈️"},
    {"car", "🚗"},
    {"bus", "🚌"},
    {"train", "🚂"},
    {"bike", "🚲"},
    {"motorcycle", "🏍️"},
    {"ship", "🚢"},
    {"anchor", "⚓"},
    {"alarm_clock", "⏰"},
    {"hourglass", "⌛"},
    {"watch", "⌚"},
    {"computer", "💻"},
    {"keyboard", "⌨️"},
    {"phone", "📱"},
    {"telephone", "☎️"},
    {"email", "📧"},
    {"envelope", "✉️"},
    {"package", "📦"},
    {"gift", "🎁"},
    {"balloon", "🎈"},
    {"tada", "🎉"},
    {"confetti_ball", "🎊"},
    {"sparkler", "🎇"},
    {"fireworks", "🎆"},
    {"trophy", "🏆"},
    {"medal", "🏅"},
    {"crown", "👑"},
    {"gem", "💎"},
    {"ring", "💍"},
    {"moneybag", "💰"},
    {"dollar", "💵"},
    {"credit_card", "💳"},
    {"bulb", "💡"},
    {"flashlight", "🔦"},
    {"wrench", "🔧"},
    {"hammer", "🔨"},
    {"lock", "🔒"},
    {"unlock", "🔓"},
    {"key", "🔑"},
    {"mag", "🔍"},
    {"microscope", "🔬"},
    {"telescope", "🔭"},
    {"satellite", "🛰️"},
    {"books", "📚"},
    {"book", "📖"},
    {"bookmark", "🔖"},
    {"pencil", "✏️"},
    {"pen", "🖊️"},
    {"paperclip", "📎"},
    {"scissors", "✂️"},
    {"pushpin", "📌"},
    {"round_pushpin", "📍"},
    {"calendar", "📅"},
    {"chart", "📈"},
    {"chart_with_downwards_trend", "📉"},
    {"clipboard", "📋"},
    {"memo", "📝"},
    {"file_folder", "📁"},
    {"open_file_folder", "📂"},
    {"wastebasket", "🗑️"},

    // Checkmarks & Status
    {"white_check_mark", "✅"},
    {"check", "✔️"},
    {"x", "❌"},
    {"heavy_multiplication_x", "✖️"},
    {"warning", "⚠️"},
    {"no_entry", "⛔"},
    {"no_entry_sign", "🚫"},
    {"question", "❓"},
    {"exclamation", "❗"},
    {"bangbang", "‼️"},
    {"interrobang", "⁉️"},
    {"100", "💯"},
    {"1234", "🔢"},
    {"abc", "🔤"},
    {"abcd", "🔡"},
    {"new", "🆕"},
    {"free", "🆓"},
    {"up", "🆙"},
    {"cool", "🆒"},
    {"ok", "🆗"},
    {"sos", "🆘"},
    {"information_source", "ℹ️"},
    {"registered", "®️"},
    {"copyright", "©️"},
    {"tm", "™️"},

    // Arrows
    {"arrow_up", "⬆️"},
    {"arrow_down", "⬇️"},
    {"arrow_left", "⬅️"},
    {"arrow_right", "➡️"},
    {"arrow_upper_left", "↖️"},
    {"arrow_upper_right", "↗️"},
    {"arrow_lower_left", "↙️"},
    {"arrow_lower_right", "↘️"},
    {"left_right_arrow", "↔️"},
    {"arrow_up_down", "↕️"},
    {"arrows_counterclockwise", "🔄"},
    {"rewind", "⏪"},
    {"fast_forward", "⏩"},
    {"play_or_pause_button", "⏯️"},
    {"arrow_forward", "▶️"},
    {"arrow_backward", "◀️"},

    // Music & Media
    {"musical_note", "🎵"},
    {"notes", "🎶"},
    {"microphone", "🎤"},
    {"headphones", "🎧"},
    {"radio", "📻"},
    {"guitar", "🎸"},
    {"piano", "🎹"},
    {"trumpet", "🎺"},
    {"violin", "🎻"},
    {"drum", "🥁"},
    {"movie_camera", "🎥"},
    {"clapper", "🎬"},
    {"camera", "📷"},
    {"video_camera", "📹"},
    {"tv", "📺"},
    {"vhs", "📼"},

    // Sports & Activities
    {"soccer", "⚽"},
    {"basketball", "🏀"},
    {"football", "🏈"},
    {"baseball", "⚾"},
    {"tennis", "🎾"},
    {"volleyball", "🏐"},
    {"rugby_football", "🏉"},
    {"golf", "⛳"},
    {"ping_pong", "🏓"},
    {"badminton", "🏸"},
    {"hockey", "🏒"},
    {"ice_skate", "⛸️"},
    {"ski", "🎿"},
    {"snowboarder", "🏂"},
    {"swimmer", "🏊"},
    {"surfer", "🏄"},
    {"fishing_pole_and_fish", "🎣"},
    {"running_shirt_with_sash", "🎽"},
    {"dart", "🎯"},
    {"game_die", "🎲"},
    {"bowling", "🎳"},
    {"video_game", "🎮"},
    {"slot_machine", "🎰"},

    // Time & Numbers
    {"clock1", "🕐"},
    {"clock2", "🕑"},
    {"clock3", "🕒"},
    {"clock4", "🕓"},
    {"clock5", "🕔"},
    {"clock6", "🕕"},
    {"clock7", "🕖"},
    {"clock8", "🕗"},
    {"clock9", "🕘"},
    {"clock10", "🕙"},
    {"clock11", "🕚"},
    {"clock12", "🕛"},
    {"one", "1️⃣"},
    {"two", "2️⃣"},
    {"three", "3️⃣"},
    {"four", "4️⃣"},
    {"five", "5️⃣"},
    {"six", "6️⃣"},
    {"seven", "7️⃣"},
    {"eight", "8️⃣"},
    {"nine", "9️⃣"},
    {"keycap_ten", "🔟"},
    {"zero", "0️⃣"},
    {"hash", "#️⃣"},
    {"asterisk", "*️⃣"},

    // Flags (common)
    {"flag_us", "🇺🇸"},
    {"flag_gb", "🇬🇧"},
    {"flag_ca", "🇨🇦"},
    {"flag_au", "🇦🇺"},
    {"flag_de", "🇩🇪"},
    {"flag_fr", "🇫🇷"},
    {"flag_es", "🇪🇸"},
    {"flag_it", "🇮🇹"},
    {"flag_jp", "🇯🇵"},
    {"flag_cn", "🇨🇳"},
    {"flag_kr", "🇰🇷"},
    {"flag_in", "🇮🇳"},
    {"flag_br", "🇧🇷"},
    {"flag_mx", "🇲🇽"},
    {"checkered_flag", "🏁"},
    {"triangular_flag_on_post", "🚩"},
    {"crossed_flags", "🎌"},
    {"black_flag", "🏴"},
    {"white_flag", "🏳️"},
    {"rainbow_flag", "🏳️‍🌈"},
    {"pirate_flag", "🏴‍☠️"},

    // Misc
    {"plus", "➕"},
    {"minus", "➖"},
    {"heavy_division_sign", "➗"},
    {"infinity", "♾️"},
    {"recycle", "♻️"},
    {"trident", "🔱"},
    {"fleur_de_lis", "⚜️"},
    {"beginner", "🔰"},
    {"part_alternation_mark", "〽️"},
    {"atom_symbol", "⚛️"},
    {"peace_symbol", "☮️"},
    {"yin_yang", "☯️"},
    {"star_of_david", "✡️"},
    {"wheel_of_dharma", "☸️"},
    {"om", "🕉️"},
    {"latin_cross", "✝️"},
    {"orthodox_cross", "☦️"},
    {"star_and_crescent", "☪️"},
    {"six_pointed_star", "🔯"},
    {"menorah", "🕎"},
    {"zzz", "💤"},
    {"anger", "💢"},
    {"bomb", "💣"},
    {"hole", "🕳️"},
    {"speech_balloon", "💬"},
    {"thought_balloon", "💭"},

    // Programming & Tech
    {"desktop_computer", "🖥️"},
    {"printer", "🖨️"},
    {"floppy_disk", "💾"},
    {"cd", "💿"},
    {"dvd", "📀"},
    {"battery", "🔋"},
    {"electric_plug", "🔌"},
    {"satellite_antenna", "📡"},
    {"robot_face", "🤖"},
    {"gear", "⚙️"},
    {"link", "🔗"},
    {"chains", "⛓️"},
    {"toolbox", "🧰"},
    {"shield", "🛡️"},
    {"dagger", "🗡️"},
    {"crossed_swords", "⚔️"},

    {nullptr, nullptr}  // End marker
};

// Find emoji by name (without : delimiters)
static const char* find_emoji(const char* name, size_t len) {
    for (int i = 0; emoji_table[i].name; i++) {
        if (strlen(emoji_table[i].name) == len &&
            strncmp(emoji_table[i].name, name, len) == 0) {
            return emoji_table[i].utf8;
        }
    }
    return nullptr;
}

bool is_emoji_shortcode(const char* name, size_t len) {
    return find_emoji(name, len) != nullptr;
}

bool is_html_entity(const char* name, size_t len) {
    return html_entity_lookup(name, len) != nullptr;
}

SymbolResolution resolve_symbol(const char* name, size_t len) {
    SymbolResolution result = {};
    result.type = SYMBOL_UNKNOWN;
    result.utf8 = nullptr;
    result.utf8_len = 0;
    result.codepoint = 0;

    if (!name || len == 0) {
        return result;
    }

    // First check emoji (higher priority for symbols like "heart")
    const char* emoji_utf8 = find_emoji(name, len);
    if (emoji_utf8) {
        result.type = SYMBOL_EMOJI;
        result.utf8 = emoji_utf8;
        result.utf8_len = strlen(emoji_utf8);
        // Note: codepoint is 0 for emoji since many are multi-codepoint
        return result;
    }

    // Then check HTML entities
    const char* replacement = html_entity_lookup(name, len);
    if (replacement) {
        result.type = SYMBOL_HTML_ENTITY;
        result.utf8 = replacement;
        result.utf8_len = strlen(replacement);
        result.codepoint = utf8_first_codepoint(replacement);
        return result;
    }

    return result;
}

SymbolResolution resolve_symbol_string(const void* string_ptr) {
    SymbolResolution result = {};
    result.type = SYMBOL_UNKNOWN;

    if (!string_ptr) {
        return result;
    }

    const String* str = (const String*)string_ptr;
    return resolve_symbol(str->chars, str->len);
}
