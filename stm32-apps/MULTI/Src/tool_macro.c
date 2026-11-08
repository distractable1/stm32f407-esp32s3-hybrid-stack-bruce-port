#include "tool_macro.h"
#include "tool_hid.h"
#include <stdio.h>
#include <string.h>

enum CommandType { CMD_SKIP, CMD_TEXT, CMD_TEXTLN, CMD_DELAY, CMD_DEFAULT, CMD_KEY };
typedef struct {
    enum CommandType type;
    const char *text;
    unsigned length, delay;
    uint8_t modifiers, key;
} Command;

static char script[TOOL_MACRO_MAX_BYTES + 1];
static unsigned script_length, cursor, next_line, current_line;
static bool loaded, armed, running;
static enum { NEXT_LINE, TEXT, WAIT_REPORT, WAIT_DELAY } phase;
static const char *string_text;
static unsigned string_length, string_position, default_delay;
static bool string_enter;
static uint32_t deadline;
static char status_text[80] = "Load a script; nothing runs automatically";

static void status(const char *message)
{
    if (current_line) snprintf(status_text, sizeof status_text, "%s (line %u)", message, current_line);
    else snprintf(status_text, sizeof status_text, "%s", message);
}

static bool whitespace(char c) { return c == ' ' || c == '\t'; }
static char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }
static bool equal(const char *text, unsigned length, const char *word)
{
    unsigned i = 0;
    for (; word[i]; ++i) if (i >= length || upper(text[i]) != word[i]) return false;
    return i == length;
}

static uint8_t modifier(const char *token, unsigned length)
{
    if (equal(token, length, "CTRL") || equal(token, length, "CONTROL")) return 1;
    if (equal(token, length, "SHIFT")) return 2;
    if (equal(token, length, "ALT")) return 4;
    if (equal(token, length, "GUI") || equal(token, length, "WINDOWS") || equal(token, length, "WIN")) return 8;
    return 0;
}

static uint8_t keycode(const char *token, unsigned length, bool allow_character, uint8_t *modifiers)
{
    static const struct { const char *name; uint8_t code; } names[] = {
        {"ENTER",40},{"RETURN",40},{"ESC",41},{"ESCAPE",41},{"BACKSPACE",42},
        {"TAB",43},{"SPACE",44},{"CAPSLOCK",57},{"INSERT",73},{"HOME",74},
        {"PAGEUP",75},{"DELETE",76},{"DEL",76},{"END",77},{"PAGEDOWN",78},
        {"RIGHT",79},{"RIGHTARROW",79},{"LEFT",80},{"LEFTARROW",80},
        {"DOWN",81},{"DOWNARROW",81},{"UP",82},{"UPARROW",82}
    };
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (equal(token, length, names[i].name)) return names[i].code;
    if (length >= 2 && length <= 3 && upper(token[0]) == 'F') {
        unsigned n = 0;
        for (unsigned i = 1; i < length; ++i) {
            if (token[i] < '0' || token[i] > '9') return 0;
            n = n * 10u + (unsigned)(token[i] - '0');
        }
        if (n >= 1 && n <= 12) return (uint8_t)(57u + n);
    }
    if (allow_character && length == 1) {
        char c = upper(token[0]);
        if (c >= 'A' && c <= 'Z') return (uint8_t)(4 + c - 'A');
        if (c >= '1' && c <= '9') return (uint8_t)(30 + c - '1');
        if (c == '0') return 39;
        static const char plain[] = "-=[]\\;'`,./";
        static const char shifted[] = "_+{}|:\"~<>?";
        static const uint8_t codes[] = {45,46,47,48,49,51,52,53,54,55,56};
        for (unsigned i = 0; i < sizeof codes; ++i) {
            if (c == plain[i]) return codes[i];
            if (c == shifted[i]) { *modifiers |= 2; return codes[i]; }
        }
    }
    return 0;
}

static bool parse_line(const char *line, unsigned length, Command *command)
{
    memset(command, 0, sizeof *command);
    command->type = CMD_SKIP;
    unsigned p = 0;
    while (p < length && whitespace(line[p])) ++p;
    if (p == length || line[p] == '#' || (p + 1 < length && line[p] == '/' && line[p + 1] == '/')) return true;
    unsigned start = p;
    while (p < length && !whitespace(line[p])) ++p;
    unsigned token_length = p - start;
    if (equal(line + start, token_length, "REM")) return true;
    if (equal(line + start, token_length, "STRING") || equal(line + start, token_length, "STRINGLN")) {
        command->type = equal(line + start, token_length, "STRING") ? CMD_TEXT : CMD_TEXTLN;
        if (p < length) ++p; /* Preserve any further spaces in the literal text. */
        command->text = line + p; command->length = length - p;
        return true;
    }
    if (equal(line + start, token_length, "DELAY") || equal(line + start, token_length, "DEFAULT_DELAY") ||
        equal(line + start, token_length, "DEFAULTDELAY")) {
        command->type = equal(line + start, token_length, "DELAY") ? CMD_DELAY : CMD_DEFAULT;
        while (p < length && whitespace(line[p])) ++p;
        unsigned digits = 0, value = 0;
        while (p < length && line[p] >= '0' && line[p] <= '9') {
            unsigned digit = (unsigned)(line[p++] - '0');
            if (value > (60000u - digit) / 10u) return false;
            value = value * 10u + digit; ++digits;
        }
        while (p < length && whitespace(line[p])) ++p;
        if (!digits || p != length || value > 60000u) return false;
        command->delay = value;
        return true;
    }
    command->type = CMD_KEY;
    p = start;
    bool saw_modifier = false;
    while (p < length) {
        while (p < length && whitespace(line[p])) ++p;
        if (p == length) break;
        start = p;
        while (p < length && !whitespace(line[p])) ++p;
        uint8_t mod = modifier(line + start, p - start);
        if (mod && !command->key) { command->modifiers |= mod; saw_modifier = true; continue; }
        if (command->key) return false;
        command->key = keycode(line + start, p - start, saw_modifier, &command->modifiers);
        if (!command->key) return false;
    }
    return command->key != 0;
}

static unsigned line_end(const char *text, unsigned length, unsigned start, unsigned *next)
{
    unsigned end = start;
    while (end < length && text[end] != '\r' && text[end] != '\n') ++end;
    *next = end;
    if (*next < length && text[*next] == '\r') ++*next;
    if (*next < length && text[*next] == '\n') ++*next;
    return end;
}

bool tool_macro_load(const char *text, unsigned length)
{
    tool_macro_cancel(); loaded = false; current_line = 0;
    if (!text || !length || length > TOOL_MACRO_MAX_BYTES) { status("Script must be 1..8192 bytes"); return false; }
    unsigned position = 0, line = 1, commands = 0;
    while (position < length) {
        unsigned next, end = line_end(text, length, position, &next);
        current_line = line;
        for (unsigned i = position; i < end; ++i)
            if (((unsigned char)text[i] < 32 && text[i] != '\t') || (unsigned char)text[i] > 126) {
                status("Invalid ASCII"); return false;
            }
        Command command;
        if (!parse_line(text + position, end - position, &command)) {
            status("Unsupported command or invalid argument"); return false;
        }
        if (command.type != CMD_SKIP) ++commands;
        position = next; ++line;
    }
    if (!commands) { current_line = 0; status("Script has no commands"); return false; }
    memcpy(script, text, length); script[length] = 0; script_length = length;
    loaded = true; current_line = 0; status("Validated; ARM then physical RUN"); return true;
}

bool tool_macro_arm(void)
{
    if (!loaded || running) { status("Load and validate before arming"); return false; }
    if (!tool_hid_connected() || !tool_hid_idle()) { status("USB must be connected and idle"); return false; }
    armed = true; current_line = 0; status("Armed; physical RUN required"); return true;
}

bool tool_macro_run(void)
{
    if (!armed || !loaded || running || !tool_hid_connected()) { status("ARM with USB connected before RUN"); return false; }
    armed = false; running = true; cursor = 0; next_line = 1; current_line = 0;
    default_delay = 0; phase = NEXT_LINE; tool_hid_release(); status("Running"); return true;
}

void tool_macro_cancel(void)
{
    armed = running = false; tool_hid_release(); status("Canceled; all keys and buttons released");
}

void tool_macro_transport_lost(void)
{
    armed = running = false; tool_hid_release(); status("USB paused/disconnected; re-arm required");
}

bool tool_macro_busy(void) { return running; }
bool tool_macro_armed(void) { return armed; }
unsigned tool_macro_line(void) { return current_line; }
const char *tool_macro_status(void) { return status_text; }

static void input_finished(uint32_t now)
{
    if (default_delay) { deadline = now + default_delay; phase = WAIT_DELAY; }
    else phase = NEXT_LINE;
}

void tool_macro_task(uint32_t now)
{
    if (!running) return;
    if (!tool_hid_connected()) { tool_macro_transport_lost(); return; }
    if (phase == WAIT_DELAY) {
        if ((int32_t)(now - deadline) < 0) return;
        phase = NEXT_LINE;
    }
    if (!tool_hid_idle()) return;
    if (phase == WAIT_REPORT) { input_finished(now); return; }
    if (phase == TEXT) {
        if (string_position < string_length) {
            if (tool_hid_character((uint8_t)string_text[string_position])) ++string_position;
            return;
        }
        if (string_enter) { if (tool_hid_key(0, 40)) string_enter = false; return; }
        input_finished(now); return;
    }
    /* Bound comment/blank processing so an 8KiB comment file cannot starve UI. */
    for (unsigned budget = 0; budget < 8; ++budget) {
        if (cursor == script_length) {
            running = false; status("Finished; released"); return;
        }
        unsigned next, end = line_end(script, script_length, cursor, &next);
        current_line = next_line++;
        Command command;
        if (!parse_line(script + cursor, end - cursor, &command)) {
            tool_macro_cancel(); status("Script validation changed unexpectedly"); return;
        }
        cursor = next;
        switch (command.type) {
        case CMD_SKIP: break;
        case CMD_DEFAULT: default_delay = command.delay; break;
        case CMD_DELAY: deadline = now + command.delay; phase = WAIT_DELAY; status("Delay"); return;
        case CMD_TEXT:
        case CMD_TEXTLN:
            string_text = command.text; string_length = command.length; string_position = 0;
            string_enter = command.type == CMD_TEXTLN; phase = TEXT; status("Typing"); return;
        case CMD_KEY:
            if (!tool_hid_key(command.modifiers, command.key)) {
                tool_macro_cancel(); status("USB input could not be queued"); return;
            }
            phase = WAIT_REPORT; status("Sending key"); return;
        }
    }
}
