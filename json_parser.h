#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#define UNICODE
#include <windows.h>
#include <stdio.h>

#define CODEPOINT_EOF -1
#define CODEPOINT_MISSING_BYTE_2 -2
#define CODEPOINT_MISSING_BYTE_3 -3
#define CODEPOINT_MISSING_BYTE_4 -4
#define CODEPOINT_NON_UTF_8_BYTE -5
#define CODEPOINT_CONTINUATION -6
int getCodepoint(LPCBYTE start, LPCBYTE stop) {
    // https://en.wikipedia.org/wiki/UTF-8#Encoding
    if (start >= stop) { return CODEPOINT_EOF; }
    int codepoint = *start;
    if (codepoint < 0x80) { return codepoint; }
    if (codepoint < 0xc0) { return CODEPOINT_CONTINUATION; }
    if (codepoint >= 0xf8) { return CODEPOINT_NON_UTF_8_BYTE; }
    // count is number of continuation bytes
    // countStops[i] is one plus the highest initial byte that indicates there
    // are i continuation bytes
    static const int countStops[4] = { 0xc0, 0xe0, 0xf0, 0xf8 };
    start++;
    int count = 1;
    while (countStops[count] <= codepoint) { count++; }
    codepoint -= countStops[count - 1];
    if (stop < start + count) {
        return CODEPOINT_MISSING_BYTE_2 - (stop - start);
    }

    for (stop = start + count; start < stop; start++) {
        if (*start < 0x80 || *start >= 0xc0) {
            return CODEPOINT_MISSING_BYTE_2 - (start + count - stop);
        }

        codepoint <<= 6;
        codepoint += *start - 0x80;
    }

    if (
        (codepoint >= 0xd800 && codepoint < 0xe000) || codepoint >= 0x110000
    ) { return -codepoint; }
    return codepoint;
}

int getCodepointUTF8Length(int codepoint) {
    return (codepoint >= 0) + (codepoint >= 0x80) + (codepoint >= 0x800)
        + (codepoint >= 0x10000);
}

#define CODEPOINT_BAD_ESCAPE -7
#define CODEPOINT_BAD_U_ESCAPE -8
#define CODEPOINT_SURROGATE_U_ESCAPE -9
int getCodepointEscaped(LPCBYTE *json, LPCBYTE stop) {
    if (*json + 2 > stop || **json != '\\') {
        int codepoint = getCodepoint(*json, stop);
        *json += getCodepointUTF8Length(codepoint);
        return codepoint;
    }

    (*json)++;
    if (**json != 'u') {
        LPCBYTE escapePair = "\"\"\\\\//b\bf\fn\nr\rt\t";
        for (; *escapePair != '\0'; escapePair += 2) {
            if (**json == *escapePair) {
                (*json)++;
                return (int)escapePair[1];
            }
        }

        return CODEPOINT_BAD_ESCAPE;
    }

    (*json)++;
    if (stop < *json + 4) { return CODEPOINT_BAD_U_ESCAPE; }
    int codepoint = 0;
    for (LPCBYTE i = *json; i < *json + 4; i++) {
        codepoint <<= 4;
        if (*i >= '0' && *i <= '9') { codepoint += *i - '0'; }
        else if (*i >= 'a' && *i <= 'f') { codepoint += 10 + *i - 'a'; }
        else if (*i >= 'A' && *i <= 'F') { codepoint += 10 + *i - 'A'; }
        else { return CODEPOINT_BAD_U_ESCAPE; }
    }

    if (codepoint >= 0xd800 && codepoint < 0xe000) {
        return CODEPOINT_SURROGATE_U_ESCAPE;
    }

    *json += 4;
    return codepoint;
}

int getCodepointUTF16Length(int codepoint) {
    // number of wide chars
    return (codepoint >= 0) + (codepoint >= 0x10000);
}

LPWSTR writeUTF16Codepoint(LPWSTR start, LPCWSTR stop, int codepoint) {
    // https://en.wikipedia.org/wiki/UTF-16
    if (
        codepoint < 0 || codepoint >= 0x110000 || (
            codepoint >= 0xd800 && codepoint < 0xe000
        )
    ) { return start; }
    if (start >= stop) { return NULL; }
    if (codepoint < 0x10000) { *start = codepoint; return start + 1; }
    if (start + 2 > stop) { return NULL; }
    *start = 0xD800 + (codepoint >> 10);
    start[1] = 0xDC00 + (codepoint & 0x3ff);
    return start + 2;
}

typedef enum {
    VALUE_CONTEXT = 0, FIRST_KEY_CONTEXT, KEY_CONTEXT, CONTEXT_COUNT,
} StringContext;
LPWSTR getCodepointError(int codepoint, StringContext context) {
    if (codepoint == CODEPOINT_EOF) {
        LPWSTR eofErrors[CONTEXT_COUNT] = {
            L"Unexpected %2$s in %1$s",
            L"Unexpected %2$s in first key of %1$s object",
            L"Unexpected %2$s in key after %1$s",
        };
        return eofErrors[context];
    } else if (
        codepoint == CODEPOINT_MISSING_BYTE_2
            || codepoint == CODEPOINT_MISSING_BYTE_3
            || codepoint == CODEPOINT_MISSING_BYTE_4
            || codepoint == CODEPOINT_NON_UTF_8_BYTE
            || codepoint == CODEPOINT_CONTINUATION
            || -codepoint >= 0x110000
            || (-codepoint >= 0xd800 && -codepoint < 0xe000)
    ) {
        LPWSTR invalidErrors[CONTEXT_COUNT] = {
            L"%2$s in %1$s",
            L"%2$s in first key of %1$s object",
            L"%2$s in key after %1$s",
        };
        return invalidErrors[context];
    } else if (codepoint >= 0 && codepoint < 0x20) {
        LPWSTR unescapedErrors[CONTEXT_COUNT] = {
            L"Unescaped %2$s in %1$s",
            L"Unescaped %2$s in first key of %1$s object",
            L"Unescaped %2$s in key after %1$s",
        };
        return unescapedErrors[context];
    } else if (codepoint == CODEPOINT_BAD_ESCAPE) {
        LPWSTR escapeErrors[CONTEXT_COUNT] = {
            L"Invalid escape sequence \\%2$.1s in %1$s",
            L"Invalid escape sequence \\%2$.1s in first key of %1$s"
                L" object",
            L"Invalid escape sequence \\%2$.1s in key after %1$s",
        };
        return escapeErrors[context];
    } else if (codepoint == CODEPOINT_BAD_U_ESCAPE) {
        LPWSTR escapeErrors[CONTEXT_COUNT] = {
            L"Invalid escape sequence \\u%2$.4s in %1$s",
            L"Invalid escape sequence \\u%2$.4s in first key"
                L" of %1$s object",
            L"Invalid escape sequence \\u%2$.4s in key after"
                L" %1$s",
        };
        return escapeErrors[context];
    } else if (codepoint == CODEPOINT_SURROGATE_U_ESCAPE) {
        LPWSTR invalidErrors[CONTEXT_COUNT] = {
            L"Invalid code point U+%2$.4s in %1$s",
            L"Invalid code point U+%2$.4s in first key of %1$s object",
            L"Invalid code point U+%2$.4s in key after %1$s",
        };
        return invalidErrors[context];
    } else { return NULL; }
}

BOOL chomp(BYTE bite, LPCBYTE *json, LPCBYTE stop) {
    if (*json < stop && **json == bite) { *json += 1; return TRUE; }
    return FALSE;
}

BOOL chompRange(BYTE start, BYTE end, LPCBYTE *json, LPCBYTE stop) {
    if (*json < stop && **json >= start && **json <= end) {
        *json += 1;
        return TRUE;
    }

    return FALSE;
}

BOOL chompAny(LPCBYTE bite, LPCBYTE *json, LPCBYTE stop) {
    if (*json >= stop) { return FALSE; }
    for (; *bite != '\0'; bite++) {
        if (**json == *bite) { *json += 1; return TRUE; }
    }

    return FALSE;
}

BOOL chompToken(LPCBYTE bite, LPCBYTE *json, LPCBYTE stop) {
    int length = strlen((LPCSTR)bite);
    if (
        *json + length <= stop
            && !strncmp((LPCSTR)*json, (LPCSTR)bite, length)
    ) {
        *json += length;
        return TRUE;
    }

    return FALSE;
}

BOOL parseIntInternal(LPCBYTE *json, LPCBYTE stop, int *integer) {
    BOOL negative = chomp('-', json, stop);
    if (chomp('0', json, stop)) { *integer = 0; return TRUE; }
    if (*json >= stop || **json < '1' || **json > '9') { return FALSE; }
    *integer = **json - '0';
    *json += 1;
    if (negative) {
        *integer = -*integer;
        for (; *json < stop && **json >= '0' && **json <= '9'; *json += 1) {
            int digit = **json - '0';
            if (*integer < (INT_MIN + digit) / 10) { return TRUE; }
            *integer *= 10; *integer -= digit;
        }
    } else {
        for (; *json < stop && **json >= '0' && **json <= '9'; *json += 1) {
            int digit = **json - '0';
            if (*integer > (INT_MAX - digit) / 10) { return TRUE; }
            *integer *= 10; *integer += digit;
        }
    }

    return TRUE;
}

typedef enum {
    JSON_UNKNOWN = 0, JSON_OBJECT = 0x1, JSON_ARRAY = 0x2, JSON_STRING = 0x4,
    JSON_NUMBER = 0x8, JSON_INT = 0x10, JSON_BOOL = 0x20, JSON_NULL = 0x40,
} JSON_TYPE;
#define JSON_TYPE_COUNT 7
#define MAX_JSON_TYPE_LENGTH 48
/*
object, array, string, number, boolean, or null0
*/
WCHAR jsonTypeOut[MAX_JSON_TYPE_LENGTH];
LPCWSTR jsonTypeToString(JSON_TYPE jsonType) {
    LPCWSTR strings[JSON_TYPE_COUNT];
    int count = 0;
    if (jsonType & JSON_OBJECT) { strings[count] = L"object"; count++; }
    if (jsonType & JSON_ARRAY) { strings[count] = L"array"; count++; }
    if (jsonType & JSON_STRING) { strings[count] = L"string"; count++; }
    if (jsonType & JSON_NUMBER) {
        strings[count] = L"number"; count++;
    } else if (jsonType & JSON_INT) { strings[count] = L"int"; count++; }
    if (jsonType & JSON_BOOL) { strings[count] = L"boolean"; count++; }
    if (jsonType & JSON_NULL) { strings[count] = L"null"; count++; }
    if (count <= 0) { return L"unknown"; }
    if (count <= 1) { return strings[0]; }
    if (count <= 2) {
        _snwprintf_s(
            jsonTypeOut, MAX_JSON_TYPE_LENGTH, _TRUNCATE,
            L"%s or %s", strings[0], strings[1]
        );
        return jsonTypeOut;
    }

    int offset = 0;
    for (int i = 0; i < count - 1; i++) {
        _snwprintf_s(
            jsonTypeOut + offset, MAX_JSON_TYPE_LENGTH - offset, _TRUNCATE,
            L"%s, ", strings[i]
        );
        offset += wcsnlen_s(
            jsonTypeOut + offset, MAX_JSON_TYPE_LENGTH - offset
        );
    }
    _snwprintf_s(
        jsonTypeOut + offset, MAX_JSON_TYPE_LENGTH - offset, _TRUNCATE,
        L"or %s", strings[count - 1]
    );
    return jsonTypeOut;
}

JSON_TYPE parseType(LPCBYTE *json, LPCBYTE stop) {
    if (*json >= stop) { return JSON_UNKNOWN; }
    if (**json == '{') { return JSON_OBJECT; }
    if (**json == '[') { return JSON_ARRAY; }
    if (**json == '"') { return JSON_STRING; }
    JSON_TYPE result = JSON_UNKNOWN;
    LPCBYTE start = *json;
    int integer;
    if (chompToken("null", json, stop)) {
        result = JSON_NULL;
    } else if (
        chompToken("true", json, stop) || chompToken("false", json, stop)
    ) {
        result = JSON_BOOL;
    } else if (parseIntInternal(json, stop, &integer)) {
        result = (JSON_TYPE)(JSON_INT | JSON_NUMBER);
        if (integer != 0) {
            while (chompRange('0', '9', json, stop)) { result = JSON_NUMBER; }
        }

        if (chomp('.', json, stop)) {
            result = JSON_UNKNOWN;
            while (chompRange('0', '9', json, stop)) { result = JSON_NUMBER; }
        }

        if (result != JSON_UNKNOWN && chompAny("eE", json, stop)) {
            result = JSON_UNKNOWN;
            chompAny("+-", json, stop);
            while (chompRange('0', '9', json, stop)) { result = JSON_NUMBER; }
        }
    }

    if (
        result == JSON_UNKNOWN
            || chompRange('0', '9', json, stop)
            || chompRange('a', 'z', json, stop)
            || chompRange('A', 'Z', json, stop)
            || chompAny("_.+-", json, stop)
    ) { *json = start; return JSON_UNKNOWN; }
    return result;
}

LPCWSTR parseString(LPCBYTE *json, LPCBYTE stop, StringContext context) {
    if (!chomp('"', json, stop)) {
        if (*json >= stop) {
            LPCWSTR errors[CONTEXT_COUNT] = {
                L"Missing %1$s string before %2$s",
                L"Unexpected %2$s at start of %1$s object",
                L"Unexpected %2$s after %1$s",
            };
            return errors[context];
        } else {
            LPCWSTR errors[CONTEXT_COUNT] = {
                L"%1$s string must start with \", not %2$s",
                L"First key of %1$s object must start with \", not %2$s",
                L"Key after %1$s must start with \", not %2$s",
            };
            return errors[context];
        }
    }

    int codepoint = 0x20;
    while (codepoint >= 0x20) {
        if (chomp('"', json, stop)) { return NULL; }
        codepoint = getCodepointEscaped(json, stop);
    }

    return getCodepointError(codepoint, context);
}

int compareHookWithStack(LPCBYTE hookKey, LPCBYTE stackKey) {
    if (hookKey == NULL || stackKey == NULL) {
        return (hookKey != NULL) - (stackKey != NULL);
    }

    int stackKeyLength = 0;
    while (stackKey[1 + stackKeyLength] != '"') { stackKeyLength++; }
    int comparison = strncmp(
        (LPCSTR)hookKey, (LPCSTR)stackKey + 1, stackKeyLength
    );
    if (comparison != 0) { return comparison; }
    return hookKey[stackKeyLength] != '\0';
}

#define HOOK_FRAME_COUNT 5
typedef struct {
    LPCWSTR (*call)(
        LPCBYTE *json, LPCBYTE stop, JSON_TYPE jsonType, int index,
        void* param, void *dest, BOOL errorVisible
    );
    void* param;
    void* dest;
    LPCBYTE frames[HOOK_FRAME_COUNT];
    int frameCount;
} Hook;
int hookBinarySearch(
    Hook *hooks, int count, LPCBYTE key, int offset, BOOL right
) {
    // returns an index in the range [0, count]
    // right=0: index of the first hook where frames[offset] >= key
    // right=1: index of the first hook where frames[offset] > key
    // returns count if no hook has frameCount > offset
    int lo = 0;
    int hi = count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (
            // equal if stack is a sub-array of hook but not vice versa
            hooks[mid].frameCount <= offset
                || compareHookWithStack(hooks[mid].frames[offset], key)
                    < right
        ) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

#define MAX_FRAME_COUNT 250
typedef struct { int index; LPCBYTE key; } Frame;
typedef struct {
    Frame frames[MAX_FRAME_COUNT];
    int count;
    int poisonedCount;
    LPCWSTR warning;
    LPCBYTE warningLocation;
    Frame warningFrames[MAX_FRAME_COUNT];
    int warningCount;
} Stack;
typedef struct { Stack *stack; Hook *hooks; int count; } ParserState;
ParserState addFrame(ParserState state, int index, LPCBYTE key) {
    if (state.stack->count >= MAX_FRAME_COUNT) {
        state.stack = NULL; return state;
    }

    state.stack->frames[state.stack->count].index = index;
    state.stack->frames[state.stack->count].key = key;
    int start = hookBinarySearch(
        state.hooks, state.count, key, state.stack->count, FALSE
    );
    state.hooks += start;
    state.count = hookBinarySearch(
        state.hooks, state.count - start, key, state.stack->count, TRUE
    );
    if (state.stack->poisonedCount > state.stack->count) {
        state.stack->poisonedCount = state.stack->count;
    }

    state.stack->count++;
    return state;
}

// https://stackoverflow.com/a/798311
#define STRINGIFY_HELP(x) L#x
#define STRINGIFY(x) STRINGIFY_HELP(x)
LPCBYTE WHITESPACE = " \n\r\t";
LPCWSTR parseValue(LPCBYTE *json, LPCBYTE stop, const ParserState *state);

LPCWSTR parseArray(LPCBYTE *json, LPCBYTE stop, const ParserState *state) {
    if (!chomp('[', json, stop)) {
        if (*json >= stop) { return L"Missing %1$s array before %2$s"; }
        return L"%1$s array must start with opening brace, not %2$s";
    }

    while (chompAny(WHITESPACE, json, stop));
    if (chomp(']', json, stop)) { return NULL; }
    for (int i = 0; TRUE; i++) {
        ParserState newState = addFrame(*state, i, NULL);
        if (newState.stack == NULL) {
            return L"JSON structure is more than " STRINGIFY(MAX_FRAME_COUNT)
                L" levels deep";
        }

        LPCWSTR error = parseValue(json, stop, &newState);
        if (error) { return error; }
        while (chompAny(WHITESPACE, json, stop));
        if (chomp(']', json, stop)) { state->stack->count--; return NULL; }
        LPCBYTE beforeComma = *json;
        if (*json >= stop) {
            return L"Unexpected %2$s after %1$s";
        } else if (!chomp(',', json, stop)) {
            return L"Expected comma or closing bracket after %1$s, not %2$s";
        }

        while (chompAny(WHITESPACE, json, stop));
        if (*json < stop && **json == ']' || **json == ',') {
            if (**json == ']') { *json = beforeComma; }
            return L"Extra comma after %1$s";
        }

        state->stack->count--;
    }
}

LPCWSTR parseObject(LPCBYTE *json, LPCBYTE stop, const ParserState *state) {
    if (!chomp('{', json, stop)) {
        if (*json >= stop) { return L"Missing %1$s object before %2$s"; }
        return L"%1$s object must start with opening brace, not %2$s";
    }

    while (chompAny(WHITESPACE, json, stop));
    if (chomp('}', json, stop)) { return NULL; }
    StringContext context = FIRST_KEY_CONTEXT;
    while (TRUE) {
        LPCBYTE key = *json;
        LPCWSTR error = parseString(json, stop, context);
        if (error) { return error; }
        if (context == KEY_CONTEXT) { state->stack->count--; }
        context = KEY_CONTEXT;
        ParserState newState = addFrame(*state, -1, key);
        if (newState.stack == NULL) {
            return L"JSON structure is more than " STRINGIFY(MAX_FRAME_COUNT)
                L" levels deep";
        }

        while (chompAny(WHITESPACE, json, stop));
        if (!chomp(':', json, stop)) {
            return L"Expected colon after %1$s key, not %2$s";
        }

        error = parseValue(json, stop, &newState);
        if (error) { return error; }
        while (chompAny(WHITESPACE, json, stop));
        if (chomp('}', json, stop)) { state->stack->count--; return NULL; }
        LPCBYTE beforeComma = *json;
        if (*json >= stop) {
            return L"Unexpected %2$s after %1$s";
        } else if (!chomp(',', json, stop)) {
            return L"Expected comma or closing brace after %1$s value, "
                L"not %2$s";
        }

        while (chompAny(WHITESPACE, json, stop));
        if (*json < stop && **json == '}' || **json == ',') {
            if (**json == '}') { *json = beforeComma; }
            return L"Extra comma after %1$s value";
        }
    }
}

LPCWSTR parseValue(LPCBYTE *json, LPCBYTE stop, const ParserState *state) {
    while (chompAny(WHITESPACE, json, stop));
    if (*json >= stop) { return L"Missing %1$s value before %2$s"; }
    LPCBYTE start = *json;
    JSON_TYPE jsonType = parseType(json, stop);
    if (jsonType == JSON_UNKNOWN) {
        return L"Could not parse %1$s value %2$s";
    }

    LPCWSTR error = NULL;
    if (jsonType == JSON_OBJECT) {
        error = parseObject(json, stop, state);
    } else if (jsonType == JSON_ARRAY) {
        error = parseArray(json, stop, state);
    } else if (jsonType == JSON_STRING) {
        error = parseString(json, stop, VALUE_CONTEXT);
    }

    if (error != NULL) { return error; }

    if (
        state->count > 0
            && state->stack->count >= state->hooks[0].frameCount
            && state->stack->count > state->stack->poisonedCount
    ) {
        int index = -1;
        if (state->stack->count > 0) {
            index = state->stack->frames[state->stack->count - 1].index;
        }

        // Let hooks know if their error won't be displayed so they can
        // avoid overwriting the internal buffer that a previous error was
        // stored in
        LPCWSTR warning = state->hooks[0].call(
            &start, *json, jsonType, index, state->hooks[0].param,
            state->hooks[0].dest,
            state->stack->warning == NULL // errorVisible
        );
        if (warning != NULL) {
            state->stack->poisonedCount = state->stack->count;
            if (state->stack->warning == NULL) {
                // save stack trace of first warning only
                state->stack->warning = warning;
                state->stack->warningLocation = start;
                memcpy(
                    state->stack->warningFrames, state->stack->frames,
                    state->stack->count * sizeof(Frame)
                );
                state->stack->warningCount = state->stack->count;
            }
        }
    }

    return NULL;
}

LPCWSTR parseJSONHelp(LPCBYTE *json, LPCBYTE stop, const ParserState *state) {
    // https://en.wikipedia.org/wiki/Byte_order_mark#Usage
    if (*json + 2 < stop) {
        if (**json == 0xfe && json[0][1] == 0xff) {
            return L"Found byte order mark indicating UTF-16BE encoding. "
                L"Please save the file with UTF-8 encoding.";
        } else if (**json == 0xff && json[0][1] == 0xfe) {
            return L"Found byte order mark indicating UTF-16LE encoding. "
                L"Please save the file with UTF-8 encoding.";
        } else if (**json == 0 && json[0][1] != 0) {
            return L"Found null byte suggesting UTF-16BE encoding. "
                L"Please save the file with UTF-8 encoding.";
        } else if (**json != 0 && json[0][1] == 0) {
            return L"Found null byte suggesting UTF-16LE encoding. "
                L"Please save the file with UTF-8 encoding.";
        }
    }

    chompToken("\xEF\xBB\xBF", json, stop);
    LPCWSTR error = parseValue(json, stop, state);
    if (error) { return error; }
    while (chompAny(WHITESPACE, json, stop));
    if (*json < stop) {
        return L"Expected end of file after %1$s value, not %2$s";
    }

    return NULL;
}

BOOL isTokenPart(int codepoint) {
    return (codepoint >= (int)'0' && codepoint <= (int)'9')
        || (codepoint >= (int)'a' && codepoint <= (int)'z')
        || (codepoint >= (int)'A' && codepoint <= (int)'Z')
        || codepoint == (int)'_' || codepoint == (int)'.'
        || codepoint == (int)'+' || codepoint == (int)'-';
}

#define MAX_TOKEN_LENGTH 80
WCHAR tokenOut[MAX_TOKEN_LENGTH];
LPCWSTR getToken(LPCBYTE json, LPCBYTE stop, BOOL uppercase) {
    int codepoint = getCodepoint(json, stop);
    LPCWSTR iChar = uppercase ? L"I" : L"i";
    if (codepoint >= 0 && codepoint < 0x20) {
        LPCWSTR names[0x20] = {
            L"null character", L"U+0001", L"U+0002", L"U+0003", L"U+0004",
            L"U+0005", L"U+0006", L"U+0007", L"U+0008", L"tab character",
            L"newline", L"U+000B", L"U+000C", L"carriage return", L"U+000E",
            L"U+000F", L"U+0010", L"U+0011", L"U+0012", L"U+0013", L"U+0014",
            L"U+0015", L"U+0016", L"U+0017", L"U+0018", L"U+0019", L"U+001A",
            L"U+001B", L"U+001C", L"U+001D", L"U+001E", L"U+001F",
        };
        wcsncpy_s(tokenOut, MAX_TOKEN_LENGTH, names[codepoint], _TRUNCATE);
        if (uppercase) {
            *tokenOut = L"NUUUUUUUUTNUUCUUUUUUUUUUUUUUUUUU"[codepoint];
        }

        return tokenOut;
    } else if (codepoint == CODEPOINT_EOF) {
        wcsncpy_s(tokenOut, MAX_TOKEN_LENGTH, L"end of file", _TRUNCATE);
        if (uppercase) { *tokenOut = L'E'; }
        return tokenOut;
    } else if (codepoint == CODEPOINT_MISSING_BYTE_2) {
        _snwprintf_s(
            tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
            L"%sncomplete UTF-8 character 0x%02x", iChar, *json
        );
        return tokenOut;
    } else if (codepoint == CODEPOINT_MISSING_BYTE_3) {
        _snwprintf_s(
            tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
            L"%sncomplete UTF-8 character 0x%02x%02x", iChar, *json, json[1]
        );
        return tokenOut;
    } else if (codepoint == CODEPOINT_MISSING_BYTE_4) {
        _snwprintf_s(
            tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
            L"%sncomplete UTF-8 character 0x%02x%02x%02x",
            iChar, *json, json[1], json[2]
        );
        return tokenOut;
    } else if (codepoint == CODEPOINT_NON_UTF_8_BYTE) {
        _snwprintf_s(
            tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
            L"%snvalid UTF-8 byte 0x%02x", iChar, *json, json[1]
        );
        return tokenOut;
    } else if (codepoint == CODEPOINT_CONTINUATION) {
        _snwprintf_s(
            tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
            L"unexpected continuation byte 0x%02x", *json, json[1]
        );
        if (uppercase) { *tokenOut = L'U'; }
        return tokenOut;
    } else if (
        -codepoint >= 0x110000
            || (-codepoint >= 0xd800 && -codepoint < 0xe000)
    ) {
        _snwprintf_s(
            tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
            L"%snvalid code point U+%X", iChar, -codepoint
        );
        return tokenOut;
    }

    LPWSTR writer = tokenOut;
    LPCWSTR writerStop = tokenOut + MAX_TOKEN_LENGTH - 1;
    LPWSTR oldWriter = writer;
    LPCBYTE temp = json;
    int ellipsisLength = getCodepointUTF16Length(0x2026);
    do {
        LPWSTR newWriter = writeUTF16Codepoint(writer, writerStop, codepoint);
        if (newWriter == NULL) {
            // write horizontal ellipsis
            writer = writeUTF16Codepoint(oldWriter, writerStop, 0x2026);
            break;
        }

        if (writerStop - writer >= ellipsisLength) { oldWriter = writer; }
        writer = newWriter;
        if (isTokenPart(codepoint)) {
            temp += getCodepointUTF8Length(codepoint);
            codepoint = getCodepoint(temp, stop);
        }
    } while (isTokenPart(codepoint));
    *writer = L'\0';
    return tokenOut;
}

BOOL isKeyPart(int codepoint) {
    return (codepoint >= (int)'0' && codepoint <= (int)'9')
        || (codepoint >= (int)'a' && codepoint <= (int)'z')
        || (codepoint >= (int)'A' && codepoint <= (int)'Z')
        || codepoint == (int)'_';
}

#define MAX_STACK_LENGTH 150
WCHAR stackOut[MAX_STACK_LENGTH];
LPCWSTR getStack(const Stack *stack, LPCBYTE stop, BOOL uppercase) {
    if (stack->count <= 0) { return uppercase ? L"Top-level" : L"top-level"; }
    LPWSTR stackStop = stackOut + MAX_STACK_LENGTH - 1;
    *stackStop = L'\0';
    int ellipsisLength = getCodepointUTF16Length(0x2026);
    int quoteLength = getCodepointUTF16Length((int)'"');
    int quoteLengthUTF8 = getCodepointUTF8Length((int)'"');
    int dotLength = getCodepointUTF16Length((int)'.');
    for (int i = stack->count - 1; i >= 0; i--) {
        LPCBYTE key = stack->frames[i].key;
        int index = stack->frames[i].index;
        LPWSTR stackStart = i > 0 ? stackOut + ellipsisLength : stackOut;
        int stackCapacity = stackStop - stackStart;
        if (key) {
            LPCBYTE keyStop = key + quoteLengthUTF8;
            LPCBYTE truncatedKeyStop = keyStop;
            int codepoint = getCodepoint(keyStop, stop);
            int truncatedStackLength = ellipsisLength;
            int stackLength = i > 0 ? dotLength : 0;
            int truncatedSkipQuotes = TRUE;
            BOOL skipQuotes = codepoint < (int)'0' || codepoint > (int)'9';
            if (!skipQuotes) { stackLength += 2 * quoteLength; }
            BOOL truncated = FALSE;
            if (codepoint == (int)'"') {
                skipQuotes = FALSE;
                stackLength += 2 * quoteLength;
                if (stackLength > stackCapacity) {
                    keyStop = truncatedKeyStop;
                    stackLength = truncatedStackLength;
                    skipQuotes = truncatedSkipQuotes;
                    truncated = TRUE;
                }
            }

            BOOL escaped = FALSE;
            while (codepoint != (int)'"' || escaped) {
                keyStop += getCodepointUTF8Length(codepoint);
                stackLength += getCodepointUTF16Length(codepoint);
                if (stackLength > stackCapacity || codepoint < 0) {
                    keyStop = truncatedKeyStop;
                    stackLength = truncatedStackLength;
                    skipQuotes = truncatedSkipQuotes;
                    truncated = TRUE;
                    break;
                }

                escaped = codepoint == (int)'\\';
                if (skipQuotes && !isKeyPart(codepoint)) {
                    skipQuotes = FALSE;
                    stackLength += 2 * quoteLength;
                }

                if (stackLength + ellipsisLength <= stackCapacity) {
                    truncatedKeyStop = keyStop;
                    truncatedStackLength = stackLength + ellipsisLength;
                    truncatedSkipQuotes = skipQuotes;
                }

                codepoint = getCodepoint(keyStop, stop);
            }

            if (skipQuotes) {
                key += quoteLengthUTF8;
            }

            if (!skipQuotes) {
                writeUTF16Codepoint(
                    stackStop - quoteLength, stackStop, (int)'"'
                );
                stackStop -= quoteLength;
                stackLength -= quoteLength;
            }

            if (truncated) {
                writeUTF16Codepoint(
                    stackStop - ellipsisLength, stackStop, 0x2026
                );
                stackStop -= ellipsisLength;
                stackLength -= ellipsisLength;
            }

            stackStart = stackStop - stackLength;
            LPWSTR writer = stackStart;
            if (i > 0 && stackLength > 0) {
                writer = writeUTF16Codepoint(writer, stackStop, (int)'.');
            }

            while (key < keyStop) {
                codepoint = getCodepoint(key, keyStop);
                writer = writeUTF16Codepoint(writer, stackStop, codepoint);
                key += getCodepointUTF8Length(codepoint);
            }

            stackStop = stackStart;
            if (stackLength == 0) { break; }
        } else {
            int digitCount = index <= 0;
            for (int test = abs(index); test > 0; test /= 10) {
                digitCount++;
            }
            if ((digitCount) > 1) {
                digitCount = digitCount;
            }

            LPWSTR digitStart = stackStop - 1 - digitCount;
            LPWSTR stackStart = digitStart - 1 - (i > 0);
            if (stackStart < stackOut) {
                stackStop[-1] = L'\u2026'; // horizontal ellipsis
                return stackStop - 1;
            }

            digitStart[-1] = L'[';
            _itow_s(index, digitStart, digitCount + 1, 10);
            stackStop[-1] = L']';
            stackStop = stackStart + (i > 0);
        }
    }

    return stackStop;
}

#define MAX_FORMAT_LENGTH 200
#define FORMATTED_ERROR_LENGTH MAX_FORMAT_LENGTH + MAX_STACK_LENGTH \
    + MAX_TOKEN_LENGTH - 3
WCHAR parseJSONOut[FORMATTED_ERROR_LENGTH];
LPWSTR parseJSON(
    LPCBYTE buffer, LPCBYTE stop, Hook *hooks, int hookCount, int *lineNumber
) {
    LPCBYTE json = buffer;
    Stack stack;
    stack.count = 0;
    stack.warning = NULL;
    stack.poisonedCount = 0;
    ParserState state;
    state.stack = &stack;
    state.hooks = hooks;
    state.count = hookCount;
    LPCWSTR errorFormat = parseJSONHelp(&json, stop, &state);
    if (errorFormat == NULL) {
        errorFormat = stack.warning;
        if (errorFormat == NULL) { return NULL; }
        json = stack.warningLocation;
        stack.count = stack.warningCount;
        memcpy(
            stack.frames, stack.warningFrames, stack.count * sizeof(Frame)
        );
    }

    BOOL uppercaseStack = !wcsncmp(errorFormat, L"%1$", 3);
    LPCWSTR stackString = getStack(&stack, stop, uppercaseStack);
    BOOL uppercaseToken = !wcsncmp(errorFormat, L"%2$", 3);
    LPCWSTR token = getToken(json, stop, uppercaseToken);
    _swprintf_p(
        parseJSONOut, FORMATTED_ERROR_LENGTH, errorFormat, stackString, token
    );
    *lineNumber = 1;
    for (; buffer < json; buffer++) { *lineNumber += *buffer == '\n'; }
    return parseJSONOut;
}

#define TYPE_ERROR_LENGTH 20 + 2 * MAX_JSON_TYPE_LENGTH - 2
WCHAR checkTypeOut[TYPE_ERROR_LENGTH];
LPCWSTR checkType(JSON_TYPE actual, JSON_TYPE expected, BOOL errorVisible) {
    if (actual & expected) {
        return NULL;
    } else if ((actual & JSON_NUMBER) && (expected & JSON_INT)) {
        return L"Could not parse %1$s value %2$s as an int";
    } else if (errorVisible) {
        _snwprintf_s(
            checkTypeOut, TYPE_ERROR_LENGTH, _TRUNCATE,
            L"%%1$s must be %s, not %s",
            jsonTypeToString(expected), jsonTypeToString(actual)
        );
        return checkTypeOut;
    } else { return L"dummy type error"; }
}

LPCWSTR expectObject(
    LPCBYTE *json, LPCBYTE stop, JSON_TYPE jsonType, int index, void *param,
    void *dest, BOOL errorVisible
) { return checkType(jsonType, JSON_OBJECT, errorVisible); }

LPCWSTR parseColor(
    LPCBYTE *json, LPCBYTE stop, JSON_TYPE jsonType, int index, void *param,
    void *dest, BOOL errorVisible
) {
    LPCWSTR error = checkType(jsonType, JSON_STRING, errorVisible);
    if (error != NULL) { return error; }
    chomp('"', json, stop);
    if (!chomp('#', json, stop)) { return L"%1$s must start with #%2$.0s"; }
    int length = stop - 1 - *json;
    if (length != 3 && length != 6) {
        return L"%1$s must have 3 or 6 characters after #%2$.0s";
    }

    while (
        chompRange('0', '9', json, stop)
            || chompRange('a', 'f', json, stop)
            || chompRange('A', 'F', json, stop)
    );
    if (*json < stop - 1) {
        return L"%1$s contains non-hexadecimal character %2$.1s";
    }

    *json -= length;
    char buffer[3];
    long channels[3];
    for (int i = 0; i < 3; i++) {
        strncpy_s(buffer, 3, (LPCSTR)*json + i * length / 3, 2);
        if (length == 3) { buffer[1] = buffer[0]; }
        channels[i] = strtol(buffer, NULL, 16);
    }

    COLORREF value = RGB(channels[0], channels[1], channels[2]);
    *(COLORREF*)dest = value;
    return NULL;
}

LPCWSTR parseBool(
    LPCBYTE *json, LPCBYTE stop, JSON_TYPE jsonType, int index, void *param,
    void *dest, BOOL errorVisible
) {
    LPCWSTR error = checkType(jsonType, JSON_BOOL, errorVisible);
    if (error != NULL) { return error; }
    chomp('"', json, stop);
    BOOL value = chomp('t', json, stop);
    *(BOOL*)dest = value;
    return NULL;
}

LPCWSTR parseInt(
    LPCBYTE *json, LPCBYTE stop, JSON_TYPE jsonType, int index, void *param,
    void *dest, BOOL errorVisible
) {
    LPCWSTR error = checkType(jsonType, JSON_INT, errorVisible);
    if (error != NULL) { return error; }
    char *dummy;
    long value = strtol(*json, &dummy, 10);
    if (param != NULL) {
        int *range = (int*)param;
        if (!(value <= range[1])) { return L"%1$s value %2$s is too high"; }
        if (!(value >= range[0])) { return L"%1$s value %2$s is too low"; }
    }

    *(int*)dest = value;
    return NULL;
}

LPCWSTR parseDouble(
    LPCBYTE *json, LPCBYTE stop, JSON_TYPE jsonType, int index, void *param,
    void *dest, BOOL errorVisible
) {
    LPCWSTR error = checkType(jsonType, JSON_NUMBER, errorVisible);
    if (error != NULL) { return error; }
    char *dummy;
    double value = strtod(*json, &dummy);
    if (param != NULL) {
        double *range = (double*)param;
        if (!(value <= range[1])) { return L"%1$s value %2$s is too high"; }
        if (!(value >= range[0])) { return L"%1$s value %2$s is too low"; }
    }

    *(double*)dest = value;
    return NULL;
}

#endif
