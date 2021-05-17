#define UNICODE
#include <windows.h>
#include <stdio.h>

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
    int length = strlen(bite);
    if (*json + length <= stop && !strncmp(*json, bite, length)) {
        *json += length;
        return TRUE;
    }

    return FALSE;
}

BOOL parseInt(LPCBYTE *json, LPCBYTE stop, int *integer) {
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
    } else if (parseInt(json, stop, &integer)) {
        result = JSON_INT | JSON_NUMBER;
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

typedef enum {
    VALUE_CONTEXT = 0, FIRST_KEY_CONTEXT, KEY_CONTEXT, CONTEXT_COUNT,
} StringContext;
int getCharSize(LPCBYTE json, LPCBYTE stop, LPCWSTR **errors) {
    if (json >= stop) {
        LPCWSTR eofErrors[CONTEXT_COUNT] = {
            L"Unexpected %2$s in %1$s",
            L"Unexpected %2$s in first key of %1$s object",
            L"Unexpected %2$s in key after %1$s",
        };
        *errors = eofErrors; // Unexpected end of file
    } else if (*json < 0x20) {
        LPCWSTR unescapedErrors[CONTEXT_COUNT] = {
            L"Unescaped %2$s in %1$s",
            L"Unescaped %2$s in first key of %1$s object",
            L"Unescaped %2$s in key after %1$s",
        };
        *errors = unescapedErrors; // Unescaped U+1F
    } else if (*json < 0x80) {
        return 1;
    } else {
        // https://en.wikipedia.org/wiki/UTF-8#Encoding
        LPWSTR invalidErrors[CONTEXT_COUNT] = {
            L"%2$s in %1$s",
            L"%2$s in first key of %1$s object",
            L"%2$s in key after %1$s",
        };
        if (*json < 0xc0) {
            // Invalid UTF-8 byte 0x95
        } else if (json + 1 >= stop || (json[1] & 0xc0) != 0x80) {
            // Incomplete UTF-8 character 0xd3
        } else if (*json < 0xe0) {
            return 2;
        } else if (json + 2 >= stop || (json[2] & 0xc0) != 0x80) {
            // Incomplete UTF-8 character 0xe481
        } else if (*json < 0xf0) {
            return 3;
        } else if (json + 3 >= stop || (json[3] & 0xc0) != 0x80) {
            // Incomplete UTF-8 character 0xf48182
        } else if (*json < 0xf8) {
            return 4;
        } else {
            // Invalid UTF-8 byte 0xfb
        }

        *errors = invalidErrors;
    }

    return 0;
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
                L"%1$s object missing \" before first key %2$s",
                L"After %1$s, missing \" before next key %2$s",
            };
            return errors[context];
        }
    }

    while (TRUE) {
        if (chomp('"', json, stop)) { return NULL; }
        if (chomp('\\', json, stop)) {
            if (chomp('u', json, stop)) {
                LPCBYTE temp = *json;
                for (int i = 0; i < 4; i++) {
                    if (
                        !chompRange('0', '9', &temp, stop)
                            && !chompRange('a', 'f', &temp, stop)
                            && !chompRange('A', 'F', &temp, stop)
                    ) {
                        LPCWSTR escapeErrors[CONTEXT_COUNT] = {
                            L"Invalid escape sequence \\u%2$.4s in %1$s",
                            L"Invalid escape sequence \\u%2$.4s in first key"
                                L" of %1$s object",
                            L"Invalid escape sequence \\u%2$.4s in key after"
                                L" %1$s",
                        };
                        LPCWSTR *errors = escapeErrors;
                        if (getCharSize(temp, stop, &errors) <= 0) {
                            *json = temp;
                        }

                        return errors[context];
                    }
                }

                *json = temp;
            } else if (!chompAny("\"\\/bfnrt", json, stop)) {
                LPCWSTR escapeErrors[CONTEXT_COUNT] = {
                    L"Invalid escape sequence \\%2$.1s in %1$s",
                    L"Invalid escape sequence \\%2$.1s in first key of %1$s"
                        L" object",
                    L"Invalid escape sequence \\%2$.1s in key after %1$s",
                };
                LPCWSTR *errors = escapeErrors;
                getCharSize(*json, stop, &errors);
                return errors[context];
            }
        } else {
            LPCWSTR *errors = NULL;
            *json += getCharSize(*json, stop, &errors);
            if (errors != NULL) { return errors[context]; }
        }
    }
}

int compareHookWithStack(LPCBYTE hookKey, LPCBYTE stackKey) {
    if (hookKey == NULL || stackKey == NULL) {
        return (hookKey != NULL) - (stackKey != NULL);
    }

    int stackKeyLength = 0;
    while (stackKey[1 + stackKeyLength] != '"') { stackKeyLength++; }
    int comparison = strncmp(hookKey, stackKey + 1, stackKeyLength);
    if (comparison != 0) { return comparison; }
    return hookKey[stackKeyLength] != '\0';
}

#define HOOK_FRAME_COUNT 5
typedef struct {
    LPCWSTR (*call)(
        LPCBYTE *json, LPCBYTE stop, JSON_TYPE jsonType, int index,
        void* param, BOOL errorVisible
    );
    void* param;
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
        return L"%1$s array must start with [, not %2$s";
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
            return L"Missing comma or ] between %1$s and %2$s";
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
        return L"%1$s object must start with {, not %2$s";
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
            return L"Missing colon between %1$s key and %2$s";
        }

        error = parseValue(json, stop, &newState);
        if (error) { return error; }
        while (chompAny(WHITESPACE, json, stop));
        if (chomp('}', json, stop)) { state->stack->count--; return NULL; }
        LPCBYTE beforeComma = *json;
        if (*json >= stop) {
            return L"Unexpected %2$s after %1$s";
        } else if (!chomp(',', json, stop)) {
            return L"Missing comma or } between %1$s value and %2$s";
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
        return L"Expected end of file after %1$s object, not %2$s";
    }

    return NULL;
}

#define MAX_TOKEN_LENGTH 80
WCHAR tokenOut[MAX_TOKEN_LENGTH];
LPCWSTR getToken(LPCBYTE json, LPCBYTE stop, BOOL uppercase) {
    if (json >= stop) {
        wcsncpy_s(tokenOut, MAX_TOKEN_LENGTH, L"end of file", _TRUNCATE);
        if (uppercase) { *tokenOut = L'E'; }
        return tokenOut;
    } else if (*json < 0x20) {
        LPCWSTR names[0x20] = {
            L"null character", L"U+0001", L"U+0002", L"U+0003", L"U+0004",
            L"U+0005", L"U+0006", L"U+0007", L"U+0008", L"tab character",
            L"newline", L"U+000B", L"U+000C", L"carriage return", L"U+000E",
            L"U+000F", L"U+0010", L"U+0011", L"U+0012", L"U+0013", L"U+0014",
            L"U+0015", L"U+0016", L"U+0017", L"U+0018", L"U+0019", L"U+001A",
            L"U+001B", L"U+001C", L"U+001D", L"U+001E", L"U+001F",
        };
        wcsncpy_s(tokenOut, MAX_TOKEN_LENGTH, names[*json], _TRUNCATE);
        if (uppercase) {
            *tokenOut = L"NUUUUUUUUTNUUCUUUUUUUUUUUUUUUUUU"[*json];
        }

        return tokenOut;
    } else if (*json >= 0x80) {
        // https://en.wikipedia.org/wiki/UTF-8#Encoding
        int byteCount = 0;
        if (*json < 0xc0) {
            _snwprintf_s(
                tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
                L"invalid UTF-8 byte 0x%02x", *json, json[1]
            );
        } else if (json + 1 >= stop || (json[1] & 0xc0) != 0x80) {
            _snwprintf_s(
                tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
                L"incomplete UTF-8 character 0x%02x", *json
            );
        } else if (*json < 0xe0) {
            byteCount = 2;
        } else if (json + 2 >= stop || (json[2] & 0xc0) != 0x80) {
            _snwprintf_s(
                tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
                L"incomplete UTF-8 character 0x%02x%02x", *json, json[1]
            );
        } else if (*json < 0xf0) {
            byteCount = 3;
        } else if (json + 3 >= stop || (json[3] & 0xc0) != 0x80) {
            _snwprintf_s(
                tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
                L"incomplete UTF-8 character 0x%02x%02x%02x", *json, json[1],
                json[2]
            );
        } else if (*json < 0xf8) {
            byteCount = 4;
        } else {
            _snwprintf_s(
                tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
                L"invalid UTF-8 byte 0x%02x", *json, json[1]
            );
        }

        if (byteCount <= 0) {
            if (uppercase) { *tokenOut = L'I'; }
            return tokenOut;
        }
    }

    LPCBYTE temp = json;
    while (
        chompRange('0', '9', &temp, stop)
            || chompRange('a', 'z', &temp, stop)
            || chompRange('A', 'Z', &temp, stop)
            || chompAny("_.+-", &temp, stop)
    );
    int tokenLength = min(max(temp - json, 1), MAX_TOKEN_LENGTH);
    MultiByteToWideChar(
        CP_UTF8, MB_PRECOMPOSED, json, stop - json, tokenOut, tokenLength
    );
    if (tokenLength >= MAX_TOKEN_LENGTH) {
        tokenLength--;
        tokenOut[tokenLength - 1] = L'\u2026'; // horizontal ellipsis
    }

    tokenOut[tokenLength] = L'\0';
    return tokenOut;
}

#define MAX_STACK_LENGTH 150
WCHAR stackOut[MAX_STACK_LENGTH];
LPCWSTR getStack(const Stack *stack, LPCBYTE stop, BOOL uppercase) {
    if (stack->count <= 0) { return uppercase ? L"Top-level" : L"top-level"; }
    LPWSTR stackStop = stackOut + MAX_STACK_LENGTH - 1;
    *stackStop = L'\0';
    for (int i = stack->count - 1; i >= 0; i--) {
        LPCBYTE key = stack->frames[i].key;
        int index = stack->frames[i].index;
        if (key) {
            LPCBYTE keyStop = key + 1;
            for (; keyStop < stop; keyStop++) {
                if (*keyStop == '"' && keyStop[-1] != '\\') {
                    keyStop++; break;
                }
            }

            LPCBYTE temp = key + 1;
            if (!chompRange('0', '9', &temp, keyStop - 1)) {
                while (
                    chompRange('0', '9', &temp, keyStop - 1)
                        || chompRange('a', 'z', &temp, keyStop - 1)
                        || chompRange('A', 'Z', &temp, keyStop - 1)
                        || chomp('_', &temp, keyStop - 1)
                );
                if (temp > key + 1 && temp >= keyStop - 1) {
                    key++; keyStop--;
                }
            }

            int keyLength = MultiByteToWideChar(
                CP_UTF8, MB_PRECOMPOSED, key, keyStop - key, NULL, 0
            );
            LPWSTR keyStart = stackStop - keyLength;
            LPWSTR stackStart = keyStart - 2 * (i > 0);
            if (stackStart < stackOut) {
                stackStop--;
                *stackStop = L'\u2026'; // horizontal ellipsis
                if (i < stack->count - 1) { return stackStop; }
                keyStart += stackOut - stackStart;
                stackStart = stackOut;
            }

            if (i > 0) { stackStart[1] = L'.'; }
            MultiByteToWideChar(
                CP_UTF8, MB_PRECOMPOSED, key, keyStop - key,
                keyStart, stackStop - keyStart
            );
            stackStop = stackStart + (i > 0);
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

    return stackStop + (*stackStop == L'.');
}

#define SETTINGS_FILENAME L"settings.json"
#define MAX_LOCATION_LENGTH sizeof(SETTINGS_FILENAME) \
    + sizeof(STRINGIFY(INT_MAX)) + sizeof(L" line : ") - 2
WCHAR locationOut[MAX_LOCATION_LENGTH];
LPCWSTR getLocation(LPCBYTE start, LPCBYTE offset) {
    int lineNumber = 1;
    for (; start < offset; start++) { lineNumber += *start == '\n'; }
    _snwprintf_s(
        locationOut, MAX_LOCATION_LENGTH, _TRUNCATE,
        L"%s line %d: ", SETTINGS_FILENAME, lineNumber
    );
    return locationOut;
}

#define MAX_FORMAT_LENGTH 200
#define FORMATTED_ERROR_LENGTH MAX_LOCATION_LENGTH + MAX_FORMAT_LENGTH \
    + MAX_STACK_LENGTH + MAX_TOKEN_LENGTH - 3
WCHAR parseJSONOut[FORMATTED_ERROR_LENGTH];
LPCWSTR parseJSON(LPCBYTE buffer, LPCBYTE stop, Hook *hooks, int hookCount) {
    LPCBYTE json = buffer;
    Stack stack = { .count = 0, .warning = NULL, .poisonedCount = 0 };
    ParserState state = {
        .stack = &stack, .hooks = hooks, .count = hookCount,
    };
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

    LPCWSTR location = getLocation(buffer, json);
    wcsncpy_s(parseJSONOut, MAX_LOCATION_LENGTH, location, _TRUNCATE);
    int locationLength = wcsnlen_s(location, MAX_LOCATION_LENGTH);
    BOOL uppercaseStack = !wcsncmp(errorFormat, L"%1$", 3);
    LPCWSTR stackString = getStack(&stack, stop, uppercaseStack);
    BOOL uppercaseToken = !wcsncmp(errorFormat, L"%2$", 3);
    LPCWSTR token = getToken(json, stop, uppercaseToken);
    _swprintf_p(
        parseJSONOut + locationLength,
        FORMATTED_ERROR_LENGTH - locationLength,
        errorFormat, stackString, token
    );
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
    BOOL errorVisible
) { return checkType(jsonType, JSON_OBJECT, errorVisible); }

LPCWSTR parseColor(
    LPCBYTE *json, LPCBYTE stop, JSON_TYPE jsonType, int index, void *param,
    BOOL errorVisible
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
        strncpy_s(buffer, 3, *json + i * length / 3, 2);
        if (length == 3) { buffer[1] = buffer[0]; }
        channels[i] = strtol(buffer, NULL, 16);
    }

    *(COLORREF*)param = RGB(channels[0], channels[1], channels[2]);
    return NULL;
}

OVERLAPPED fileReadIn;
struct { DWORD errorCode; DWORD byteCount; } fileReadOut;
void CALLBACK fileReadComplete(
    DWORD errorCode, DWORD byteCount, LPOVERLAPPED overlapped
) {
    fileReadOut.errorCode = errorCode;
    fileReadOut.byteCount = byteCount;
}

LPBYTE readFile(LPCWSTR path, LPCBYTE *stop) {
    // https://docs.microsoft.com/en-us/windows/win32/fileio/opening-a-file-for-reading-or-writing
    HANDLE file = CreateFile(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL, // default security settings
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL // no attribute template
    );
    if (file == INVALID_HANDLE_VALUE) { return NULL; }
    while (TRUE) { // fake loop as a substitute for context manager
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(file, &fileSize)) { break; }
        if (fileSize.HighPart) { break; }
        LPBYTE buffer = malloc(fileSize.LowPart);
        if (fileSize.LowPart > 0 && !buffer) { break; }
        while (TRUE) {
            fileReadIn.Offset = fileReadIn.OffsetHigh = 0;
            fileReadOut.errorCode = 0;
            fileReadOut.byteCount = 0;
            if (
                !ReadFileEx(
                    file,
                    buffer,
                    fileSize.LowPart,
                    &fileReadIn,
                    fileReadComplete
                )
            ) { break; }
            SleepEx(1000, TRUE);
            if (fileReadOut.errorCode) { break; }
            *stop = buffer + fileReadOut.byteCount;
            CloseHandle(file);
            return buffer;
        }

        free(buffer);
        break;
    }

    CloseHandle(file);
    *stop = NULL;
    return NULL;
}

int main() {
    COLORREF labelColor = RGB(1, 2, 3);
    COLORREF borderColor = RGB(4, 5, 6);
    Hook hooks[3] = {
        { .call = expectObject, .frameCount = 0 },
        {
            .call = parseColor,
            .param = &borderColor,
            .frames = { "borderColor" },
            .frameCount = 1,
        },
        {
            .call = parseColor,
            .param = &labelColor,
            .frames = { "labelColor" },
            .frameCount = 1,
        },
    };
    LPCBYTE stop;
    LPBYTE buffer = readFile(SETTINGS_FILENAME, &stop);
    LPCWSTR error = parseJSON(buffer, stop, hooks, 3);
    if (error) { wprintf(L"%s\n", error); }
    wprintf(
        L"labelColor: red = %d, green = %d, blue = %d\n",
        GetRValue(labelColor), GetGValue(labelColor), GetBValue(labelColor)
    );
    wprintf(
        L"borderColor: red = %d, green = %d, blue = %d\n",
        GetRValue(borderColor), GetGValue(borderColor), GetBValue(borderColor)
    );

    WCHAR path[MAX_PATH] = L"../JSONTestSuite/test_parsing/";
    LPWSTR name = path + wcsnlen(path, MAX_PATH);
    // https://docs.microsoft.com/en-us/windows/win32/fileio/listing-the-files-in-a-directory
    WIN32_FIND_DATA info;
    wcsncpy(name, L"*", path + MAX_PATH - name);
    HANDLE find = FindFirstFile(path, &info);
    if (find == INVALID_HANDLE_VALUE) { return 1; }
    do {
        if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { continue; }
        wcsncpy(name, info.cFileName, path + MAX_PATH - name);
        LPCBYTE stop;
        LPBYTE buffer = readFile(path, &stop);
        LPCWSTR error = parseJSON(buffer, stop, NULL, 0);
        if (error || info.cFileName[0] == L'n') {
            wprintf(
                L"%-50s %s\n", info.cFileName, error ? error : L"No error!"
            );
        }

        free(buffer);
    } while (FindNextFile(find, &info));

    FindClose(find);
    wprintf(L"tests complete\n");
    return 0;
}
